#pragma once

#include <string>
#include <string_view>

#define MAX_PREAMBLE 16

enum class TransponderType {
    OpenStint, // support for openstint protocol
    Legacy     // legacy (Amb) protocol
};

struct TransponderProps {
    uint16_t preamble;
    std::size_t payload_size;
    std::string_view prefix;
};

uint32_t decode_openstint(const uint8_t *softbits, int *err);
uint32_t decode_legacy3(const uint8_t *softbits, int *err);

inline constexpr TransponderProps TRANSPONDER_PROPERTIES[] = {
    {0xe255, 80, "OPN"},
    {0x51e4, 80, "AMB"}
};

constexpr TransponderProps transponder_props(TransponderType t) {
    return TRANSPONDER_PROPERTIES[static_cast<int>(t)];
}