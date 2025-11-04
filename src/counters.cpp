#include "counters.hpp"

#define REPORTING_PERIOD 5000


bool RxStatistics::reporting_due(uint64_t current_timestamp) {
    return current_timestamp >= last_reset_timestamp + REPORTING_PERIOD;
}

void RxStatistics::register_frame(bool processed) {
    std::lock_guard<std::mutex> lock(mutex);

    frames_received++;
    if (processed) { frames_processed++; }
}

void RxStatistics::save_channel_characteristics(std::complex<int8_t> _dc_offset, float _noise_power) {
    std::lock_guard<std::mutex> lock(mutex);

    dc_offset = _dc_offset;
    noise_power = _noise_power;
}

void RxStatistics::reset(uint64_t current_timestamp) {
    std::lock_guard<std::mutex> lock(mutex);

    frames_received = 0;
    frames_processed = 0; 
    last_reset_timestamp = current_timestamp;
}

std::string RxStatistics::to_string() {
    std::lock_guard<std::mutex> lock(mutex);

    std::string temp;
    float noise_floor = std::log2f(noise_power);
    std::format_to(
        std::back_inserter(temp), "{} {} {} {}", 
        noise_floor, 
        std::abs(dc_offset), 
        frames_received,
        frames_processed
    );
    return temp;
}