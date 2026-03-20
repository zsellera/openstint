#include "commons.hpp"

#include <cstdlib>
#include <complex>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include <zmq.hpp>

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

static enum FrameParseMode { FRAME_SEEK, FRAME_FOUND } frame_parse_mode = FRAME_SEEK;
static FrameDetector frame_detector(0.84f - 0.01f*SAMPLES_PER_SYMBOL);
static SymbolReader symbol_reader;
static Frame frame;
static PassingDetector passing_detector;
static RxStatistics rx_stats;
static bool monitor_mode = false;
static const uint64_t startup_ts = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
static bool mode_sysclk = false;
static uint64_t timecode = 0ul;

static RC4Registry rc4_registry;
static RC4Trainer rc4_trainer;

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
        case TransponderProtocol::RC3:
        if (decode_rc3(softbits, &transponder_id)) {
            if (transponder_id < 10000000) { // extra check (7-digit max)
                passing_detector.append(frame, transponder_id);
            }
            return true;
        }
        break;
        case TransponderProtocol::RC4:
        RC4Message msg(softbits);
        if (rc4_registry.lookup(msg, &transponder_id)) {
            passing_detector.append(frame, transponder_id);
            rc4_trainer.append(frame->timestamp, frame->rssi(), transponder_id, softbits);
            return true;
        }
        rc4_trainer.append(frame->timestamp, frame->rssi(), 0, softbits);
        return false;
    }
    return false;
}

void detect_frames(const std::complex<int8_t>* samples, std::size_t sample_count) {
    const uint64_t timestamp = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count() - startup_ts;

    bool frame_detected = false;
    for (uint32_t idx=0; (idx+SAMPLES_PER_SYMBOL)<=sample_count; idx+=SAMPLES_PER_SYMBOL) {
        if (frame_parse_mode == FRAME_SEEK) {
            const std::optional<TransponderProtocol> detected = frame_detector.process_baseband(samples+idx);
            if (detected) {
                frame_parse_mode = FRAME_FOUND;
                frame_detected = true; // do not use this buffer for noisefloor calculation
                frame = Frame(
                    detected.value(),
                    timestamp + (idx * 1000000ul / SAMPLE_RATE),
                    timecode + idx
                );
                symbol_reader.read_preamble(&frame, frame_detector.dc_offset(), samples, idx+SAMPLES_PER_SYMBOL);
            }
        } else if (frame_parse_mode == FRAME_FOUND) {
            symbol_reader.read_symbol(&frame, frame_detector.dc_offset(), samples+idx);
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
        // there was an actice frame in the buffer, do not update
        // statistics, as the received data messes with the
        // noise/dc-offset calculation
        frame_detector.reset_statistics_counters();
    } else {
        frame_detector.update_statistics();
        rx_stats.save_channel_characteristics(frame_detector.dc_offset(), frame_detector.noise_energy());
    }
}

bool parse_common_arguments(int& i, const int argc, const std::string& arg, char** argv) {
    if (arg == "-p" && i + 1 < argc) {
        zmq_port = std::atoi(argv[++i]);
    } else if (arg == "-m") {
        monitor_mode = true;
    } else if (arg == "-t") {
        mode_sysclk = true;
    } else {
        return false;
    }
    return true;
}

void init_commons() {
    // transponder processing (allocate viterbi trellis); TODO RAII
    init_transponders();

    //  Prepare our context and publisher
    std::string zmq_address;
    std::format_to(std::back_inserter(zmq_address), "tcp://*:{}", zmq_port);
    zmq_context = new zmq::context_t(1);
    publisher = new zmq::socket_t(*zmq_context, zmq::socket_type::pub);
    publisher->bind(zmq_address);
    std::cout << "Listening on " << zmq_address << std::endl;
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

    std::vector<Passing> passings = passing_detector.identify_passings(now_ts - 250000ul);
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
            const auto report = std::format("L {} START", status_ts);
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
            auto messages = rc4_trainer.registry_messages();
            auto [tsmin, tsmax] = rc4_trainer.buffer_timerange();
            auto detected_transponders = passing_detector.passings_between(TransponderSystem::AMB, tsmin, tsmax);
            if (transponder_id == 0 && detected_transponders.size() == 1) {
                transponder_id = detected_transponders.front();
            }
            rc4_registry.store(transponder_id, messages);
            const auto report = std::format("L {} DONE {} {}", status_ts, transponder_id, messages.size());
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
}