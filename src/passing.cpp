#include <cstdint>

#include <algorithm> 
#include <vector>

#include "passing.hpp"

#define REPORT_HIT_LIMIT 2
#define TRANSPONDER_DETECTION_MSG_LIMIT (1<<12)

void PassingDetector::append(const Frame* frame, uint32_t transponder_id) {
    TransponderKey transponder_key = std::make_pair(frame->transponder_type, transponder_id);
    Detection d(frame->timestamp, frame->rssi(), frame->evm());
    
    std::lock_guard<std::mutex> lock(mutex);
    detections[transponder_key].push_back(std::move(d));
    if (detections[transponder_key].size() > TRANSPONDER_DETECTION_MSG_LIMIT) {
        // this is an optimalization / guard: if a car parks on the loop,
        // we could fill up the memory and things go get crazy. A transponder
        // sends on avg. ~700 messages a second. A sane limit must apply.
        detections[transponder_key].pop_front();
    }
}

void PassingDetector::timesync(const Frame* frame, uint32_t transponder_timestamp) {
    TimeSyncMsg ts(frame->timestamp, transponder_timestamp);
    std::lock_guard<std::mutex> lock(mutex);
    timesync_messages.push_back(std::move(ts));
}

struct PassingPoint {
    uint64_t weighted_timestamp;
    float max_rssi;
};

PassingPoint compute_passing_point(const std::deque<Detection>& detections) {
    // Find the maximum RSSI
    const auto max_it = std::max_element(
        detections.begin(),
        detections.end(),
        [](const Detection& a, const Detection& b) {
            return a.rssi < b.rssi;
        });

    float max_rssi = max_it->rssi;
    float rssi_threshold = max_rssi - 6.0f;

    // Calculate RSSI-weighted average timestamp for detections within 6 dB of max
    float weighted_sum = 0.0f;
    float weight_total = 0.0f;

    for (const auto& d : detections) {
        if (d.rssi >= rssi_threshold) {
            weighted_sum += static_cast<float>(d.timestamp) * d.rssi;
            weight_total += d.rssi;
        }
    }

    uint64_t weighted_timestamp = static_cast<uint64_t>(weighted_sum / weight_total);
    return {weighted_timestamp, max_rssi};
}

Passing create_passing(TransponderKey transponder_key, const std::deque<Detection>& detections) {
    PassingPoint stats = compute_passing_point(detections);
    Passing p = {
        .timestamp = stats.weighted_timestamp,
        .transponder_type = transponder_key.first,
        .transponder_id = transponder_key.second,
        .rssi = stats.max_rssi,
        .hits = detections.size()
    };
    return p;
}

std::vector<Passing> PassingDetector::identify_passings(uint64_t deadline) {
    std::lock_guard<std::mutex> lock(mutex);

    // collect passings:
    std::vector<Passing> passings;
    std::vector<TransponderKey> erasable_entries;
    for (const auto& [transponder_key, detections] : detections) {
        if (!detections.empty() && detections.back().timestamp <= deadline) {
            Passing p = create_passing(transponder_key, detections);
            erasable_entries.push_back(transponder_key);
            if (p.hits >= REPORT_HIT_LIMIT) {
                passings.push_back(std::move(p));
            }
        }
    }

    // delete old or invalid detections:
    for (TransponderKey key : erasable_entries) {
        detections.erase(key);
    }

    return passings;
}

std::vector<TimeSync> PassingDetector::identify_timesyncs(uint64_t margin) {
    std::vector<TimeSync> timesyncs;

    std::lock_guard<std::mutex> lock(mutex);
    // verify all timesync messages can belong only to a single transponder
    for (const auto& ts_msg : timesync_messages) {
        TransponderKey matching_transponder;
        int matching_transponder_count = 0;

        for (const auto& [transponder_key, detection_vec] : detections) {
            if (detection_vec.empty()) { continue; }
            bool front_ok = (detection_vec.front().timestamp - margin) < ts_msg.timestamp;
            bool back_ok = (detection_vec.back().timestamp + margin) > ts_msg.timestamp;
            if  (front_ok && back_ok) {
                ++matching_transponder_count;
                matching_transponder = transponder_key;
            }
        }
        if (matching_transponder_count == 1) {
            TimeSync ts = {
                .timestamp = ts_msg.timestamp,
                .transponder_type = matching_transponder.first,
                .transponder_id = matching_transponder.second,
                .transponder_timestamp = ts_msg.transponder_timestamp
            };
            timesyncs.push_back(std::move(ts));
        }
    }

    // erase all processed timesync messages
    timesync_messages.clear();

    return timesyncs;
}
