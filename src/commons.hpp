#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>

#include "frame.hpp"

#ifndef SAMPLES_PER_SYMBOL
#define SAMPLES_PER_SYMBOL 4
#endif

#define DEFAULT_ZEROMQ_PORT 5556

static const uint32_t SYMBOL_RATE = 1250000;
static const uint32_t SAMPLE_RATE = SYMBOL_RATE * SAMPLES_PER_SYMBOL;

void detect_frames(const std::complex<int8_t>* samples, std::size_t sample_count);
bool parse_common_arguments(int& i, const int argc, const std::string& arg, char** argv);
void init_commons();
void report_detections();
