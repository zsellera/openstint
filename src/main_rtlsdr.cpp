#include <atomic>
#include <chrono>
#include <complex>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "commons.hpp"

#include <rtl-sdr.h>


static rtlsdr_dev_t* device = nullptr;
static std::atomic<bool> do_exit(false);
static std::atomic<bool> streaming(false);

static const uint64_t CENTER_FREQ_HZ       = 5000000ULL;
static const int DEFAULT_GAIN_TENTHS_DB    = 200;           // dB

// Conversion buffer: RTL-SDR provides uint8_t, commons.cpp expects int8_t
static std::vector<std::complex<int8_t>> conversion_buffer;

// signal handler to break the capture loop
void signal_handler(int signum) {
    std::cerr << "\nCaught signal " << signum << " — stopping...\n";
    do_exit = true;
    if (device) {
        rtlsdr_cancel_async(device);
    }
}

// rtlsdr callback invoked for each block of data
void rx_callback(unsigned char* buf, uint32_t len, void* /*ctx*/) {
    if (do_exit) {
        return;
    }

    uint32_t sample_count = len / 2;

    if (conversion_buffer.size() < sample_count) {
        conversion_buffer.resize(sample_count);
    }

    // RTL-SDR provides unsigned uint8_t samples (0-255, DC at 128).
    // Convert to signed int8_t (-128 to 127, DC at 0) for commons.cpp.
    for (uint32_t i = 0; i < sample_count; ++i) {
        conversion_buffer[i] = std::complex<int8_t>(
            static_cast<int8_t>(buf[2 * i]     - 128),
            static_cast<int8_t>(buf[2 * i + 1] - 128)
        );
    }

    detect_frames(conversion_buffer.data(), sample_count);
}

int main(int argc, char** argv) {
    int result;

    const uint64_t freq_hz = CENTER_FREQ_HZ;
    const uint32_t sample_rate = SAMPLE_RATE;
    int gain_tenths_db = DEFAULT_GAIN_TENTHS_DB;
    bool bias_tee = false;
    const char* serial = nullptr;

    // process command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-d" && i + 1 < argc) {
            serial = argv[++i];
        } else if (arg == "-g" && i + 1 < argc) {
            gain_tenths_db = std::atoi(argv[++i]) * 10;
        } else if (arg == "-b") {
            bias_tee = true;
        } else if (parse_common_arguments(i, argc, arg, argv)) {
            // do nothing
        } else {
            if (arg != "-h") {
                std::cerr << "Unknown argument: " << arg << "\n";
            }
            std::cerr << "Usage: " << argv[0] << " [-d ser_nr] [-g <gain_dB>] [-D] [-b] [-p tcp_port] [-m] [-t]\n";
            std::cerr << "\t-d ser_nr   default:first\tserial number of the desired RTL-SDR\n";
            std::cerr << "\t-g <dB>     default:" << DEFAULT_GAIN_TENTHS_DB / 10 << "  \ttuner gain in dB\n";
            std::cerr << "\t-b          default:off \tEnable bias-tee (+4.5 V)\n";
            std::cerr << "\t-p port     default:" << DEFAULT_ZEROMQ_PORT << "\tZeroMQ publisher port\n";
            std::cerr << "\t-m          default:off \tEnable monitor mode (print received frames to stdout)\n";
            std::cerr << "\t-t          default:off \tUse system clock as the timebase (beware of NTP jumps)\n";

            return 1;
        }
    }

    init_commons();

    std::cout << "RTL-SDR RX: freq=" << freq_hz << " Hz, sample_rate=" << sample_rate
              << " Hz, gain=" << gain_tenths_db / 10 << " dB\n";

    // install signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // find device
    int device_count = rtlsdr_get_device_count();
    if (device_count == 0) {
        std::cerr << "No RTL-SDR devices found.\n";
        return EXIT_FAILURE;
    }

    int device_index = 0;
    if (serial != nullptr) {
        device_index = rtlsdr_get_index_by_serial(serial);
        if (device_index < 0) {
            std::fprintf(stderr, "RTL-SDR with serial '%s' not found.\n", serial);
            return EXIT_FAILURE;
        }
    }

    // open device
    result = rtlsdr_open(&device, device_index);
    if (result != 0 || device == nullptr) {
        std::fprintf(stderr, "rtlsdr_open() failed: %d\n", result);
        return EXIT_FAILURE;
    }

    // print device info
    const char* name = rtlsdr_get_device_name(device_index);
    char manufact[256], product[256], sn[256];
    if (rtlsdr_get_device_usb_strings(device_index, manufact, product, sn) == 0) {
        std::printf("RTL-SDR: %s (SN: %s)\n", name, sn);
    } else {
        std::printf("RTL-SDR: %s\n", name);
    }

    // set center frequency
    result = rtlsdr_set_center_freq(device, freq_hz);
    if (result != 0) {
        std::fprintf(stderr, "rtlsdr_set_center_freq() failed: %d\n", result);
        goto cleanup;
    }

    // set sample rate (2.5 MSPS with SAMPLES_PER_SYMBOL=2)
    result = rtlsdr_set_sample_rate(device, sample_rate);
    if (result != 0) {
        std::fprintf(stderr, "rtlsdr_set_sample_rate() failed: %d\n", result);
        goto cleanup;
    }

    // set IF filter bandwidth to 2.0 MHz - to be refined
    result = rtlsdr_set_tuner_bandwidth(device, 2000000);
    if (result != 0) {
        std::fprintf(stderr, "rtlsdr_set_tuner_bandwidth() failed: %d\n", result);
    }

    // set manual gain mode and tuner gain
    rtlsdr_set_tuner_gain_mode(device, 1);
    result = rtlsdr_set_tuner_gain(device, gain_tenths_db);
    if (result != 0) {
        std::fprintf(stderr, "rtlsdr_set_tuner_gain() failed: %d\n", result);
    } else {
        int actual = rtlsdr_get_tuner_gain(device);
        std::fprintf(stderr, "Tuner gain set to %.1f dB\n", actual / 10.0);
    }

    // enable bias-tee if requested
    if (bias_tee) {
        result = rtlsdr_set_bias_tee(device, 1);
        if (result != 0) {
            std::fprintf(stderr, "Warning: Failed to enable bias-tee (may not be supported)\n");
        }
    }

    // reset buffer to clear stale data
    rtlsdr_reset_buffer(device);

    // start async reading in a background thread
    {
        streaming = true;
        std::thread rx_thread([]() {
            int r = rtlsdr_read_async(device, rx_callback, nullptr, 12, 32768);
            if (r != 0) {
                std::fprintf(stderr, "rtlsdr_read_async() failed: %d\n", r);
            }
            streaming = false;
        });
        std::cerr << "Streaming... stop with Ctrl-C\n";

        // main loop — exit when handler sets do_exit or device stops streaming
        while (!do_exit && streaming) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            report_detections();
        }

        // ensure async reading is cancelled
        rtlsdr_cancel_async(device);

        if (rx_thread.joinable()) {
            rx_thread.join();
        }
    }

cleanup:
    std::cout << "cleanup\n";
    if (device != nullptr) {
        rtlsdr_close(device);
        device = nullptr;
    }

    std::cerr << "Done.\n";
    return 0;
}
