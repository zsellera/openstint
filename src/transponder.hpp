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

inline constexpr TransponderProps TRANSPONDER_PROPERTIES[] = {
    {0xe255, 80, "OPN"},
    {0x51e4, 80, "AMB"}
};

constexpr TransponderProps transponder_props(TransponderType t) {
    return TRANSPONDER_PROPERTIES[static_cast<int>(t)];
}