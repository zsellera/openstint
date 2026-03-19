#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>

#include "frame.hpp"

#define DEFAULT_ZEROMQ_PORT 5556

void detect_frames(const std::complex<int8_t>* samples, std::size_t sample_count);
bool parse_common_arguments(int& i, const int argc, const std::string& arg, char** argv);
void init_commons();
void report_detections();
