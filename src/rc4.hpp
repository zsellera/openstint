#pragma once

#include <ostream>
#include <string>
#include <deque>
#include <map>
#include <set>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <optional>

#include "frame.hpp"


struct RC4Message {
    uint64_t payload;
    bool is_valid;

    RC4Message() : payload(0), is_valid(false) {}
    RC4Message(const uint8_t *softbits);
};

class RC4Registry {
    std::shared_mutex mutex;
    std::map<uint64_t, uint32_t> registry;
    uint32_t next_transponder = 1000u;

protected:
    void remove(uint32_t transponder_id);

public:
    virtual ~RC4Registry() = default;
    bool lookup(const uint64_t &rc4_payload, uint32_t *transponder_id);
    virtual uint32_t store(uint32_t transponder_id, std::vector<uint64_t> rc4_payloads);
    virtual void resync();
};

class RC4FileBasedRegistry : public RC4Registry {
    std::string directory;
    std::set<uint32_t> loaded_ids;

public:
    explicit RC4FileBasedRegistry(std::string directory);
    uint32_t store(uint32_t transponder_id, std::vector<uint64_t> rc4_payloads) override;
    void resync() override;
};

class RC4Trainer {
    static constexpr size_t BUFFER_MAX_SIZE = 8196;

    struct Entry {
        uint64_t timestamp;
        uint64_t rc4_payload;
        float rssi;
        uint32_t transponder_id;
    };

    std::mutex mutex;
    enum state_t { IDLE, TRAINING, FINALIZING } state = IDLE;
    std::deque<Entry> buffer;

public:
    enum EvaluationResult { NO_ACTION, START, INTERRUPED, DONE, RESET };

    void append(uint64_t timestamp, float rssi, uint32_t transponder_id, uint64_t rc4_payload);
    EvaluationResult evaluate(uint64_t timestamp);
    std::vector<uint64_t> registry_payloads();
    uint32_t preferred_transponder_id();
    std::pair<uint64_t, uint64_t> buffer_timerange();
    float last_rssi();
};
