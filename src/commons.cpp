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
#include "timebase.hpp"


static int zmq_port = DEFAULT_ZEROMQ_PORT;
static zmq::context_t* zmq_context = nullptr;
static zmq::socket_t* publisher = nullptr;

static enum FrameParseMode { FRAME_SEEK, FRAME_FOUND } frame_parse_mode = FRAME_SEEK;
static FrameDetector frame_detector(0.84f);
static SymbolReader symbol_reader;
static Frame frame;
static PassingDetector passing_detector;
static RxStatistics rx_stats;
static bool monitor_mode = false;
static Timebase timebase;


bool process_frame(Frame* frame) {
    const uint8_t *softbits = frame->bits();
    if (!softbits) {
        // preamble not found
        return false;
    }

    if (monitor_mode) {
        std::cout << "F " << *frame << std::endl;
    }

    uint32_t transponder_id;
    switch (frame->transponder_type) {
        case TransponderType::OpenStint:
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
        case TransponderType::Legacy:
        if (decode_legacy(softbits, &transponder_id)) {
            if (transponder_id < 10000000) { // extra check (7-digit max)
                passing_detector.append(frame, transponder_id);
            }
            return true;
        }
        break;
    }
    return false;
}

void detect_frames(const std::complex<int8_t>* samples, std::size_t sample_count) {
    uint64_t buffer_timestamp = timebase.now();
    bool frame_detected = false;
    for (uint32_t idx=0; (idx+SAMPLES_PER_SYMBOL)<=sample_count; idx+=SAMPLES_PER_SYMBOL) {
        if (frame_parse_mode == FRAME_SEEK) {
            const std::optional<TransponderType> detected = frame_detector.process_baseband(samples+idx);
            if (detected) {
                frame_parse_mode = FRAME_FOUND;
                frame_detected = true; // do not use this buffer for noisefloor calculation
                uint64_t timestamp = buffer_timestamp + (1000 * idx) / SAMPLE_RATE;
                frame = Frame(detected.value(), timestamp);
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
        timebase.use_system_clock();
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

void report_detections() {
    const auto now = timebase.now();

    // report status once a second
    if (rx_stats.reporting_due(now)) {
        const std::string report = std::format("S {} {}", now, rx_stats.to_string());
        rx_stats.reset(now);

        std::cout << report << std::endl;
        publisher->send(zmq::buffer(report), zmq::send_flags::none);
    }
    
    std::vector<TimeSync> timesyncs = passing_detector.identify_timesyncs(500ul);
    for (const auto& time_sync : timesyncs) {
        const std::string report = std::format("T {} {} {} {}",
            time_sync.timestamp,
            transponder_props(time_sync.transponder_type).prefix, // always openstint
            time_sync.transponder_id,
            time_sync.transponder_timestamp
        );

        std::cout << report << std::endl;
        publisher->send(zmq::buffer(report), zmq::send_flags::none);
    }

    std::vector<Passing> passings = passing_detector.identify_passings(now - 250ul);
    for (const auto& passing : passings) {
        const std::string report = std::format("P {} {} {} {:.2f} {}",
            passing.timestamp,
            transponder_props(passing.transponder_type).prefix,
            passing.transponder_id,
            passing.rssi,
            passing.hits
        );

        std::cout << report << std::endl;
        publisher->send(zmq::buffer(report), zmq::send_flags::none);
    }
}