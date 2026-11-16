#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>

#include "frame.hpp"

#define DEFAULT_ZEROMQ_PORT 5556

// microseconds of the steady clock since startup; the live timebase
uint64_t steady_now_us();
// rx_timestamp: when the buffer was received, microseconds since startup (steady_now_us() when live)
void detect_frames(const std::complex<int8_t>* samples, std::size_t sample_count, uint64_t rx_timestamp);
bool parse_common_arguments(int& i, const int argc, const std::string& arg, char** argv);
void init_commons();
// now_ts: current time on the same timebase as detect_frames()'s rx_timestamp
void report_detections(uint64_t now_ts);
