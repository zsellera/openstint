#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "frame.hpp"

struct Passing;

#define DEFAULT_ZEROMQ_PORT 5556

// microseconds of the steady clock since startup; the live timebase
uint64_t steady_now_us();
// rx_timestamp: when the buffer was received, microseconds since startup (steady_now_us() when live)
void detect_frames(const std::complex<int8_t>* samples, std::size_t sample_count, uint64_t rx_timestamp);
bool parse_common_arguments(int& i, const int argc, const std::string& arg, char** argv);
void init_commons();
// now_ts: current time on the same timebase as detect_frames()'s rx_timestamp
void report_detections(uint64_t now_ts);

// what an "S" message carries
struct StatusReport {
    uint64_t timestamp;         // ms, the reporting timebase of the ZeroMQ messages
    float noise_floor;          // dBFS, -infinity until the first estimate
    float dc_offset;            // magnitude, ADC counts
    uint32_t frames_received;   // during the last reporting period (~1 s)
    uint32_t frames_processed;
    double jitter_p95_ms;       // buffer arrival jitter
};

// what an "L" message carries
struct Rc4LearningReport {
    enum class Event { Start, Interrupted, Done, Reset };

    uint64_t timestamp;         // ms, the reporting timebase of the ZeroMQ messages
    Event event;
    float rssi = 0.0f;              // Start only: RSSI of the transponder being learned, dBFS
    uint32_t transponder_id = 0;    // Done only: id the payloads were stored under
    size_t payload_count = 0;       // Done only: number of payloads stored
};

// Optional in-process consumer of decoder output (e.g. a GUI). Each callback is
// called synchronously from report_detections(), after the matching ZeroMQ
// message was published; unset callbacks are skipped.
struct DecoderListener {
    std::function<void(const Passing&)> on_passing;
    std::function<void(const StatusReport&)> on_status;
    std::function<void(const Rc4LearningReport&)> on_rc4_learning;
};

void set_decoder_listener(DecoderListener listener);
