#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>
#include <iomanip>
#include <cmath>
#include <unistd.h>
#include <atomic>
#include <iostream>
#include <thread>
#include <chrono>
#include <complex>
#include <algorithm>
#include <numeric>
#include <functional>
#include <algorithm>
#include <vector>

#include "complex_cast.hpp"

#include <zmq.hpp>
#include <zmq_addon.hpp>

#include <libhackrf/hackrf.h>
#include <liquid/liquid.h>

#include "preamble.hpp"
#include "transponder.hpp"
#include "frame.hpp"
#include "passing.hpp"


static hackrf_device* device = nullptr;
static std::atomic<bool> do_exit(false);

static const uint64_t CENTER_FREQ_HZ       = 5000000ULL;
static const uint32_t SAMPLE_RATE          = 5000000;
static const uint32_t SYMBOL_RATE          = 1250000;
static const uint32_t BB_FILTER_BW         = 1750000;
static const uint32_t SAMPLES_PER_SYMBOL   = SAMPLE_RATE / SYMBOL_RATE;
static const uint8_t DEFAULT_LNA_GAIN      = 32;           // 0-40 in steps of 8 or so; experiment
static const uint8_t DEFAULT_VGA_GAIN      = 24;           // 0-62

static enum FrameParseMode { FRAME_SEEK, FRAME_FOUND } frame_parse_mode = FRAME_SEEK;
static FrameDetector frame_detector(0.9f);
static SymbolReader symbol_reader;
static Frame frame;

static PassingDetector passing_detector;

// signal handler to break the capture loop
void signal_handler(int signum) {
    std::cerr << "\nCaught signal " << signum << " — stopping...\n";
    do_exit = true;
}

void process_frame(Frame* frame) {
    // std::cout << "\n\nframe " << *frame << std::endl;
    const uint8_t *softbits = frame->bits();
    if (!softbits) {
        // preamble not found
        return;
    }

    uint32_t transponder_id;
    switch (frame->transponder_type) {
        case TransponderType::OpenStint:
        if (decode_openstint(softbits, &transponder_id)) {
            if (transponder_id < 10000000u) {
                passing_detector.append(transponder_id, frame->timestamp, frame->rssi);
            } else if ((transponder_id & 0x00A00000) == 0x00A00000) {
                std::cout << "TIMESYNC " << (transponder_id & 0x000FFFFF) << std::endl;
            }
        }
        break;
        case TransponderType::Legacy:
            if (decode_legacy(softbits, &transponder_id)) {
                if (transponder_id < 10000000) {
                    passing_detector.append(transponder_id, frame->timestamp, frame->rssi);
                }
            }
        break;
    }
}

// hackrf callback invoked for each block of data
extern "C" int rx_callback(hackrf_transfer* transfer) {
    if (do_exit) {
        return 0;
    }

    uint64_t buffer_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    uint32_t sample_count = transfer->valid_length / 2;
    const std::complex<int8_t> *samples = reinterpret_cast<const std::complex<int8_t>*>(transfer->buffer);
    
    bool frame_detected = false;
    for (uint32_t idx=0; (idx+SAMPLES_PER_SYMBOL)<=sample_count; idx+=SAMPLES_PER_SYMBOL) {
        if (frame_parse_mode == FRAME_SEEK) {
            const std::optional<TransponderType> detected = frame_detector.process_baseband(samples+idx);
            if (detected) {
                frame_parse_mode = FRAME_FOUND;
                frame_detected = true; // do not use this buffer for noisefloor calculation
                uint64_t timestamp = buffer_timestamp + (1000 * idx) / SAMPLE_RATE;
                frame = Frame(detected.value(), timestamp, frame_detector.symbol_energy2(), frame_detector.noise_energy2());
                symbol_reader.read_preamble(&frame, frame_detector.dc_offset(), samples, idx+4);
            }
        } else if (frame_parse_mode == FRAME_FOUND) {
            symbol_reader.read_symbol(&frame, frame_detector.dc_offset(), samples+idx);
            if (symbol_reader.is_frame_complete(&frame)) {
                frame_parse_mode = FRAME_SEEK;
                process_frame(&frame);
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
    }

    // Returning 0 indicates "keep going".
    return 0;
}    

int main(int argc, char** argv) {
    int result = HACKRF_SUCCESS;

    const uint64_t freq_hz = CENTER_FREQ_HZ;
    const uint32_t sample_rate = SAMPLE_RATE;
    const uint32_t filter_bw = BB_FILTER_BW;
    const uint8_t lna_gain = DEFAULT_LNA_GAIN;
    const uint8_t vga_gain = DEFAULT_VGA_GAIN;

    init_transponders();

    //  Prepare our context and publisher
    zmq::context_t context(1);
    zmq::socket_t publisher(context, zmq::socket_type::pub);
    publisher.bind("tcp://*:5556");

    std::cout << "HackRF RX: freq=" << freq_hz << " Hz, sample_rate=" << sample_rate
              << " Hz, LNA=" << (int)lna_gain << ", VGA=" << (int)vga_gain << "\n";

    // install signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // init lib
    result = hackrf_init();
    if (result != HACKRF_SUCCESS) {
        std::fprintf(stderr, "hackrf_init() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        return EXIT_FAILURE;
    }

    // open the first available device
    result = hackrf_open(&device);
    if (result != HACKRF_SUCCESS || device == nullptr) {
        std::fprintf(stderr, "hackrf_open() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        hackrf_exit();
        return EXIT_FAILURE;
    }

    // set center frequency
    result = hackrf_set_freq(device, freq_hz);
    if (result != HACKRF_SUCCESS) {
        std::fprintf(stderr, "hackrf_set_freq() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        goto cleanup;
    }

    // set sample rate (Hz)
    result = hackrf_set_sample_rate(device, sample_rate);
    if (result != HACKRF_SUCCESS) {
        std::fprintf(stderr, "hackrf_set_sample_rate() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        goto cleanup;
    }

    // set filter BW (Hz)
    result = hackrf_set_baseband_filter_bandwidth(device, filter_bw);
    if (result != HACKRF_SUCCESS) {
        std::fprintf(stderr, "hackrf_set_baseband_filter_bandwidth() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        goto cleanup;
    }

    // set LNA and VGA gains (optional)
    result = hackrf_set_lna_gain(device, lna_gain);
    if (result != HACKRF_SUCCESS) {
        std::fprintf(stderr, "hackrf_set_lna_gain() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        // not fatal; continue
    }
    result = hackrf_set_vga_gain(device, vga_gain);
    if (result != HACKRF_SUCCESS) {
        std::fprintf(stderr, "hackrf_set_vga_gain() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        // not fatal; continue
    }

    // (Optional) enable amplified antenna path (if you have an amplifier on board)
    // hackrf_set_amp_enable(device, 0);

    // start receiving (callback provides raw interleaved I/Q samples)
    result = hackrf_start_rx(device, rx_callback, nullptr);
    if (result != HACKRF_SUCCESS) {
        std::fprintf(stderr, "hackrf_start_rx() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        goto cleanup;
    }

    std::cerr << "Streaming... stop with Ctrl-C\n";

    // main loop — exit when handler sets do_exit (Ctrl-C) or device stops
    while (!do_exit && hackrf_is_streaming(device) == HACKRF_TRUE) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        std::vector<Passing> passings = passing_detector.identify_passings(now - 250ul);
        for (const auto& passing : passings) {
            // std::cout << passing.transponder_id << " " << passing.timestamp << " " << passing.rssi << " " << passing.hits << std::endl;
            const std::string report = std::format("{} {} {:.2f} {}", passing.timestamp, passing.transponder_id, passing.rssi, passing.hits);

            std::cout << report << std::endl;
            publisher.send(zmq::buffer(report), zmq::send_flags::none);
        }
        // std::cout << "." << std::flush;
    }

    // stop RX
    result = hackrf_stop_rx(device);
    if (result != HACKRF_SUCCESS) {
        std::fprintf(stderr, "hackrf_stop_rx() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
    }

cleanup:
    std::cout << "cleanup\n";
    if (device != nullptr) {
        result = hackrf_close(device);
        if (result != HACKRF_SUCCESS) {
            std::fprintf(stderr, "hackrf_close() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        }
    }
    hackrf_exit();

    std::cerr << "Done.\n";
    return 0;
}