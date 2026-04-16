#include "rc4.hpp"

#include <algorithm>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

#define RC4_TRAINING_RSSI_LIMIT -20.0f

RC4Message::RC4Message(const uint8_t *softbits) {
    // differential-decode: decoded[i] = raw[i] ^ raw[i-1], assuming raw[-1] = 0
    uint8_t bits[100];
    int prev = 1; // from preamble
    for (int i = 0; i < 100; i++) {
        int raw = softbits[i] > 127 ? 1 : 0;
        bits[i] = raw ^ prev;
        prev = raw;
    }

    // parity check: 5th bit of each block must be the inverse of the 4th
    is_valid = true;
    for (int block = 0; block < 20; block++) {
        if (bits[block * 5 + 3] == bits[block * 5 + 4]) {
            is_valid = false;
            break;
        }
    }

    // extract payload bits
    payload = 0ull;
    for (int block = 0; block < 16; block++) {
        for (int bit = 0; bit < 4; bit++) {
            int bit_idx = block * 5 + bit;
            int payload_idx = block * 4 + bit;
            if (bits[bit_idx]) {
                payload |= (uint64_t)1 << (63 - payload_idx);
            }
        }
    }

    // GF(2) verification codes: v[i] = XOR of selected payload bits, XOR constant
    if (is_valid) {
        static const std::vector<uint64_t> check_polys = {
            // block 17:
            0xc2cd82058e2c0c88ull,
            0xe166c102c7160644ull,
            0xf0b36081638b0322ull,
            0xf859b040b1c58191ull,
            // block 18:
            0xbee15a25d6cecc40ull,
            0xdf70ad12eb676620ull,
            0x6fb8568975b3b310ull,
            0xb7dc2b44bad9d988ull,
            // block 19:
            0xdbee15a25d6cecc4ull,
            0x6df70ad12eb67662ull,
            0x36fb8568975b3b31ull,
            0x59b040b1c5819110ull,
            // block 20:
            0x2cd82058e2c0c888ull,
            0x166c102c71606444ull,
            0x0b36081638b03222ull,
            0x859b040b1c581911ull
        };
        static const uint8_t check_constants[] = {
            0, 0, 1, 1,
            0, 0, 0, 1,
            0, 0, 1, 1,
            1, 1, 1, 0
        };
        static const int parity_pos[] = {
            80, 81, 82, 83,
            85, 86, 87, 88,
            90, 91, 92, 93,
            95, 96, 97, 98
        };

        for (int v = 0; v < 16; v++) {
            auto popcount = std::popcount(payload & check_polys[v]);
            auto parity = (popcount + check_constants[v]) % 2;
            if (parity != bits[parity_pos[v]]) {
                is_valid = false;
                break;
            }
        }
    }
}

bool RC4Registry::lookup(const uint64_t &rc4_payload, uint32_t *transponder_id) {
    std::shared_lock<std::shared_mutex> read_lock(mutex);

    auto it = registry.find(rc4_payload);
    if (it == registry.end())
        return false;
    *transponder_id = it->second;
    return true;
}

uint32_t RC4Registry::store(uint32_t transponder_id, std::vector<uint64_t> rc4_payloads) {
    std::unique_lock<std::shared_mutex> write_lock(mutex);

    if (transponder_id >= 1000 && transponder_id <= 9999) {
        if (transponder_id + 1 > next_transponder) {
            next_transponder = transponder_id + 1;
        }
    }
    if (transponder_id == 0) {
        transponder_id = (next_transponder++);
    }
    for (const auto &p : rc4_payloads) {
        registry[p] = transponder_id;
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

uint32_t RC4FileBasedRegistry::store(uint32_t transponder_id, std::vector<uint64_t> rc4_payloads) {
    transponder_id = RC4Registry::store(transponder_id, rc4_payloads);
    loaded_ids.insert(transponder_id);

    std::ofstream file(directory + "/" + std::to_string(transponder_id) + ".rc4", std::ios::app);
    for (const auto &p : rc4_payloads) {
        char buf[17];
        snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)p);
        file << buf << "\n";
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
        // do not re-load already loaded (even if content has changed!)
        if (loaded_ids.count(id)) { continue; }

        std::ifstream file(path);
        std::string line;
        std::vector<uint64_t> payloads;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() == 16) {
                try {
                    payloads.push_back(std::stoull(line, nullptr, 16));
                } catch (...) {}
            } else if (line.size() == 28) {
                // older pilot format: 28 hex chars = 112 bits
                // drop first 5 bits, next 100 bits become softbits
                try {
                    // parse 28 hex chars into 14 bytes
                    uint8_t bytes[14];
                    for (int i = 0; i < 14; i++) {
                        bytes[i] = (uint8_t)std::stoul(line.substr(i * 2, 2), nullptr, 16);
                    }
                    // extract 100 softbits starting at bit offset 5
                    uint8_t softbits[100];
                    for (int i = 0; i < 100; i++) {
                        int bit_pos = 5 + i;
                        int byte_idx = bit_pos / 8;
                        int bit_idx = 7 - (bit_pos % 8);
                        softbits[i] = (bytes[byte_idx] >> bit_idx) & 1 ? 0xFF : 0x00;
                    }
                    RC4Message msg(softbits);
                    if (msg.is_valid) {
                        payloads.push_back(msg.payload);
                    }
                } catch (...) {}
            }
        }
        if (!payloads.empty()) {
            std::cerr << "RC4 transpoder loaded: " << id << std::endl;
            RC4Registry::store(id, payloads);
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

void RC4Trainer::append(uint64_t timestamp, float rssi, uint32_t transponder_id, uint64_t rc4_payload) {
    std::lock_guard<std::mutex> lock(mutex);

    buffer.push_back({timestamp, rc4_payload, rssi, transponder_id});
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
            if ((int64_t)(timestamp - last.timestamp) > 100000 || last.rssi <= RC4_TRAINING_RSSI_LIMIT) break;
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

std::vector<uint64_t> RC4Trainer::registry_payloads() {
    std::lock_guard<std::mutex> lock(mutex);

    std::map<uint64_t, int> counts;
    for (const auto &e : buffer) {
        counts[e.rc4_payload]++;
    }
    std::vector<uint64_t> payloads;
    for (const auto &[p, count] : counts) {
        if (count > 1) {
            payloads.push_back(p);
        }
    }
    return payloads;
}

uint32_t RC4Trainer::preferred_transponder_id() {
    std::lock_guard<std::mutex> lock(mutex);

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

float RC4Trainer::last_rssi() {
    std::lock_guard<std::mutex> lock(mutex);
    return buffer.back().rssi;
}
