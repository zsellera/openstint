#pragma once

#include <cstdlib>

#include <mutex>
#include <map>
#include <deque>
#include <string>
#include <vector>
#include <utility>

#include "transponder.hpp"
#include "frame.hpp"


enum class TransponderSystem {
    OpenStint,  // openstint transponder
    AMB         // rc3 and rc4 transponders
};

std::string transponder_system_name(TransponderSystem tsys);

struct Detection {
    uint64_t timestamp;
    uint64_t timecode;
    float rssi;

    Detection(uint64_t _ts, uint64_t _tc, float _rssi) : timestamp(_ts), timecode(_tc), rssi(_rssi) {};
};

struct TimeSyncMsg {
    uint64_t decoder_timestamp;
    uint32_t transponder_timestamp;

    TimeSyncMsg(uint64_t _ts, uint64_t _transponder_ts) : decoder_timestamp(_ts), transponder_timestamp(_transponder_ts) {};
};

struct Passing {
    uint64_t timestamp;
    TransponderSystem transponder_type;
    uint32_t transponder_id;
    float rssi;
    size_t hits;
    uint64_t duration;
};

struct TimeSync {
    uint64_t timestamp;
    TransponderSystem transponder_type;
    uint32_t transponder_id;
    uint32_t transponder_timestamp;
};

typedef std::pair<TransponderSystem, uint32_t> TransponderKey;

class PassingDetector {
    std::map<TransponderKey, std::deque<Detection>> detections;
    std::vector<TimeSyncMsg> timesync_messages;
    std::mutex mutex;

public:
    void append(const Frame* frame, uint32_t transponder_id);
    void timesync(const Frame* frame, uint32_t transponder_timestamp);
    std::vector<TimeSync> identify_timesyncs(uint64_t margin);
    std::vector<Passing> identify_passings(uint64_t deadline);
    std::vector<uint32_t> passings_between(TransponderSystem tsys, uint64_t timestamp_from, uint64_t timestamp_until);
};