#pragma once

#include <array>
#include <cstdint>
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
    uint16_t bpsk_preamble;
    std::size_t payload_size;
    std::string_view prefix;
    std::array<float, PREAMBLE_LENGTH> preamble_syms;
    std::array<float, PREAMBLE_LENGTH * SAMPLES_PER_SYMBOL> preamble_up;
};

void init_transponders();
int decode_openstint(const uint8_t *softbits, uint32_t *transponder_id);
int decode_rc3(const uint8_t *softbits, uint32_t *transponder_id, uint8_t *status_code);

inline constexpr TransponderProps TRANSPONDER_PROPERTIES[] = {
    {0xf9a8, 80, "OPN", preamble_symbols(0xf9a8), preamble_upsampled(0xf9a8)},
    {0x51e4, 80, "RC3", preamble_symbols(0x51e4), preamble_upsampled(0x51e4)},
    {0x80cd, 100, "RC4", preamble_symbols(0x80cd), preamble_upsampled(0x80cd)}
};

constexpr TransponderProps transponder_props(TransponderProtocol t) {
    return TRANSPONDER_PROPERTIES[static_cast<int>(t)];
}