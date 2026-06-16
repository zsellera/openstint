#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>

enum class TransponderProtocol {
    OpenStint, // openstint protocol
    RC3,       // legacy protocol
    RC4,       // newer MyLaps protocol
};

static constexpr int PREAMBLE_LENGTH = 16;

// expand a 16-bit preamble word into its BPSK ±1 symbols (MSB first)
constexpr std::array<float, PREAMBLE_LENGTH> preamble_symbols(uint16_t word) {
    std::array<float, PREAMBLE_LENGTH> syms{};
    for (int i = 0; i < PREAMBLE_LENGTH; i++) {
        syms[i] = (word & (1 << (PREAMBLE_LENGTH - 1 - i))) ? +1.0f : -1.0f;
    }
    return syms;
}

// upsample the ±1 preamble to the sample rate (np.repeat(pre, sps))
constexpr std::array<float, PREAMBLE_LENGTH * SAMPLES_PER_SYMBOL> preamble_upsampled(uint16_t word) {
    std::array<float, PREAMBLE_LENGTH * SAMPLES_PER_SYMBOL> up{};
    const auto syms = preamble_symbols(word);
    for (int i = 0; i < PREAMBLE_LENGTH; i++) {
        for (int s = 0; s < SAMPLES_PER_SYMBOL; s++) {
            up[i * SAMPLES_PER_SYMBOL + s] = syms[i];
        }
    }
    return up;
}

struct TransponderProps {
    uint16_t dpsk_preamble;
    uint16_t preamble;
    std::size_t payload_size;
    std::string_view prefix;
    std::array<float, PREAMBLE_LENGTH> preamble_syms;
    std::array<float, PREAMBLE_LENGTH * SAMPLES_PER_SYMBOL> preamble_up;
};

void init_transponders();
int decode_openstint(const uint8_t *softbits, uint32_t *transponder_id);
int decode_rc3(const uint8_t *softbits, uint32_t *transponder_id, uint8_t *status_code);

inline constexpr TransponderProps TRANSPONDER_PROPERTIES[] = {
    {0x857c, 0xf9a8, 80, "OPN", preamble_symbols(0xf9a8), preamble_upsampled(0xf9a8)},
    {0x7916, 0x51e4, 80, "RC3", preamble_symbols(0x51e4), preamble_upsampled(0x51e4)},
    {0xc0ab, 0x80cd, 100, "RC4", preamble_symbols(0x80cd), preamble_upsampled(0x80cd)}
};

constexpr TransponderProps transponder_props(TransponderProtocol t) {
    return TRANSPONDER_PROPERTIES[static_cast<int>(t)];
}

// Old AMBRc DP transponders send *transponder* frames with all status bits set;
// unfortunately newer models can transmit RC3 status/validation messages the same way.
// This builds a block-list for such transponders.
//
// For a given transponder, top 8 bits of status/validation messages are the same.
// We do not report a passing unless there are at least 2 frames detected; we can
// pass the problematic transponder_id through, then permanently ban it once a
// status message with the same 8 MSB is detected within 250ms.
class AmbRcBlacklist {
    std::map<uint8_t, uint64_t> msb8_timestamps; // msb8 -> timestamp of latest validation message
    std::set<uint32_t> banned_transponders;      // permanently banned transponder ids

public:
    void process(uint64_t timestamp, uint8_t status_code, uint32_t transponder_id);
    bool check_banned(uint32_t transponder_id) const;
};