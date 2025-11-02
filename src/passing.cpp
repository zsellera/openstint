#include <cstdint>

#include <algorithm> 
#include <vector>

#include "passing.hpp"

#define REPORT_HIT_LIMIT 2
#define TRANSPONDER_DETECTION_MSG_LIMIT (1<<12)

void PassingDetector::append(uint64_t timestamp,
                             int32_t transponder_id, 
                             float rssi)
{
    Detection d(timestamp, rssi);
    std::lock_guard<std::mutex> lock(mutex);

    // this is an optimalization / guard: if a car parks on the loop,
    // we could fill up the memory and things go get crazy. A transponder
    // sends on avg. ~700 messages a second. A sane limit must apply.
    if (detections[transponder_id].size() > TRANSPONDER_DETECTION_MSG_LIMIT) {
        detections[transponder_id].back() = d;
    } else {
        detections[transponder_id].push_back(std::move(d));
    }
}

void PassingDetector::timesync(uint64_t timestamp, uint32_t transponder_timestamp) {
    TimeSyncMsg ts(timestamp, transponder_timestamp);
    std::lock_guard<std::mutex> lock(mutex);
    timesync_messages.push_back(std::move(ts));
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

std::vector<Passing> PassingDetector::identify_passings(uint64_t deadline) {
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

std::vector<TimeSync> PassingDetector::identify_timesyncs(uint64_t margin) {
    std::vector<TimeSync> timesyncs;

    std::lock_guard<std::mutex> lock(mutex);
    // verify all timesync messages can belong only to a single transponder
    for (const auto& ts_msg : timesync_messages) {
        uint32_t matching_transponder = 0;
        int matching_transponder_count = 0;

        for (const auto& [transpoder_id, detection_vec] : detections) {
            if (detection_vec.empty()) { continue; }
            bool front_ok = (detection_vec.front().timestamp - margin) < ts_msg.timestamp;
            bool back_ok = (detection_vec.back().timestamp + margin) > ts_msg.timestamp;
            if  (front_ok && back_ok) {
                ++matching_transponder_count;
                matching_transponder = transpoder_id;
            }
        }
        if (matching_transponder_count == 1) {
            TimeSync ts = {
                .timestamp = ts_msg.timestamp,
                .transponder_id = matching_transponder,
                .transponder_timestamp = ts_msg.transponder_timestamp
            };
            timesyncs.push_back(std::move(ts));
        }
    }

    // erase all processed timesync messages
    timesync_messages.clear();

    return timesyncs;
}
