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

struct Passing {
    uint32_t transponder_id;
    uint64_t timestamp;
    float rssi;
    size_t hits;
};

class PassingDetector {
    std::map<uint32_t, std::vector<Detection>> detections;
    std::mutex mutex;

public:
    void append(int32_t transponder_id, uint64_t timestamp, float rssi);
    std::vector<Passing> identify_passings(int64_t deadline);
};