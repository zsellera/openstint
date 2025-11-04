#pragma once

#include <cstdlib>
#include <stdbool.h>

#include <complex>
#include <mutex>
#include <format>
#include <string_view>
#include <cmath>

#include "frame.hpp"

class RxStatistics {
    uint32_t frames_received = 0;
    uint32_t frames_processed = 0;
    std::complex<int8_t> dc_offset = {0, 0};
    float noise_power = 0;
    uint64_t last_reset_timestamp = 0;

    std::mutex mutex;

public:
    void register_frame(bool processed);
    void save_channel_characteristics(std::complex<int8_t> dc_offset, float noise_power);

    void reset(uint64_t current_timestamp);
    bool reporting_due(uint64_t current_timestamp);
    std::string to_string();
};

