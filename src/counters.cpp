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

    // there is a minor trickery here: noise power is calculated from sample variance (sigma-squared),
    // while ADC_FULL_SCALE represents a voltage. As such,
    // rssi = 10*log(Psig/Pmax)
    //      = 10*log(Psig) - 10*log(Pmax)
    //      = 10*log(Psig) - 20*log(Vmax)
    float noise_floor = 10.0f * std::log10(noise_power) - 20.0 * std::log10(ADC_FULL_SCALE);
    
    std::string temp;
    std::format_to(
        std::back_inserter(temp), "{} {} {} {}", 
        noise_floor, 
        std::abs(dc_offset), 
        frames_received,
        frames_processed
    );
    return temp;
}