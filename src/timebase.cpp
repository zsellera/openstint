#include "timebase.hpp"
#include "commons.hpp"
#include <iostream>

using namespace std::chrono;

void Timebase::use_system_clock() { mode_sysclk = true; }

uint64_t Timebase::get_timecode() {
    std::lock_guard<std::mutex> lock(mutex);
    return sample_counter;
}

int32_t Timebase::advance_clock(uint64_t sample_count)
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto actual_ts = steady_clock::now();
    const microseconds buffer_duration(sample_count * 1000000 / SAMPLE_RATE);
    last_sample_ts = actual_ts;

    if (sample_counter == 0) { // first read
        first_sample_ts = actual_ts;
        wallclk_duration += buffer_duration;
        sample_counter += sample_count;
    } else { // regular buffer read
        wallclk_duration += buffer_duration;
        sample_counter += sample_count;

        const auto expected_ts = first_sample_ts + wallclk_duration;
        const int64_t total_error_us = duration_cast<microseconds>(actual_ts - expected_ts).count();
        if (total_error_us > USB_QUEUE_DEPTH * buffer_duration.count()) {
            // buffer underrun, adjust timing
            const auto skipped_buffers = total_error_us / buffer_duration.count() - USB_QUEUE_DEPTH + 1;
            sample_counter += skipped_buffers * sample_count;
            wallclk_duration += skipped_buffers * buffer_duration;
            return static_cast<int32_t>(skipped_buffers);
        }
    }
    return 0;
}

steady_clock::time_point Timebase::to_steady(uint64_t timecode)
{
    const microseconds tdelta(timecode * 1000000 / SAMPLE_RATE);
    return first_sample_ts + tdelta;
}

uint64_t Timebase::to_timestamp(uint64_t timecode)
{
    if (mode_sysclk) {
        const auto steady_now = steady_clock::now();
        const auto steady_sample = to_steady(timecode);
        const auto delta = duration_cast<milliseconds>(steady_now - steady_sample);
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() - delta.count();
    } else {
        return (timecode * 1000 / SAMPLE_RATE);
    }
}

int64_t Timebase::from_millis(int64_t ms) {
    return ms * SAMPLE_RATE / 1000;
}

microseconds Timebase::get_error() {
    microseconds d = duration_cast<microseconds>(last_sample_ts - first_sample_ts);
    return wallclk_duration - d;
}