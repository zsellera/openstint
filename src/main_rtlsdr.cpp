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
#include "capture.hpp"

#include <rtl-sdr.h>


static rtlsdr_dev_t* device = nullptr;
static std::atomic<bool> do_exit(false);
static std::atomic<bool> streaming(false);
static std::atomic<int64_t> last_rx_ms(0);  // lost radio detection

static const uint64_t CENTER_FREQ_HZ       = 5000000ULL;
static const int DEFAULT_GAIN_TENTHS_DB    = 200;           // dB

// number of raw bytes (2 per IQ sample) read per chunk, matching the RTL-SDR read buffer
static const size_t CHUNK_BYTES = 2*16384;

// Conversion buffer: RTL-SDR provides uint8_t, commons.cpp expects int8_t
static std::vector<std::complex<int8_t>> conversion_buffer(CHUNK_BYTES / 2);

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

    // lost/frozen radio detection:
    last_rx_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

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

    const uint32_t sample_rate = SAMPLE_RATE;
    uint64_t freq_hz = CENTER_FREQ_HZ;
    int gain_tenths_db = DEFAULT_GAIN_TENTHS_DB;
    bool bias_tee = false;
    const char* serial = nullptr;
    std::vector<std::string> capture_files;

    // process command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-d" && i + 1 < argc) {
            serial = argv[++i];
        } else if (arg == "-g" && i + 1 < argc) {
            gain_tenths_db = std::atoi(argv[++i]) * 10;
        } else if (arg == "-b") {
            bias_tee = true;
        } else if (arg == "-c" && i + 1 < argc) {
            capture_files.push_back(argv[++i]);
        } else if (parse_common_arguments(i, argc, arg, argv)) {
            // do nothing
        } else {
            if (arg != "-h") {
                std::cerr << "Unknown argument: " << arg << "\n";
            }
            std::cerr << "Usage: " << argv[0] << " [-d ser_nr] [-g <gain_dB>] [-D] [-b] [-c file.iq] [-p tcp_port] [-s dir] [-m] [-t]\n";
            std::cerr << "\t-d ser_nr   default:first\tserial number of the desired RTL-SDR\n";
            std::cerr << "\t-g <0..40>  default:" << DEFAULT_GAIN_TENTHS_DB / 10 << "  \ttuner gain in dB\n";
            std::cerr << "\t-b          default:off \tEnable bias-tee (+4.5 V)\n";
            std::cerr << "\t-c file.iq  default:off \tReplay CU8 IQ capture (rtl_sdr) instead of using the radio\n";
            std::cerr << "\t-p port     default:" << DEFAULT_ZEROMQ_PORT << "\tZeroMQ publisher port\n";
            std::cerr << "\t-m          default:off \tEnable monitor mode (print received frames to stdout)\n";
            std::cerr << "\t-t          default:off \tUse system clock as the timebase (beware of NTP jumps)\n";
            std::cerr << "\t-s dir      default:.   \tRC4 registry storage directory\n";

            return 1;
        }
    }

    init_commons();

    // install signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // capture replay mode: skip radio init entirely and stream the file(s)
    // through the same rx_callback used for live samples.
    if (!capture_files.empty()) {
        std::cout << "RTL-SDR FILE RX: replaying " << capture_files.size()
                  << " capture file(s), sample_rate=" << sample_rate << " Hz\n";
        replay_capture(capture_files, sample_rate, rx_callback, nullptr, do_exit);
        std::cerr << "Done.\n";
        return 0;
    }

    std::cout << "RTL-SDR RX: freq=" << freq_hz << " Hz, sample_rate=" << sample_rate
              << " Hz, gain=" << gain_tenths_db / 10 << " dB\n";

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

    bool is_v4 = false;
    char manufact[256] = {0}, product[256] = {0}, sn[256] = {0};
    // 1. Attempt to retrieve USB strings. 
    // Note: On Windows, calling rtlsdr_get_usb_strings(device, ...) after rtlsdr_open 
    // is more reliable than using the device index before opening.
    if (rtlsdr_get_usb_strings(device, manufact, product, sn) == 0) {
        is_v4 = (std::strstr(product, "V4") != nullptr);
        std::printf("RTL-SDR: %s (SN: %s)\n", product, sn);
    } else {
        // Fallback: If USB string retrieval fails, identify by device index name
        const char* name = rtlsdr_get_device_name(device_index);
        std::printf("RTL-SDR: %s\n", name);
    }

    // 2. Hardware-level verification via Tuner Type.
    // RTL-SDR Blog V4 uses the R828D tuner, whereas V3 typically uses R820T2.
    // This is the most robust detection method if USB descriptors are blocked by drivers.
    if (!is_v4) {
        enum rtlsdr_tuner tuner_type = rtlsdr_get_tuner_type(device);
        is_v4 = (tuner_type == RTLSDR_TUNER_R828D);
        if (is_v4) {
            std::printf("RTL-SDR Blog V4 detected via Tuner Type (R828D)\n");
        }
    }

    // RTL-SDR Blog V4 features an integrated HF upconverter (Frequency Upconverter/Mixer)
    // allowing native HF reception without direct sampling.
    if (is_v4) {
        std::printf("V4 Mode: Native HF reception enabled.\n");
    } else {
        // Older dongles (V3 and generic) require Direct Sampling Mode (Q-branch) for HF.
        std::fprintf(stderr, "Non-V4 dongle detected — enabling direct sampling (Q-branch)\n");
        result = rtlsdr_set_direct_sampling(device, 2);
        if (result != 0) {
            std::fprintf(stderr, "rtlsdr_set_direct_sampling() failed: %d\n", result);
            goto cleanup;
        }
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
    if (is_v4) {
        result = rtlsdr_set_tuner_gain(device, gain_tenths_db);
        if (result != 0) {
            std::fprintf(stderr, "rtlsdr_set_tuner_gain() failed: %d\n", result);
        } else {
            int actual = rtlsdr_get_tuner_gain(device);
            std::fprintf(stderr, "Tuner gain set to %.1f dB\n", actual / 10.0);
        }
    } else {
        std::fprintf(stderr, "Tuner gain not applicable in direct sampling mode\n");
    }

    // enable bias-tee if requested
    result = rtlsdr_set_bias_tee(device, bias_tee ? 1 : 0);
    if (result != 0) {
        std::fprintf(stderr, "Warning: Failed to set bias-tee (may not be supported)\n");
    }

    // reset buffer to clear stale data
    rtlsdr_reset_buffer(device);

    // start async reading in a background thread
    {
        streaming = true;
        last_rx_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        std::thread rx_thread([]() {
            int r = rtlsdr_read_async(device, rx_callback, nullptr, 12, CHUNK_BYTES);
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

            // watchdog: rtlsdr_read_async() may stall silently if the device
            // is unplugged, so bail out if no samples arrive for a while
            int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (now_ms - last_rx_ms > 2000) {
                std::fprintf(stderr, "No samples for 2s — device lost?\n");
                do_exit = true;
            }
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
        rtlsdr_set_bias_tee(device, 0);
        rtlsdr_close(device);
        device = nullptr;
    }

    std::cerr << "Done.\n";
    return 0;
}
