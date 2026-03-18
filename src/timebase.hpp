#pragma once

#include <cstdint>
#include <chrono>
#include <mutex>

class Timebase
{
    static const long USB_QUEUE_DEPTH = 4u;

    bool mode_sysclk = false;

    uint64_t sample_counter = 0;
    std::chrono::microseconds wallclk_duration{0};
    std::chrono::steady_clock::time_point first_sample_ts;
    std::chrono::steady_clock::time_point last_sample_ts;
    std::mutex mutex;

public:
    Timebase() {}

    void use_system_clock();
    uint64_t get_timecode();
    int32_t advance_clock(uint64_t sample_count);
    std::chrono::steady_clock::time_point to_steady(uint64_t timecode);
    uint64_t to_timestamp(uint64_t timecode);
    int64_t from_millis(int64_t ms);
    std::chrono::microseconds get_error();
};
