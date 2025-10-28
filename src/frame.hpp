#pragma once

#include <cstdint>
#include <cstdbool>
#include <complex>
#include <optional>
#include <ostream>

#include "transponder.hpp"
#include "preamble.hpp"

#include <liquid/liquid.h>

struct Frame {
    TransponderType transponder_type; // what kind of preamble was matched
    uint32_t preamble_size;
    uint32_t payload_size;

    // bitstream decision probabilities for soft-decoding
    // 0..127..255 <=> totally 0 ... unknown ... totally 1
    std::vector<uint8_t> softbits;

    uint64_t timestamp;
    float rssi; // *relative* to 1 LSB
    float snr;
    
    Frame();
    Frame(TransponderType transponder_type, uint64_t timestamp, int symbol_e2, int noise_e2);

    const uint8_t* bits();
};

std::ostream& operator <<(std::ostream& os, const Frame& f);

class FrameDetector {
    static constexpr int samples_per_symbol = 4;

    // preamble matching
    static inline const Preamble<uint16_t> p_openstint { transponder_props(TransponderType::OpenStint).preamble };
    static inline const Preamble<uint16_t> p_legacy { transponder_props(TransponderType::Legacy).preamble };
    CircBuff<uint16_t> buffer;
    float threshold;

    // stream statistics:
    std::complex<int8_t> offset= {0, 0}; // dc offset ~ sample mean
    uint32_t variance2 = 0; // ~noise power
    
    // statistic calculation:
    std::complex<int32_t> s1 = {0, 0}; // sum of samples
    uint32_t s2 = 0; // sum of sample squared
    int n = 0; // number of samples measured
public:
    FrameDetector(float threshold);

    std::optional<TransponderType> process_baseband(const std::complex<int8_t> *samples);
    void update_statistics();
    void reset_statistics_counters();

    const uint32_t symbol_energy2();
    const uint32_t noise_energy2();
    const std::complex<int8_t> dc_offset();
};

class SymbolReader {
    static constexpr int samples_per_symbol = 4;
    static constexpr int filter_delay = 5;
    static constexpr int preamble_length = 16;

    symsync_crcf symsync;
    modemcf dpsk_modem;

    // reading a matched preamble might require lookback into
    // the previous buffer. this contain the last section of
    // the previous buffer
    std::complex<int8_t> reserve_buffer[MAX_PREAMBLE*samples_per_symbol];

public:
    SymbolReader();
    ~SymbolReader();
    // manages external resources (liquid) with uncopyable internal state:
    SymbolReader(const SymbolReader&) = delete;
    SymbolReader(SymbolReader&&) noexcept = delete;
    SymbolReader& operator=(const SymbolReader&) = delete;
    SymbolReader& operator=(SymbolReader&&) noexcept = delete;
    
    void read_preamble(Frame *dst, std::complex<int8_t> offset, const std::complex<int8_t> *src, int end);
    void read_symbol(Frame *dst, std::complex<int8_t> offset, const std::complex<int8_t> *src);
    void update_reserve_buffer(const std::complex<int8_t> *src, int end);
    bool is_frame_complete(const Frame *f);

private:
    uint32_t read_single(Frame *dst, const std::complex<int8_t> offset, const std::complex<int8_t> *src);
    void read_preamble0(Frame *dst, std::complex<int8_t> offset, const std::complex<int8_t> *src, int end);
};
