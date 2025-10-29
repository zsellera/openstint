#include <cstdint>

#include <algorithm> 
#include <vector>

#include "passing.hpp"

#define REPORT_HIT_LIMIT 2

void PassingDetector::append(int32_t transponder_id, 
                             uint64_t timestamp, 
                             float rssi)
{
    Detection d(timestamp, rssi);
    std::lock_guard<std::mutex> lock(mutex);
    detections[transponder_id].push_back(std::move(d));
}

uint64_t timestamp_at_max_rssi(const std::vector<Detection>& detections) {
    const auto it = std::max_element(
        detections.begin(),
        detections.end(),
        [](const Detection& a, const Detection& b) {
            return a.rssi < b.rssi;
        });

    return it->timestamp;
}

float median_rssi(const std::vector<Detection>& detections) {
    std::vector<float> rssi_values;
    rssi_values.reserve(detections.size());
    for (const auto& d : detections)
        rssi_values.push_back(d.rssi);
    
    const size_t mid = rssi_values.size() / 2;
    std::nth_element(rssi_values.begin(), rssi_values.begin() + mid, rssi_values.end());
    return rssi_values[mid];
}

Passing create_passing(uint32_t transponder_id, const std::vector<Detection>& detections) {
    Passing p {
        .transponder_id = transponder_id,
        .timestamp = timestamp_at_max_rssi(detections),
        .rssi = median_rssi(detections),
        .hits = detections.size()
    };
    return p;
}

std::vector<Passing> PassingDetector::identify_passings(int64_t deadline) {
    std::lock_guard<std::mutex> lock(mutex);

    // collect passings:
    std::vector<Passing> passings;
    std::vector<uint32_t> erasable_entries;
    for (const auto& [transpoder_id, detection_vec] : detections) {
        if (!detection_vec.empty() && detection_vec.back().timestamp <= deadline) {
            Passing p = create_passing(transpoder_id, detection_vec);
            erasable_entries.push_back(transpoder_id);
            if (p.hits >= REPORT_HIT_LIMIT) {
                passings.push_back(std::move(p));
            }
        }
    }

    // delete old or invalid detections:
    for (uint32_t key : erasable_entries) {
        detections.erase(key);
    }

    return passings;
}