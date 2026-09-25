#include "syndrome.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdlib>
#include <limits>

SyndromeCode::SyndromeCode(const uint64_t *column_syndromes, int bit_count, uint64_t offset)
    : columns(column_syndromes, column_syndromes + bit_count), offset(offset) {
    // decode() keeps its per-bit working set on the stack, sized for MAX_BITS
    assert(bit_count > 0 && bit_count <= MAX_BITS);

    // Build the single-error lookup. A zero column means the bit takes part in no
    // check at all, and a syndrome two bits share names no unique repair; neither
    // can be corrected, so both stay out of the table and read back as "detected,
    // not located" rather than as a guess that would corrupt the frame.
    for (int i = 0; i < bit_count; i++) {
        if (columns[i] != 0) {
            singles.push_back({columns[i], i});
        }
    }
    std::sort(singles.begin(), singles.end(),
              [](const Entry &a, const Entry &b) { return a.syndrome < b.syndrome; });

    std::vector<Entry> unique;
    for (std::size_t i = 0; i < singles.size(); ) {
        std::size_t j = i + 1;
        while (j < singles.size() && singles[j].syndrome == singles[i].syndrome) { j++; }
        if (j == i + 1) {
            unique.push_back(singles[i]);
        }
        i = j;
    }
    singles.swap(unique);
}

uint64_t SyndromeCode::syndrome(const uint8_t *hard_bits) const {
    uint64_t s = offset;
    for (std::size_t i = 0; i < columns.size(); i++) {
        if (hard_bits[i]) {
            s ^= columns[i];
        }
    }
    return s;
}

int SyndromeCode::single_error(uint64_t s) const {
    if (s == 0) { // already a codeword, nothing to repair
        return -1;
    }
    auto it = std::lower_bound(singles.begin(), singles.end(), s,
                               [](const Entry &e, uint64_t v) { return e.syndrome < v; });
    return (it != singles.end() && it->syndrome == s) ? it->bit : -1;
}

SyndromeCode::Correction SyndromeCode::decode(const uint8_t *softbits, uint8_t *hard_bits, int chase_bits) const {
    const int n = static_cast<int>(columns.size());

    // hard decisions, plus how far each one sits from the 127/128 decision boundary.
    // that distance is the reliability: cheap bits to flip are the ones the demodulator
    // was least sure about, and the sum over the flipped bits is the cost Chase minimises.
    int reliability[MAX_BITS];
    for (int i = 0; i < n; i++) {
        hard_bits[i] = softbits[i] > 127 ? 1 : 0;
        reliability[i] = std::abs(2 * static_cast<int>(softbits[i]) - 255);
    }

    const uint64_t s0 = syndrome(hard_bits);
    if (s0 == 0) { // received clean, no cheaper answer exists
        return {true, 0, 0};
    }

    const int p = std::clamp(chase_bits, 0, std::min(MAX_CHASE_BITS, n));

    // the p least reliable positions, the ones Chase-II retries
    int least_reliable[MAX_CHASE_BITS];
    bool taken[MAX_BITS] = {false};
    for (int t = 0; t < p; t++) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (!taken[i] && (best < 0 || reliability[i] < reliability[best])) {
                best = i;
            }
        }
        taken[best] = true;
        least_reliable[t] = best;
    }

    // best candidate so far: a subset of the chase positions, plus at most one bit
    // located by the single-error table
    int best_cost = std::numeric_limits<int>::max();
    uint32_t best_pattern = 0;
    int best_single = -1;
    bool found = false;

    if (const int j = single_error(s0); j >= 0) {
        best_cost = reliability[j];
        best_single = j;
        found = true;
    }

    // Walk the 2^p flip patterns in Gray-code order, so each step toggles exactly one
    // position and the syndrome and cost carry over with a single XOR and one add.
    // countr_zero(i) is the position that changes between Gray codes i-1 and i.
    uint64_t s = s0;
    uint32_t pattern = 0;
    int cost = 0;
    for (uint32_t i = 1; i < (1u << p); i++) {
        const int k = std::countr_zero(i);
        const int bit = least_reliable[k];

        pattern ^= (1u << k);
        s ^= columns[bit];
        cost += (pattern & (1u << k)) ? reliability[bit] : -reliability[bit];

        if (cost >= best_cost) { // even a free repair could not win from here
            continue;
        }
        if (s == 0) { // the flips alone reached a codeword
            best_cost = cost;
            best_pattern = pattern;
            best_single = -1;
            found = true;
            continue;
        }

        const int j = single_error(s);
        if (j < 0) {
            continue;
        }
        // Repairing a bit this pattern already flipped just undoes that flip; the same
        // word is reached by the pattern without it, which is cheaper and enumerated
        // separately, so skip it rather than charge for both flips.
        bool inside = false;
        for (int t = 0; t < p; t++) {
            if (least_reliable[t] == j && (pattern & (1u << t))) {
                inside = true;
                break;
            }
        }
        if (inside || cost + reliability[j] >= best_cost) {
            continue;
        }
        best_cost = cost + reliability[j];
        best_pattern = pattern;
        best_single = j;
        found = true;
    }

    if (!found) { // errors detected, but no codeword within reach
        return {false, 0, 0};
    }

    int flips = 0;
    for (int t = 0; t < p; t++) {
        if (best_pattern & (1u << t)) {
            hard_bits[least_reliable[t]] ^= 1;
            flips++;
        }
    }
    if (best_single >= 0) {
        hard_bits[best_single] ^= 1;
        flips++;
    }
    return {true, flips, best_cost};
}
