#pragma once

#include <cstdlib>

#include <mutex>
#include <map>
#include <vector>

typedef std::map<int, int> RssiCounter;

class RxStatistics {
    std::map<uint64_t, RssiCounter> counters;
    uint64_t timestep = 32;
    uint64_t buckets = 16;

public:
    void rx_event(uint64_t timestamp, float rssi);
}