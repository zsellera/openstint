#include "rc4.hpp"

#include <algorithm>
#include <string>

RC4Message::RC4Message(const uint8_t *softbits) {
    hi = 0;
    for (int i = 0; i < 48; i++) {
        if (softbits[i] >= 128)
            hi |= (uint64_t)1 << (47 - i);
    }

    lo = 0;
    for (int i = 0; i < 64; i++) {
        if (softbits[48 + i] >= 128)
            lo |= (uint64_t)1 << (63 - i);
    }
}

bool RC4Message::operator<(const RC4Message &o) const {
    return hi < o.hi || (hi == o.hi && lo < o.lo);
}

std::string RC4Message::toString() const {
    char buf[29];
    snprintf(buf, sizeof(buf), "%012llx%016llx",
             (unsigned long long)(hi & 0xFFFFFFFFFFFFULL),
             (unsigned long long)lo);
    return buf;
}

std::ostream &operator<<(std::ostream &os, const RC4Message &m) {
    return os << m.toString();
}

bool RC4Registry::lookup(const RC4Message &message, uint32_t *transponder_id) {
    auto it = registry.find(message);
    if (it == registry.end())
        return false;
    *transponder_id = it->second;
    return true;
}

void RC4Registry::store(uint32_t transponder_id, std::vector<RC4Message> messages) {
    if (transponder_id == 0) {
        transponder_id = (next_transponder++);
    }
    for (const auto &msg : messages)
        registry[msg] = transponder_id;
}

void RC4Trainer::append(uint64_t timestamp, float rssi, uint32_t transponder_id, RC4Message message) {
    buffer.push_back({timestamp, rssi, transponder_id, message});
    if (buffer.size() > BUFFER_MAX_SIZE)
        buffer.pop_front();
}

RC4Trainer::EvaluationResult RC4Trainer::evaluate(uint64_t timestamp) {
    switch (state) {
        case state_t::IDLE: {
            if (buffer.size() < 128) break;
            const Entry &last = buffer.back();
            if ((int64_t)(timestamp - last.timestamp) > 100000 || last.rssi <= -15.0f) break;
            auto tail = std::prev(buffer.end(), 128);
            auto [mn, mx] = std::minmax_element(
                tail,
                buffer.end(),
                [](const Entry &a, const Entry &b) { return a.rssi < b.rssi; }
            );
            bool stable = (int64_t)(last.timestamp - tail->timestamp) <= 1000000 &&
                          mx->rssi - mn->rssi <= 2.0f;
            if (stable) {
                state = state_t::TRAINING;
                buffer.erase(buffer.begin(), tail);
                return EvaluationResult::START;
            }
        }
        break;

        case state_t::TRAINING: {
            const Entry &last = buffer.back();
            if ((int64_t)(timestamp - last.timestamp) > 500000) {
                state = state_t::IDLE;
                return EvaluationResult::INTERRUPED;
            }
            auto [mn, mx] = std::minmax_element(
                buffer.begin(),
                buffer.end(),
                [](const Entry &a, const Entry &b) { return a.rssi < b.rssi; }
            );
            bool stable = (mx->rssi - mn->rssi) <= 3.0f;
            if (!stable) {
                state = state_t::IDLE;
                return EvaluationResult::INTERRUPED;
            }
            if (buffer.size() >= BUFFER_MAX_SIZE) {
                state = state_t::FINALIZING;
                return EvaluationResult::DONE;
            }
        }
        break;

        case state_t::FINALIZING: {
            const Entry &last = buffer.back();
            if ((int64_t)(timestamp - last.timestamp) > 1000000) {
                state = state_t::IDLE;
                return EvaluationResult::RESET;
            }
        }
        break;
    }
    return EvaluationResult::NO_ACTION;
}

std::vector<RC4Message> RC4Trainer::registry_messages() {
    // find repeating messages by counting them:
    std::map<RC4Message, int> counts;
    for (const auto &e : buffer) {
        counts[e.message]++;
    }
    // collect repeating messages:
    std::vector<RC4Message> messages;
    for (const auto &[msg, count] : counts) {
        if (count > 1) {
            messages.push_back(msg);    
        }
    }
    return messages;
}

uint32_t RC4Trainer::preferred_transponder_id() {
    // if any of the messages were from a previous training, find
    // the preferred transponder id:
    auto known = std::find_if(
        buffer.begin(), buffer.end(),
        [](const Entry &e) { return e.transponder_id != 0; }
    );
    return known != buffer.end() ? known->transponder_id : 0;
}

std::pair<uint64_t, uint64_t> RC4Trainer::buffer_timerange() {
    return std::make_pair(buffer.front().timestamp, buffer.back().timestamp);
}
