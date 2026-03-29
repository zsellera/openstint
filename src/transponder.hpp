#pragma once

#include <cstdint>
#include <string>
#include <string_view>

enum class TransponderProtocol {
    OpenStint, // openstint protocol
    RC3,       // legacy protocol
    RC4,       // newer MyLaps protocol
};

struct TransponderProps {
    uint16_t bpsk_preamble;
    std::size_t payload_size;
    std::string_view prefix;
};

void init_transponders();
int decode_openstint(const uint8_t *softbits, uint32_t *transponder_id);
int decode_rc3(const uint8_t *softbits, uint32_t *transponder_id);

inline constexpr TransponderProps TRANSPONDER_PROPERTIES[] = {
    {0xf9a8, 80, "OPN"},
    {0x51e4, 80, "RC3"},
    {0x5406, 112, "RC4"}
};

constexpr TransponderProps transponder_props(TransponderProtocol t) {
    return TRANSPONDER_PROPERTIES[static_cast<int>(t)];
}