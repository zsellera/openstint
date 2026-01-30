#pragma once

#include <cstdint>
#include <cstdbool>
#include <complex>
#include <optional>
#include <ostream>
#include <vector>

#include "summing_buffer.hpp"

#include "transponder.hpp"
#include "preamble.hpp"

#include <liquid/liquid.h>

#define ADC_FULL_SCALE 127.0f

struct Frame {
    TransponderType transponder_type; // what kind of preamble was matched
    uint32_t preamble_size;
    uint32_t payload_size;

    // bitstream decision probabilities for soft-decoding
    // 0..127..255 <=> totally 0 ... unknown ... totally 1
    std::vector<uint8_t> softbits;
    // actual symbols
    std::vector<std::complex<float>> symbols;
    // decoding error accumulator
    float evm_sum = 0;

    uint64_t timestamp;
    
    // preamble-data
    // what is the optimal sampling point when reading
    int symsync_sym = 0;
    int symsync_bank = 0;
    float symbol_magnitude = 0;
    float symbol_phase = 0;
    std::complex<float> correction = {1.0f, 0.0f}; // phase & magnitude correction
    
    Frame();
    Frame(TransponderType transponder_type, uint64_t timestamp);

    const uint8_t* bits();
    float rssi() const;
    float evm() const;
};

std::ostream& operator <<(std::ostream& os, const Frame& f);

class FrameDetector {
    static constexpr int samples_per_symbol = SAMPLES_PER_SYMBOL;

    // preamble matching
    static inline const Preamble<uint16_t> p_openstint { transponder_props(TransponderType::OpenStint).bpsk_preamble };
    static inline const Preamble<uint16_t> p_legacy { transponder_props(TransponderType::Legacy).bpsk_preamble };
    CircBuff<uint16_t> buffers[samples_per_symbol];
    float threshold;

    // stream statistics:
    std::complex<int8_t> offset= {0, 0}; // dc offset ~ sample mean
    float variance = 0; // ~noise power (expected value squared after dc offset removal)
    
    // statistic calculation:
    std::complex<int32_t> s1 = {0, 0}; // sum of samples
    uint32_t s2 = 0; // sum of sample squared
    int n = 0; // number of samples measured
public:
    FrameDetector(float threshold);

    std::optional<TransponderType> process_baseband(const std::complex<int8_t> *samples);
    void update_statistics();
    void reset_statistics_counters();

    float symbol_energy() const;
    float noise_energy() const;
    std::complex<int8_t> dc_offset() const;
};

class SymbolReader {
public:
    static constexpr int samples_per_symbol = SAMPLES_PER_SYMBOL;
    static constexpr int filter_delay = 4;
    static constexpr int num_filters = 16 / samples_per_symbol;
    static constexpr int preamble_length = 16;
    static constexpr int reserve_buffer_size = preamble_length * samples_per_symbol;

private:
    firpfb_crcf sym_pfb; // preprocessing - polyphase filter bank
    eqlms_cccf sym_eq;   // equalizer, trained on preamble data
    modemcf bpsk_modem;

    // reading a matched preamble might require lookback into
    // the previous buffer. this contain the last section of
    // the previous buffer
    std::complex<int8_t> reserve_buffer[reserve_buffer_size];

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
    void read_single(Frame *dst, const std::complex<int8_t> offset, const std::complex<int8_t> *src);
    void read_preamble_symbol(std::complex<float> *dst, std::complex<float> symbol);
    void train_preamble(Frame *dst, std::complex<int8_t> offset, const std::complex<int8_t> *src, int end);
};
