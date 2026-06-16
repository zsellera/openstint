#include "commons.hpp"

#include <cstdlib>
#include <complex>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <zmq.hpp>

#include "crash_handler.hpp"
#include "preamble.hpp"
#include "transponder.hpp"
#include "frame.hpp"
#include "passing.hpp"
#include "counters.hpp"
#include "rc4.hpp"

using namespace std::chrono;

static int zmq_port = DEFAULT_ZEROMQ_PORT;
static zmq::context_t* zmq_context = nullptr;
static zmq::socket_t* publisher = nullptr;

static enum FrameParseMode { FRAME_SEEK, FRAME_WAIT, FRAME_FOUND } frame_parse_mode = FRAME_SEEK;
static int pending_trail = 0; // symbols left to wait before the centered EQ window is full
static FrameDetector frame_detector(0.68f);
static SymbolReader symbol_reader;
static Frame frame;
static PassingDetector passing_detector;
static RxStatistics rx_stats;
static bool monitor_mode = false;
static const uint64_t startup_ts = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
static bool mode_sysclk = false;
static uint64_t timecode = 0ul;

static std::string storage_dir = ".";
static std::unique_ptr<RC4FileBasedRegistry> rc4_registry;
static RC4Trainer rc4_trainer;
static AmbRcBlacklist ambrc_blacklist;

bool process_frame(Frame* frame) {
    if (monitor_mode) {
        std::cout << "F " << *frame << std::endl;
    }

    const uint8_t *softbits = frame->bits();
    if (!softbits) {
        // preamble not found
        return false;
    }

    uint32_t transponder_id;
    switch (frame->transponder_protocol) {
        case TransponderProtocol::OpenStint:
        if (decode_openstint(softbits, &transponder_id)) {
            if (transponder_id < 10000000u) {
                passing_detector.append(frame, transponder_id);
            } else if ((transponder_id & 0x00A00000) == 0x00A00000) {
                uint32_t transponder_timestamp = (transponder_id & 0x000FFFFF);
                passing_detector.timesync(frame, transponder_timestamp);
            }
            return true;
        }
        break;
        case TransponderProtocol::RC3: {
            uint8_t status_code;
            if (decode_rc3(softbits, &transponder_id, &status_code)) {
                if (transponder_id >= 10000000) { // not a 7-digit transponder for sure
                    // check for known status/validation message (to track some statistics)
                    return ((status_code & 0x07) == 0); 
                }
                // status byte:
                // https://www.rctech.net/forum/showpost.php?p=16244070&postcount=1171
                // RC4 hybrid and "recent" RC3 indicate status messages in lower 3 bits (0x07 mask)
                // Older RC3 indicate normal messages by setting all bits 1 (0xff)
                
                // Old AMBRc DP transponders send *transponder* frames with all status bits set (0xff);
                // unfortunately newer models can transmit RC3 status/validation messages the same way.
                // Let's build a block-list for such transponders.
                ambrc_blacklist.process(frame->timestamp, status_code, transponder_id);
                if (status_code == 0xff && !ambrc_blacklist.check_banned(transponder_id)) {
                    passing_detector.append(frame, transponder_id);
                } else if ((status_code & 0x07) == 0) { // not a status/validation message for sure
                    passing_detector.append(frame, transponder_id);
                }
                // at this point decoding was success; if status byte indicates
                // non-transponder message, it should not screw decoded statistics
                return true;
            }
        }
        break;
        case TransponderProtocol::RC4: {
            RC4Message msg(softbits);
            if (!msg.is_valid) { // fails validation
                return false;
            }
            if (rc4_registry->lookup(msg.payload, &transponder_id)) {
                passing_detector.append(frame, transponder_id);
                rc4_trainer.append(frame->timestamp, frame->rssi(), transponder_id, msg.payload);
                return true;
            }
            rc4_trainer.append(frame->timestamp, frame->rssi(), 0, msg.payload);
            return true;
        }
    }
    return false;
}

void detect_frames(const std::complex<int8_t>* samples, std::size_t sample_count) {
    const uint64_t timestamp = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count() - startup_ts;

    // on USB hiccup, there might be a super-small buffer, which can not even fit
    // the preamble; these buffers should be dropped as bougus to prevent indexing
    // issues later on.
    if (sample_count < SymbolReader::reserve_buffer_size) {
        timecode += sample_count;
        return; // no meaningful work here
    }

    bool frame_detected = false;
    for (uint32_t idx=0; (idx+SAMPLES_PER_SYMBOL)<=sample_count; idx+=SAMPLES_PER_SYMBOL) {
        if (frame_parse_mode == FRAME_SEEK) {
            const std::optional<TransponderProtocol> detected = frame_detector.process_baseband(samples+idx);
            if (detected) {
                frame_parse_mode = FRAME_WAIT;
                frame_detected = true; // do not use this buffer for noisefloor calculation
                frame = Frame(
                    detected.value(),
                    timestamp + (idx * 1000000ul / SAMPLE_RATE),
                    timecode + idx
                );
                // defer training by fseq_halflen symbols: the centered EQ needs the
                // trailing (future) symbols, which become ordinary past samples once
                // they arrive. timing stays anchored at this detection point.
                pending_trail = SymbolReader::fseq_halflen;
            }
        } else if (frame_parse_mode == FRAME_WAIT) {
            // count the trailing symbols of the centered EQ window; once they are in,
            // train/read the preamble looking both back (lead) and ahead (trailing).
            if (--pending_trail == 0) {
                const int end = idx + SAMPLES_PER_SYMBOL;
                symbol_reader.train_preamble(&frame, samples, end, frame_detector.dc_offset());
                symbol_reader.read_preamble(&frame, samples, end, frame_detector.dc_offset());
                frame_parse_mode = FRAME_FOUND;
            }
        } else if (frame_parse_mode == FRAME_FOUND) {
            symbol_reader.read_symbol(&frame, samples+idx, frame_detector.dc_offset());
            if (symbol_reader.is_frame_complete(&frame)) {
                frame_parse_mode = FRAME_SEEK;
                bool frame_processed = process_frame(&frame);
                rx_stats.register_frame(frame_processed);
            }
        }
    }

    // update global sample counter
    timecode += sample_count;

    // save a small section of the buffer
    // if there is a frame in the next buffer, and read_preamble() must
    // look back, here save the trailing section of the current buffer
    symbol_reader.update_reserve_buffer(samples, sample_count);

    // update counters for noise energy and dc offset
    if (frame_detected) {
        // there was an active frame in the buffer, do not update
        // statistics, as the received data messes with the
        // noise/dc-offset calculation
        frame_detector.reset_statistics_counters();
    } else {
        frame_detector.update_statistics();
        rx_stats.save_channel_characteristics(
            frame_detector.dc_offset(),
            frame_detector.noise_energy()
        );
    }
}

bool parse_common_arguments(int& i, const int argc, const std::string& arg, char** argv) {
    if (arg == "-p" && i + 1 < argc) {
        zmq_port = std::atoi(argv[++i]);
    } else if (arg == "-m") {
        monitor_mode = true;
    } else if (arg == "-t") {
        mode_sysclk = true;
    } else if (arg == "-s" && i + 1 < argc) {
        storage_dir = argv[++i];
    } else {
        return false;
    }
    return true;
}

void init_commons() {
    install_crash_handler();

    // transponder processing (allocate viterbi trellis); TODO RAII
    init_transponders();

    //  Prepare our context and publisher
    std::string zmq_address;
    std::format_to(std::back_inserter(zmq_address), "tcp://*:{}", zmq_port);
    zmq_context = new zmq::context_t(1);
    publisher = new zmq::socket_t(*zmq_context, zmq::socket_type::pub);
    publisher->bind(zmq_address);
    std::cout << "Listening on " << zmq_address << std::endl;

    // initial load rc4 transponder database
    rc4_registry = std::make_unique<RC4FileBasedRegistry>(storage_dir);
    rc4_registry->resync();
}

uint64_t reporting_timestamp(uint64_t timestamp_us, uint64_t steady_now, uint64_t sysclk_now) {
    if (mode_sysclk) {
        return (sysclk_now - (steady_now - timestamp_us))/1000ul;
    } else {
        return timestamp_us/1000ul;
    }
}

void report_detections() {
    const uint64_t now_sysclk = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
    const uint64_t now_ts = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count() - startup_ts;
    const uint64_t status_ts = reporting_timestamp(now_ts, now_ts, now_sysclk);

    // report status once a second
    if (rx_stats.reporting_due(now_ts)) {
        const std::string report = std::format("S {} {}",
            status_ts,
            rx_stats.to_string()
        );
        rx_stats.reset(now_ts);

        std::cout << report << std::endl;
        publisher->send(zmq::buffer(report), zmq::send_flags::none);
    }
    
    std::vector<TimeSync> timesyncs = passing_detector.identify_timesyncs(500000l);
    for (const auto& time_sync : timesyncs) {
        const std::string report = std::format("T {} {} {} {}",
            reporting_timestamp(time_sync.timestamp, now_ts, now_sysclk),
            transponder_system_name(time_sync.transponder_type), // always openstint
            time_sync.transponder_id,
            time_sync.transponder_timestamp
        );

        std::cout << report << std::endl;
        publisher->send(zmq::buffer(report), zmq::send_flags::none);
    }

    std::vector<Passing> passings = passing_detector.identify_passings(now_ts > 250000ul ? (now_ts-250000ul) : 0ul);
    for (const auto& passing : passings) {
        const std::string report = std::format("P {} {} {} {:.2f} {} {}",
            reporting_timestamp(passing.timestamp, now_ts, now_sysclk),
            transponder_system_name(passing.transponder_type),
            passing.transponder_id,
            passing.rssi,
            passing.hits,
            passing.duration
        );

        std::cout << report << std::endl;
        publisher->send(zmq::buffer(report), zmq::send_flags::none);
    }

    auto trainer_result = rc4_trainer.evaluate(now_ts);
    switch (trainer_result) {
        case RC4Trainer::EvaluationResult::START: {
            const auto report = std::format("L {} START {:.1f}", status_ts, rc4_trainer.last_rssi());
            std::cout << report << std::endl;
            publisher->send(zmq::buffer(report), zmq::send_flags::none);
        }
        break;
        case RC4Trainer::EvaluationResult::INTERRUPED: {
            const auto report = std::format("L {} INTERRUPTED", status_ts);
            std::cout << report << std::endl;
            publisher->send(zmq::buffer(report), zmq::send_flags::none);
        }
        break;
        case RC4Trainer::EvaluationResult::DONE: {
            uint32_t transponder_id = rc4_trainer.preferred_transponder_id();
            auto payloads = rc4_trainer.registry_payloads();
            auto [tsmin, tsmax] = rc4_trainer.buffer_timerange();
            auto detected_transponders = passing_detector.passings_between(TransponderSystem::AMB, tsmin, tsmax);
            if (transponder_id == 0 && detected_transponders.size() == 1) {
                transponder_id = detected_transponders.front();
            }
            transponder_id = rc4_registry->store(transponder_id, payloads);
            const auto report = std::format("L {} DONE {} {}", status_ts, transponder_id, payloads.size());
            std::cout << report << std::endl;
            publisher->send(zmq::buffer(report), zmq::send_flags::none);
        }
        break;
        case RC4Trainer::EvaluationResult::RESET: {
            const auto report = std::format("L {} RESET", status_ts);
            std::cout << report << std::endl;
            publisher->send(zmq::buffer(report), zmq::send_flags::none);
        }
        break;
        case RC4Trainer::EvaluationResult::NO_ACTION:
        // no action
        break;
    }

    // re-sync rc4 transponder database
    rc4_registry->resync();
}