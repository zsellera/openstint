#pragma once

#include <cstdlib>
#include <stdbool.h>

#include <array>
#include <complex>
#include <limits>
#include <mutex>
#include <tuple>
#include <cmath>

#include "frame.hpp"

// what a reporting period measured: noise floor (dBFS), dc offset magnitude
// (ADC counts), frames received, frames processed. The noise floor is -infinity
// until the first estimate lands.
using RxSnapshot = std::tuple<float, float, uint32_t, uint32_t>;

class RxStatistics {
    uint32_t frames_received = 0;
    uint32_t frames_processed = 0;
    std::complex<float> dc_offset = {0, 0};
    float noise_power = 0;
    uint64_t last_reset_timestamp = 0;

    std::mutex mutex;

public:
    void register_frame(bool processed);
    void save_channel_characteristics(std::complex<float> dc_offset, float noise_power);

    bool reporting_due(uint64_t current_timestamp);
    RxSnapshot snapshot_and_reset(uint64_t current_timestamp);
};


// P95 of |actual - expected| buffer arrival interval over the last ~5 s
class TimestampJitter {
public:
    void sample(uint64_t host_us, uint64_t sample_count);
    double p95_ms();

private:
    static constexpr uint64_t WINDOW_US = 5000000ull;
    static constexpr std::size_t CAPACITY = 1024;

    struct Entry {
        uint64_t host_us;
        uint32_t error_us; // |actual - expected| arrival interval
    };

    std::array<Entry, CAPACITY> entries{};
    uint64_t head = 0;

    bool started = false;
    uint64_t prev_host_us = 0;

    std::mutex mutex;
};
