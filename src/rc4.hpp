#pragma once

#include <ostream>
#include <string>
#include <deque>
#include <map>
#include <vector>
#include <optional>

#include "frame.hpp"


struct RC4Message {
    uint64_t hi;
    uint64_t lo;

    RC4Message(const uint8_t *softbits);
    bool operator<(const RC4Message &o) const;
    std::string toString() const;
    friend std::ostream &operator<<(std::ostream &os, const RC4Message &m);
};

class RC4Registry {
    std::map<RC4Message, uint32_t> registry;
    uint32_t next_transponder = 1000u;

public:
    bool lookup(const RC4Message &message, uint32_t *transponder_id);
    void store(uint32_t transpoder_id, std::vector<RC4Message> messages);
};

class RC4Trainer {
    static constexpr size_t BUFFER_MAX_SIZE = 8196;

    struct Entry {
        uint64_t timestamp;
        float rssi;
        uint32_t transponder_id;
        RC4Message message;
    };

    enum state_t { IDLE, TRAINING, FINALIZING } state = IDLE;
    std::deque<Entry> buffer;

public:
    enum EvaluationResult { NO_ACTION, START, INTERRUPED, DONE, RESET };
    
    void append(uint64_t timestamp, float rssi, uint32_t transponder_id, RC4Message message);
    EvaluationResult evaluate(uint64_t timestamp);
    std::vector<RC4Message> registry_messages();
    uint32_t preferred_transponder_id();
    std::pair<uint64_t, uint64_t> buffer_timerange();
};
