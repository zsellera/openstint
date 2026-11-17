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
    AMB,        // rc3 and rc4 transponders
    Vostok,     // Vostok (using rc3 preamble)
};

std::string transponder_system_name(TransponderSystem tsys);

struct Detection {
    uint64_t timestamp;
    uint64_t timecode;
    float rssi;
    float mer; // modulation error ratio of the frame, dB

    Detection(uint64_t _ts, uint64_t _tc, float _rssi, float _mer) : timestamp(_ts), timecode(_tc), rssi(_rssi), mer(_mer) {};
};

struct TimeSyncMsg {
    uint64_t decoder_timestamp;
    uint32_t transponder_timestamp;

    TimeSyncMsg(uint64_t _ts, uint64_t _transponder_ts) : decoder_timestamp(_ts), transponder_timestamp(_transponder_ts) {};
};

// a single received frame of a passing, on the same timebase as Passing::timestamp
struct PassingHit {
    uint64_t timestamp; // microseconds
    float rssi;         // dBFS
    float mer;          // dB
};

struct Passing {
    uint64_t timestamp;
    TransponderSystem transponder_type;
    uint32_t transponder_id;
    float rssi;
    size_t hits;
    uint64_t duration;
    float snr; // dB, MER of the REPORT_HIT_LIMIT-th strongest detection
    std::vector<PassingHit> hit_list; // every detection the passing was computed from, in time order
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
    void append(const Frame* frame, TransponderSystem transponder_system, uint32_t transponder_id);
    void timesync(const Frame* frame, uint32_t transponder_timestamp);
    std::vector<TimeSync> identify_timesyncs(uint64_t margin);
    std::vector<Passing> identify_passings(uint64_t deadline);
    std::vector<uint32_t> passings_between(TransponderSystem tsys, uint64_t timestamp_from, uint64_t timestamp_until);
};