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
#include "counters.hpp"
#include "timebase.hpp"


static hackrf_device* device = nullptr;
static std::atomic<bool> do_exit(false);

static const uint64_t CENTER_FREQ_HZ       = 5000000ULL;
static const uint32_t SAMPLE_RATE          = 5000000;
static const uint32_t SYMBOL_RATE          = 1250000;
static const uint32_t BB_FILTER_BW         = 1750000;
static const uint32_t SAMPLES_PER_SYMBOL   = SAMPLE_RATE / SYMBOL_RATE;
static const uint8_t DEFAULT_LNA_GAIN      = 24;           // 0-40 in steps of 8 or so; experiment
static const uint8_t DEFAULT_VGA_GAIN      = 24;           // 0-62
static const int DEFAULT_ZEROMQ_PORT       = 5556;

static enum FrameParseMode { FRAME_SEEK, FRAME_FOUND } frame_parse_mode = FRAME_SEEK;
static FrameDetector frame_detector(0.9f);
static SymbolReader symbol_reader;
static Frame frame;

static PassingDetector passing_detector;
static RxStatistics rx_stats;
static bool monitor_mode = false;

static Timebase timebase;

// signal handler to break the capture loop
void signal_handler(int signum) {
    std::cerr << "\nCaught signal " << signum << " — stopping...\n";
    do_exit = true;
}

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

// hackrf callback invoked for each block of data
extern "C" int rx_callback(hackrf_transfer* transfer) {
    if (do_exit) {
        return 0;
    }

    uint64_t buffer_timestamp = timebase.now();
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
                frame = Frame(detected.value(), timestamp, frame_detector.symbol_energy());
                symbol_reader.read_preamble(&frame, frame_detector.dc_offset(), samples, idx+4);
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

    // Returning 0 indicates "keep going".
    return 0;
}    

int main(int argc, char** argv) {
    int result = HACKRF_SUCCESS;

    const uint64_t freq_hz = CENTER_FREQ_HZ;
    const uint32_t sample_rate = SAMPLE_RATE;
    const uint32_t filter_bw = BB_FILTER_BW;
    uint8_t lna_gain = DEFAULT_LNA_GAIN;
    uint8_t vga_gain = DEFAULT_VGA_GAIN;
    bool bias_tee = false;
    bool amp_enable = false; // hackrf has a custom, +13 dB preamp
    int zmq_port = DEFAULT_ZEROMQ_PORT;
    const char* hackrf_serial = nullptr;

    // process command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-d" && i + 1 < argc) {
            hackrf_serial = argv[++i];
        } else if (arg == "-l" && i + 1 < argc) {
            lna_gain = std::atoi(argv[++i]);
            lna_gain = (lna_gain / 8) * 8; // steps of 8
            if (lna_gain > 40) {
                std::cerr << "Error: LNA gain must be between 0 and 40.\n";
                return 1;
            }
        } else if (arg == "-v" && i + 1 < argc) {
            vga_gain = std::atoi(argv[++i]);
            vga_gain = (vga_gain / 2) * 2; // steps of 2
            if (vga_gain > 62) {
                std::cerr << "Error: VGA gain must be between 0 and 62.\n";
                return 1;
            }
        } else if (arg == "-b") {
            bias_tee = true;
        } else if (arg == "-a") {
            amp_enable = true;
        } else if (arg == "-p" && i + 1 < argc) {
            zmq_port = std::atoi(argv[++i]);
        } else if (arg == "-m") {
            monitor_mode = true;
        } else if (arg == "-t") {
            timebase.use_system_clock();
        } else {
            if (arg != "-h") {
                std::cerr << "Unknown argument: " << arg << "\n";
            }
            std::cerr << "Usage: " << argv[0] << " [-d ser_nr] [-p tcp_port] [-l <0..40>] [-v <0..62>] [-a] [-b] [-m] [-t]\n";
            std::cerr << "\t-d ser_nr   default:first\tserial number of the desired HackRF\n";
            std::cerr << "\t-p port     default:" << DEFAULT_ZEROMQ_PORT << "\tZeroMQ publisher port\n";
            std::cerr << "\t-l <0..40>  default:" << static_cast<int>(DEFAULT_LNA_GAIN) << "  \tLNA gain (rf signal amplifier; valid values: 0/8/16/24/32/40)\n";
            std::cerr << "\t-v <0..62>  default:" << static_cast<int>(DEFAULT_LNA_GAIN) << "  \tVGA gain (baseband signal amplifier, steps of 2)\n";
            std::cerr << "\t-a          default:off \tEnable preamp (+13 dB to input RF signal)\n";
            std::cerr << "\t-b          default:off \tEnable bias-tee (+3.3 V, 50 mA max)\n";
            std::cerr << "\t-m          default:off \tEnable monitor mode (print received frames to stdout)\n";
            std::cerr << "\t-t          default:off \tUse system clock as the timebase (beware of NTP jumps)\n";
            
            return 1;
        }
    }

    // transponder processing (allocate viterbi trellis); TODO RAII
    init_transponders();

    //  Prepare our context and publisher
    std::string zmq_address;
    std::format_to(std::back_inserter(zmq_address), "tcp://*:{}", zmq_port);
    zmq::context_t context(1);
    zmq::socket_t publisher(context, zmq::socket_type::pub);
    publisher.bind(zmq_address);
    std::cout << "Listening on " << zmq_address << std::endl;

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
    result = hackrf_open_by_serial(hackrf_serial, &device);
    if (result != HACKRF_SUCCESS || device == nullptr) {
        std::fprintf(stderr, "hackrf_open() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        hackrf_exit();
        return EXIT_FAILURE;
    }

    read_partid_serialno_t serno;
    result = hackrf_board_partid_serialno_read(device, &serno);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_board_partid_serialno_read() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
    } else {
        printf("HackRF SerNo.: %08x%08x%08x%08x\n", serno.serial_no[0], serno.serial_no[1], serno.serial_no[2], serno.serial_no[3]);
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

    // set LNA gain
    result = hackrf_set_lna_gain(device, lna_gain);
    if (result != HACKRF_SUCCESS) {
        std::fprintf(stderr, "hackrf_set_lna_gain() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
    }

    // set VGA gain
    result = hackrf_set_vga_gain(device, vga_gain);
    if (result != HACKRF_SUCCESS) {
        std::fprintf(stderr, "hackrf_set_vga_gain() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
    }

    // (Optional) enable amplified antenna
    hackrf_set_amp_enable(device, amp_enable ? 1 : 0);
    if (result != HACKRF_SUCCESS) {
		std::fprintf(stderr, "hackrf_set_amp_enable() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
    }

    // (Optional) enable bias-tee
    result = hackrf_set_antenna_enable(device, bias_tee ? 1 : 0);
    if (result != HACKRF_SUCCESS) {
		std::fprintf(stderr, "hackrf_set_antenna_enable() failed: %s (%d)\n", hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
    }

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
        
        const auto now = timebase.now();

        // report status once a second
        if (rx_stats.reporting_due(now)) {
            const std::string report = std::format("S {} {}", now, rx_stats.to_string());
            rx_stats.reset(now);

            std::cout << report << std::endl;
            publisher.send(zmq::buffer(report), zmq::send_flags::none);
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
            publisher.send(zmq::buffer(report), zmq::send_flags::none);
        }

        std::vector<Passing> passings = passing_detector.identify_passings(now - 250ul);
        for (const auto& passing : passings) {
            const std::string report = std::format("P {} {} {} {:.2f} {} {}",
                passing.timestamp,
                transponder_props(passing.transponder_type).prefix,
                passing.transponder_id,
                passing.rssi,
                passing.hits,
                passing.duration
            );

            std::cout << report << std::endl;
            publisher.send(zmq::buffer(report), zmq::send_flags::none);
        }
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