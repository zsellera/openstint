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

// A transponder sitting still on the loop (RC4 learning, parked cars): PLATEAU_WINDOW
// frames flat to PLATEAU_START_DB, staying within PLATEAU_KEEP_DB while it does.
#define PLATEAU_WINDOW 128
#define PLATEAU_START_DB 2.0f
#define PLATEAU_KEEP_DB 3.0f

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

struct PassingPoint {
    uint64_t weighted_timestamp;
    float max_rssi;
    uint64_t duration;
};

PassingPoint compute_passing_point(const std::deque<Detection>& detections);

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
    float snr; // dB, MER of the REPORT_HIT_LIMIT-th strongest detection
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