#pragma once

#include <cstdlib>

#include <mutex>
#include <map>
#include <vector>


struct Detection {
    uint64_t timestamp;
    float rssi;

    Detection(uint64_t _ts, float _rssi) : timestamp(_ts), rssi(_rssi) {};
};

struct TimeSyncMsg {
    uint64_t timestamp;
    uint32_t transponder_timestamp;

    TimeSyncMsg(uint64_t _ts, uint64_t _transponder_ts) : timestamp(_ts), transponder_timestamp(_transponder_ts) {};
};

struct Passing {
    uint32_t transponder_id;
    uint64_t timestamp;
    float rssi;
    size_t hits;
};

struct TimeSync {
    uint32_t transponder_id;
    uint64_t timestamp;
    uint32_t transponder_timestamp;
};

class PassingDetector {
    std::map<uint32_t, std::vector<Detection>> detections;
    std::vector<TimeSyncMsg> timesync_messages;
    std::mutex mutex;

public:
    void append(uint64_t timestamp, int32_t transponder_id, float rssi);
    void timesync(uint64_t timestamp, uint32_t transponder_timestamp);
    std::vector<TimeSync> identify_timesyncs(uint64_t margin);
    std::vector<Passing> identify_passings(uint64_t deadline);
};