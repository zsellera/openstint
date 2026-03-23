#include "rc4.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
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

std::optional<RC4Message> RC4Message::fromString(const std::string &s) {
    if (s.size() != 28) return std::nullopt;
    try {
        RC4Message msg;
        msg.hi = std::stoull(s.substr(0, 12), nullptr, 16);
        msg.lo = std::stoull(s.substr(12, 16), nullptr, 16);
        return msg;
    } catch (...) {
        return std::nullopt;
    }
}

bool RC4Registry::lookup(const RC4Message &message, uint32_t *transponder_id) {
    std::shared_lock<std::shared_mutex> read_lock(mutex);

    auto it = registry.find(message);
    if (it == registry.end())
        return false;
    *transponder_id = it->second;
    return true;
}

uint32_t RC4Registry::store(uint32_t transponder_id, std::vector<RC4Message> messages) {
    std::unique_lock<std::shared_mutex> write_lock(mutex);

    if (transponder_id >= 1000 && transponder_id <= 9999) {
        if (transponder_id + 1 > next_transponder) {
            next_transponder = transponder_id + 1;
        }
    }
    if (transponder_id == 0) {
        transponder_id = (next_transponder++);
    }
    for (const auto &msg : messages) {
        registry[msg] = transponder_id;
    }

    return transponder_id;
}

void RC4Registry::remove(uint32_t transponder_id) {
    std::unique_lock<std::shared_mutex> write_lock(mutex);

    for (auto it = registry.begin(); it != registry.end(); ) {
        if (it->second == transponder_id) {
            it = registry.erase(it);
        } else {
            ++it;
        }
    }
}

void RC4Registry::resync() {
    // do nothing, work in-memory only
}

RC4FileBasedRegistry::RC4FileBasedRegistry(std::string directory)
    : directory(std::move(directory)) {}

uint32_t RC4FileBasedRegistry::store(uint32_t transponder_id, std::vector<RC4Message> messages) {
    transponder_id = RC4Registry::store(transponder_id, messages);
    loaded_ids.insert(transponder_id);

    std::ofstream file(directory + "/" + std::to_string(transponder_id) + ".rc4", std::ios::app);
    for (const auto &msg : messages) {
        file << msg << "\n";
    }

    return transponder_id;
}

void RC4FileBasedRegistry::resync() {
    namespace fs = std::filesystem;

    std::set<uint32_t> current_ids;

    for (const auto &entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) { continue; }
        const auto &path = entry.path();
        if (path.extension() != ".rc4") { continue; }

        const std::string stem = path.stem().string();
        if (stem.empty() || !std::all_of(stem.begin(), stem.end(), ::isdigit)) {
            continue;
        }

        uint32_t id;
        try { id = std::stoul(stem); } catch (...) { continue; }

        current_ids.insert(id);
        if (loaded_ids.count(id)) { continue; }

        std::ifstream file(path);
        std::string line;
        std::vector<RC4Message> messages;
        while (std::getline(file, line)) {
            auto msg = RC4Message::fromString(line);
            if (msg) messages.push_back(*msg);
        }

        if (!messages.empty()) {
            std::cerr << "RC4 transpoder loaded: " << id << std::endl;
            RC4Registry::store(id, messages);
        }
        loaded_ids.insert(id);
    }

    for (auto it = loaded_ids.begin(); it != loaded_ids.end(); ) {
        if (!current_ids.count(*it)) {
            RC4Registry::remove(*it);
            std::cerr << "RC4 transpoder removed: " << (*it) << std::endl;
            it = loaded_ids.erase(it);
        } else {
            ++it;
        }
    }
}

void RC4Trainer::append(uint64_t timestamp, float rssi, uint32_t transponder_id, RC4Message message) {
    std::lock_guard<std::mutex> lock(mutex);

    buffer.push_back({timestamp, rssi, transponder_id, message});
    if (buffer.size() > BUFFER_MAX_SIZE) {
        buffer.pop_front();
    }
}

RC4Trainer::EvaluationResult RC4Trainer::evaluate(uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex);

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
    std::lock_guard<std::mutex> lock(mutex);

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
    std::lock_guard<std::mutex> lock(mutex);

    // if any of the messages were from a previous training, find
    // the preferred transponder id:
    auto known = std::find_if(
        buffer.begin(), buffer.end(),
        [](const Entry &e) { return e.transponder_id != 0; }
    );
    return known != buffer.end() ? known->transponder_id : 0;
}

std::pair<uint64_t, uint64_t> RC4Trainer::buffer_timerange() {
    std::lock_guard<std::mutex> lock(mutex);
    return std::make_pair(buffer.front().timestamp, buffer.back().timestamp);
}
