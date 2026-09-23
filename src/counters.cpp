#include "counters.hpp"

#include <algorithm>
#include <vector>

#define REPORTING_PERIOD_US 1000000


bool RxStatistics::reporting_due(uint64_t current_timestamp) {
    return current_timestamp >= last_reset_timestamp + REPORTING_PERIOD_US;
}

void RxStatistics::register_frame(bool processed) {
    std::lock_guard<std::mutex> lock(mutex);

    frames_received++;
    if (processed) { frames_processed++; }
}

void RxStatistics::save_channel_characteristics(std::complex<float> _dc_offset, float _noise_power) {
    std::lock_guard<std::mutex> lock(mutex);

    dc_offset = _dc_offset;
    noise_power = _noise_power;
}

// Reading and resetting happen under a single lock: with two separate locked
// calls, a frame registered by the RX thread in between would be counted into
// neither the snapshot being taken nor the next one.
RxSnapshot RxStatistics::snapshot_and_reset(uint64_t current_timestamp) {
    std::lock_guard<std::mutex> lock(mutex);

    // there is a minor trickery here: noise power is calculated from sample variance (sigma-squared),
    // while ADC_FULL_SCALE represents a voltage. As such,
    // rssi = 10*log(Psig/Pmax)
    //      = 10*log(Psig) - 10*log(Pmax)
    //      = 10*log(Psig) - 20*log(Vmax)
    // Frame::rssi() reports on this very scale, so a difference of the two is an SNR in dB.
    const float noise_floor = 10.0f * std::log10(noise_power) - 20.0 * std::log10(ADC_FULL_SCALE);

    const RxSnapshot snapshot = { noise_floor, std::abs(dc_offset), frames_received, frames_processed };

    frames_received = 0;
    frames_processed = 0;
    last_reset_timestamp = current_timestamp;

    return snapshot;
}

void TimestampJitter::sample(uint64_t host_us, uint64_t sample_count) {
    std::lock_guard<std::mutex> lock(mutex);

    if (started) {
        // how far this buffer's arrival interval landed from the interval its
        // own sample count represents
        const double actual_us = static_cast<double>(host_us - prev_host_us);
        const double expected_us = static_cast<double>(sample_count)
            * 1000000.0 / static_cast<double>(SAMPLE_RATE);
        const double error_us = std::fabs(actual_us - expected_us);

        entries[head % CAPACITY] = {
            host_us,
            static_cast<uint32_t>(std::llround(std::fmin(error_us, 4000000000.0)))
        };
        head++;
    }

    started = true;
    prev_host_us = host_us;
}

double TimestampJitter::p95_ms() {
    std::vector<uint32_t> recent;

    {
        std::lock_guard<std::mutex> lock(mutex);

        const uint64_t oldest = (head > CAPACITY) ? (head - CAPACITY) : 0;
        const uint64_t newest_us = (head > 0) ? entries[(head - 1) % CAPACITY].host_us : 0;

        recent.reserve(static_cast<std::size_t>(head - oldest));
        for (uint64_t i = oldest; i < head; ++i) {
            const Entry& e = entries[i % CAPACITY];
            if ((newest_us - e.host_us) <= WINDOW_US) {
                recent.push_back(e.error_us);
            }
        }
    }

    // percentile taken outside the lock: the RX callback should never wait on it
    if (recent.size() < 20) {
        return 0.0;
    }

    const std::size_t k = (recent.size() * 95) / 100;
    std::nth_element(recent.begin(), recent.begin() + k, recent.end());
    return static_cast<double>(recent[k]) / 1000.0;
}
