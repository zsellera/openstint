#pragma once

#include <cstdint>
#include <string>
#include <string_view>

enum class TransponderType {
    OpenStint, // support for openstint protocol
    Legacy,     // legacy (Amb) protocol
    RC4         //RC4 (Amb RC4) protocol
};

struct TransponderProps {
    uint16_t bpsk_preamble;
    std::size_t payload_size;
    std::string_view prefix;
};

void init_transponders();
int decode_openstint(const uint8_t *softbits, uint32_t *transponder_id);
int decode_legacy(const uint8_t *softbits, uint32_t *transponder_id);
int decode_rc4(const uint8_t *softbits, uint32_t *transponder_id,uint64_t timestamp);

inline constexpr TransponderProps TRANSPONDER_PROPERTIES[] = {
    {0xf9a8, 80, "OPN"},
    {0x51e4, 80, "RC3"},
    {0x5406, 80, "RC4"} 
};

constexpr TransponderProps transponder_props(TransponderType t) {
    return TRANSPONDER_PROPERTIES[static_cast<int>(t)];
}
