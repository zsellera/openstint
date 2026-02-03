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
#include "commons.hpp"

#include <zmq.hpp>
#include <zmq_addon.hpp>

#include <libhackrf/hackrf.h>
#include <liquid/liquid.h>


static hackrf_device* device = nullptr;
static std::atomic<bool> do_exit(false);

static const uint64_t CENTER_FREQ_HZ       = 5000000ULL;
static const uint32_t BB_FILTER_BW         = 1750000;
static const uint8_t DEFAULT_LNA_GAIN      = 24;           // 0-40 in steps of 8 or so; experiment
static const uint8_t DEFAULT_VGA_GAIN      = 24;           // 0-62

// signal handler to break the capture loop
void signal_handler(int signum) {
    std::cerr << "\nCaught signal " << signum << " — stopping...\n";
    do_exit = true;
}

// hackrf callback invoked for each block of data
extern "C" int rx_callback(hackrf_transfer* transfer) {
    if (do_exit) {
        return 0;
    }

    uint32_t sample_count = transfer->valid_length / 2;
    const std::complex<int8_t> *samples = reinterpret_cast<const std::complex<int8_t>*>(transfer->buffer);
    
    detect_frames(samples, sample_count);

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
        } else if (parse_common_arguments(i, argc, arg, argv)) {
            // do nothing
        } else {
            if (arg != "-h") {
                std::cerr << "Unknown argument: " << arg << "\n";
            }
            std::cerr << "Usage: " << argv[0] << " [-d ser_nr] [-l <0..40>] [-v <0..62>] [-a] [-b] [-p tcp_port] [-m] [-t]\n";
            std::cerr << "\t-d ser_nr   default:first\tserial number of the desired HackRF\n";
            std::cerr << "\t-l <0..40>  default:" << static_cast<int>(DEFAULT_LNA_GAIN) << "  \tLNA gain (rf signal amplifier; valid values: 0/8/16/24/32/40)\n";
            std::cerr << "\t-v <0..62>  default:" << static_cast<int>(DEFAULT_LNA_GAIN) << "  \tVGA gain (baseband signal amplifier, steps of 2)\n";
            std::cerr << "\t-a          default:off \tEnable preamp (+13 dB to input RF signal)\n";
            std::cerr << "\t-b          default:off \tEnable bias-tee (+3.3 V, 50 mA max)\n";
            std::cerr << "\t-p port     default:" << DEFAULT_ZEROMQ_PORT << "\tZeroMQ publisher port\n";
            std::cerr << "\t-m          default:off \tEnable monitor mode (print received frames to stdout)\n";
            std::cerr << "\t-t          default:off \tUse system clock as the timebase (beware of NTP jumps)\n";
            
            return 1;
        }
    }

    init_commons();

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
        
        report_detections();
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