#pragma once

#include <cstdint>
#include <vector>

// Soft-decision decoder for short binary block codes, driven by a syndrome table.
//
// Every parity check of a linear code is an XOR over a fixed set of bit positions
// that comes out 0 on a valid codeword. Turn that around and ask, for each bit,
// which checks it takes part in: that set of checks is the bit's *column syndrome*,
// one bit per check. The syndrome of a received word is then the XOR of the column
// syndromes of the bits that arrived as 1, and it is zero exactly when every check
// passes -- the same answer as evaluating the checks one by one, computed bit by bit
// instead of check by check.
//
// What that buys is error correction. The syndrome depends only on which bits got
// flipped and never on the data, so a single flipped bit always produces that bit's
// own column syndrome: one lookup names the bit to repair. Codes whose checks carry
// constant terms (affine codes, such as RC4's) fold those constants into `offset`,
// which is simply the syndrome of the all-zero word.
//
// decode() adds Chase-II on top: it retries the search with every combination of the
// `chase_bits` least reliable bits flipped, and keeps whichever candidate reaches a
// codeword with the cheapest flips in soft-decision terms. That reaches error patterns
// past the single bit plain syndrome decoding can repair, for 2^chase_bits lookups.
//
// Limits: at most 64 parity checks (one uint64_t of syndrome) and MAX_BITS positions.
class SyndromeCode {
public:
    static constexpr int MAX_BITS = 128;      // RC4's frame is 100 symbols
    static constexpr int MAX_CHASE_BITS = 10; // 1024 candidate patterns

    struct Correction {
        bool valid = false; // a codeword was reached
        int flips = 0;      // bits repaired to get there
        int cost = 0;       // summed reliability of the repaired bits
    };

    // column_syndromes: one entry per bit position, the checks that bit appears in.
    // offset: the syndrome of the all-zero word, carrying any constant terms.
    SyndromeCode(const uint64_t *column_syndromes, int bit_count, uint64_t offset);

    int bit_count() const { return static_cast<int>(columns.size()); }

    // syndrome of a word of hard decisions (one 0/1 per bit); zero means valid
    uint64_t syndrome(const uint8_t *hard_bits) const;

    // the single bit whose repair explains this syndrome, or -1 if no single bit
    // does. Syndromes shared by two positions are reported as -1: such an error is
    // detected but not locatable, and guessing would corrupt the frame.
    int single_error(uint64_t syndrome) const;

    // Hard-decide `softbits` (0..255, above 127 is a 1) into `hard_bits`, then repair
    // it towards the nearest codeword. On success `hard_bits` holds that codeword.
    // On failure it is left holding the unrepaired hard decisions.
    Correction decode(const uint8_t *softbits, uint8_t *hard_bits, int chase_bits) const;

private:
    struct Entry {
        uint64_t syndrome;
        int bit;
    };

    std::vector<uint64_t> columns; // column syndrome per bit position
    std::vector<Entry> singles;    // sorted by syndrome, ambiguous ones dropped
    uint64_t offset;               // syndrome of the all-zero word
};
