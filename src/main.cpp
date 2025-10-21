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
#include <vector>

#include <zmq.hpp>
#include <zmq_addon.hpp>

#include <libhackrf/hackrf.h>
#include <liquid/liquid.h>

#include "framesync.hpp"
#include "passing.hpp"

#define MIN_FRAME_LEN 8*4


static hackrf_device* device = nullptr;
static std::atomic<bool> do_exit(false);

static const uint64_t CENTER_FREQ_HZ       = 5000000ULL;
static const uint32_t SAMPLE_RATE          = 5000000;
static const uint32_t SYMBOL_RATE          = 1250000;
static const uint32_t BB_FILTER_BW         = 1750000;
static const uint32_t SYMBOLS_PER_SAMPLE   = SAMPLE_RATE / SYMBOL_RATE;
static const uint8_t DEFAULT_LNA_GAIN      = 32;           // 0-40 in steps of 8 or so; experiment
static const uint8_t DEFAULT_VGA_GAIN      = 24;           // 0-62

static FrameParser frame_parser(2.0f, 12, 8);
static modemcf dpsk_modem = modemcf_create(LIQUID_MODEM_DPSK2);
static PassingDetector passing_detector;

// signal handler to break the capture loop
void signal_handler(int signum) {
    std::cerr << "\nCaught signal " << signum << " — stopping...\n";
    do_exit = true;
}

/**
 * BPSK symbols should be 180 degrees apart from each other.
 * The expression "z*z" means "rotate and scale z by z".
 * This transform should bring the z^2 to the same spot: (180+phi)*2 == phi*2.
 * By averaging, we can get a good estimate on symbol phase.
 */
float bpsk_avg_phase(const Frame *frame, int skip_head, int n) {
    int beg_idx = (skip_head < frame->len) ? skip_head : frame->len;
    int end_idx = ((skip_head + n) < frame->len) ? (skip_head + n) : frame->len;
    
    // guard clause if *frame is too short:
    if ((end_idx - beg_idx) <= 0) {
        return 0.0f;
    }

    std::complex<float> z2_sum = std::transform_reduce(
        frame->data + beg_idx, frame->data + end_idx,
        std::complex<float> {0.0f, 0.0f},
        std::plus<>(),
        [](const std::complex<float> &z) {
            return z * z;
        }
    );

    return std::arg(z2_sum) / 2.0f;
}

/**
 * BPSK demodulator based on sign(z.real)
 * Before decision, it rotates each symbol back to the real axis by -phase
 */
void bpsk_demod(Frame *frame, std::vector<uint8_t> *datastream) {
    // reset output
    datastream->clear();

    // do not decode very short frames (differential encodig would make no sense):
    if (frame->len < MIN_FRAME_LEN) {
        return;
    }
    
    // initialize DPSK, add first symbol (will be dropped)
    unsigned int symbol;
    modem_reset(dpsk_modem);
    modemcf_demodulate(dpsk_modem, frame->data[0], &symbol);
    // demodulate actual bytestream:
    uint8_t current_byte = 0;
    for (int i=1; i<frame->len; i++) {
        modemcf_demodulate(dpsk_modem, frame->data[i], &symbol); // this should track phase shifting
        current_byte = (current_byte << 1) | static_cast<uint8_t>(symbol);

        if (i % 8 == 0) {
            datastream->push_back(current_byte);
            current_byte = 0;
        }
    }
    // last byte:
    int remainder = frame->len % 8;
    datastream->push_back(current_byte << (8-remainder));

    // for (auto i = datastream->cbegin(); i != datastream->cend(); ++i) {
    //     std::cout << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(*i) << " ";
    // }
    // std::cout << std::dec << std::endl;

    return;
}

int preamble_position(const std::vector<uint8_t> bytestream, uint32_t preamble, int preamble_size) {
    if (bytestream.size() < 4) {
        return -1;
    }
    
    uint32_t seq = (static_cast<uint32_t>(bytestream[0]) << 24) |
           (static_cast<uint32_t>(bytestream[1]) << 16) |
           (static_cast<uint32_t>(bytestream[2]) << 8)  |
           (static_cast<uint32_t>(bytestream[3]));
    
    uint32_t mask = (1u << preamble_size) - 1u;

    for (int i=0; i<=(32-preamble_size); i++) {
        if ((seq & mask) == preamble) {
            // std::cout << i << " " << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << seq << std::endl << std::dec;
            return 32 - preamble_size - i;
        } else {
            preamble = preamble << 1;
            mask = mask << 1;
        }
    }
    return -1; // not found
}

uint32_t transponder_rc3(const uint8_t payload[]) {
    std::array<uint8_t, 24> transponder_bits = {};

    for (int i = 0; i < 8; ++i) {
        transponder_bits[24 - i * 3 - 1] = ((payload[i] & 0x20) >> 5) ^ ((payload[i] & 0x10) >> 4);
        transponder_bits[24 - i * 3 - 2] = ((payload[i] & 0x08) >> 3) ^ ((payload[i] & 0x04) >> 2);
        transponder_bits[24 - i * 3 - 3] = ((payload[i] & 0x02) >> 1) ^ ((payload[i] & 0x01) >> 0);
    }

    uint32_t result = 0;
    for (int i = 0; i < 24; ++i) {
        result |= static_cast<uint32_t>(transponder_bits[i]) << (23 - i);
    }

    if (result > 9999999) {
        return 0; // status messages are above 7 digits
    }
    return result;
}

uint32_t transponder_decode(const std::vector<uint8_t> bytestream) {
    // if (bytestream.size() < 10) { // preamble + 8 databytes
    //     return 0;
    // }

    int rc3_start = preamble_position(bytestream, 0x7916, 16);
    if (rc3_start >= 0) {
        rc3_start += 16;
        uint8_t buffer[32]; // Frame has 161 symbols max, bytestream max size is 20 
        memset(buffer, 0, 32);

        // copy aligned data to buffer:
        int cut_bytes = rc3_start / 8;
        int shift_bits = rc3_start - 8 * cut_bytes;
        if (shift_bits == 0) { // shortcut if no further bit-shifting is needed:
            for (int i=cut_bytes; i<bytestream.size(); i++) {
                buffer[i-cut_bytes] = bytestream[i];
            }
        } else {
            uint8_t l_mask = (1u << shift_bits) - 1u;
            uint8_t h_mask = ~l_mask;
            
            for (int i=cut_bytes; i<bytestream.size()-1; i++) {
                uint8_t h = (bytestream[i] << shift_bits) & h_mask;
                uint8_t l = (bytestream[i+1] >> (8 - shift_bits)) & l_mask;
                buffer[i-cut_bytes] = h | l;
            }
        }

        // rc3 decoding
        return transponder_rc3(buffer);
    }
    return 0;
}

// hackrf callback invoked for each block of data
extern "C" int rx_callback(hackrf_transfer* transfer) {
    if (do_exit) {
        return 0;
    }

    uint64_t buffer_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    std::size_t sample_count = transfer->valid_length / 2;
    const std::complex<int8_t> *iq_samples = reinterpret_cast<const std::complex<int8_t>*>(transfer->buffer);
    
    // allocate buffer for received bytestream
    std::vector<uint8_t> demodulated_bytestream;
    demodulated_bytestream.reserve(64);

    // identify frames
    int start_idx = 0;
    while (std::unique_ptr<Frame> frame = frame_parser.next_frame(iq_samples, sample_count, start_idx)) {
        float rssi = frame->rssi();
        uint64_t timestamp = buffer_timestamp + (1000 * start_idx) / SAMPLE_RATE; // END of the frame
        // collect frame statistics:
        // TODO frame_statistics.report(timestamp, frame->len, rssi);
        
        // makes no sense to allocate further cpu for short messages
        if (frame->len < MIN_FRAME_LEN) {
            continue;
        }

        // demodulate the BPSK signal:
        bpsk_demod(frame.get(), &demodulated_bytestream);
        uint32_t transponder_id = transponder_decode(demodulated_bytestream);
        if (transponder_id != 0) { // we have a transponder!!!
            passing_detector.append(transponder_id, timestamp, rssi);
        }
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
            std::chrono::system_clock::now().time_since_epoch()
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