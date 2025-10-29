#pragma once

#include <string>
#include <string_view>

#define MAX_PREAMBLE 16

enum class TransponderType {
    OpenStint, // support for openstint protocol
    Legacy     // legacy (Amb) protocol
};

struct TransponderProps {
    uint16_t bpsk_preamble;
    uint16_t dpsk_preamble;
    std::size_t payload_size;
    std::string_view prefix;
};

void init_transponders();
int decode_openstint(const uint8_t *softbits, uint32_t *transponder_id);
int decode_legacy(const uint8_t *softbits, uint32_t *transponder_id);

inline constexpr TransponderProps TRANSPONDER_PROPERTIES[] = {
    {0xe255, 0x937f, 80, "OPN"},
    {0x51e4, 0x7916, 80, "AMB"}
};

constexpr TransponderProps transponder_props(TransponderType t) {
    return TRANSPONDER_PROPERTIES[static_cast<int>(t)];
}