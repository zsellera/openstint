#pragma once

#include <cstdint>
#include <complex>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

#include "summing_buffer.hpp"

#include "transponder.hpp"
#include "preamble.hpp"

#include <liquid/liquid.h>

#define ADC_FULL_SCALE 179.0f // 127*1.41 (max vector magnitude of the two adcs)

#ifndef SAMPLES_PER_SYMBOL
#define SAMPLES_PER_SYMBOL 4
#endif

#ifndef SYMBOL_RATE
#define SYMBOL_RATE 1250000
#endif

#ifndef SAMPLE_RATE
#define SAMPLE_RATE (SYMBOL_RATE * SAMPLES_PER_SYMBOL)
#endif

struct Frame {
    TransponderProtocol transponder_protocol; // what kind of preamble was matched
    uint32_t preamble_size;
    uint32_t payload_size;

    // bitstream decision probabilities for soft-decoding
    // 0..127..255 <=> totally 0 ... unknown ... totally 1
    std::vector<uint8_t> softbits;
    // actual symbols
    std::vector<std::complex<float>> symbols;
    // decoding error accumulator
    float evm_sum = 0;

    // frame timing, 2 types of time is tracked:
    // - timestamp is an OS-provided steady-time, subject to scheduler's jitter
    // - timecode is the number of IQ samples since startup, subject to buffer underruns
    // the clocks run at different rates, a few ppm differece is expected
    uint64_t timestamp; // steady time
    uint64_t timecode;  // sample counter
    
    // based on preamble-data
    float symbol_scale = 0;
    float phase = 0;
    float phase_per_symbol = 0; // radian/symbol

    Frame();
    Frame(TransponderProtocol transponder_protocol, uint64_t timestamp, uint64_t timecode);

    const uint8_t* bits();
    float rssi() const;
    float evm() const;
    float symbol_magnitude() const;
};

std::ostream& operator <<(std::ostream& os, const Frame& f);

class FrameDetector {
    static constexpr int samples_per_symbol = SAMPLES_PER_SYMBOL;

    // preamble matching
    static inline const Preamble<uint16_t> p_openstint { transponder_props(TransponderProtocol::OpenStint).dpsk_preamble };
    static inline const Preamble<uint16_t> p_rc3 { transponder_props(TransponderProtocol::RC3).dpsk_preamble };
    static inline const Preamble<uint16_t> p_rc4 { transponder_props(TransponderProtocol::RC4).dpsk_preamble };

    // preamble detection - dynamic threshold
    const float threshold_low;
    const float threshold_high;
    float threshold;

    std::complex<int32_t> last_samples[samples_per_symbol] = {0};
    CircBuff<uint16_t> buffers[samples_per_symbol];
    
    // stream statistics:
    std::complex<int32_t> offset= {0, 0}; // dc offset ~ sample mean
    std::complex<float> offset_hires = { 0.0f, 0.0f }; // dc offset
    float variance = 0; // ~noise power (expected value squared after dc offset removal)
    
    // statistic calculation:
    std::complex<int32_t> s1 = {0, 0}; // sum of samples
    uint32_t s2 = 0; // sum of sample squared
    int n = 0; // number of samples measured
public:
    FrameDetector(float threshold_low, float threshold_high);

    std::optional<TransponderProtocol> process_baseband(const std::complex<int8_t> *samples);
    void update_statistics();
    void reset_statistics_counters();

    float symbol_energy() const;
    float noise_energy() const;
    std::complex<float> dc_offset() const;
    float dynamic_threshold() const;
};

class SymbolReader {
public:
    static constexpr int samples_per_symbol = SAMPLES_PER_SYMBOL;
    static constexpr int fseq_halflen = 1;                       // symbols of past/future context
    static constexpr int fseq_syms = 2 * fseq_halflen + 1;       // total filter span (symbols)
    static constexpr int preamble_length = 16;
    // window = fseq_halflen lead + preamble + fseq_halflen trailing (future) symbols
    static constexpr int preamble_symbol_count = preamble_length + 2 * fseq_halflen;
    static constexpr int preamble_buffer_size = preamble_symbol_count * samples_per_symbol;
    static constexpr int reserve_buffer_size = preamble_buffer_size;
    
    static constexpr float eq_mu_train = 0.05f * samples_per_symbol;
    static constexpr float eq_mu_track = eq_mu_train * 2.0f;
    static constexpr float costas_p = 0.020f;
    static constexpr float costas_i = 0.002f;

private:
    eqlms_cccf sym_eq;   // equalizer, trained on preamble data
    modemcf bpsk_modem;

    // reading a matched preamble might require lookback into
    // the previous buffer. this contain the last section of
    // the previous buffer
    std::complex<int8_t> reserve_buffer[reserve_buffer_size];

    // when a preamble is matched, copy received data here for further processing:
    // - the centered EQ filter needs fseq_halflen lead + fseq_halflen trailing symbols
    // - there is the preamble (16 symbols)
    std::complex<float> preamble_buffer[preamble_buffer_size];

public:
    SymbolReader();
    ~SymbolReader();
    // manages external resources (liquid) with uncopyable internal state:
    SymbolReader(const SymbolReader&) = delete;
    SymbolReader(SymbolReader&&) noexcept = delete;
    SymbolReader& operator=(const SymbolReader&) = delete;
    SymbolReader& operator=(SymbolReader&&) noexcept = delete;
    
    void train_preamble(Frame *dst, const std::complex<int8_t> *src, int end, std::complex<float> dc_offset);
    void read_preamble(Frame *dst, const std::complex<int8_t> *src, int end, std::complex<float> dc_offset);
    void read_symbol(Frame *dst, const std::complex<int8_t> *src, std::complex<float> dc_offset);
    void update_reserve_buffer(const std::complex<int8_t> *src, int end);
    bool is_frame_complete(const Frame *f);

private:
    void costas_tune_correction(Frame *frame, std::complex<float> symbol);
    void load_preamble_buffer(const std::complex<int8_t> *src, int end, std::complex<float> dc_offset);
    std::pair<float, float> estimate_phase_freq(Frame *frame, int shift = 2);
    void train_fseq(Frame *frame, float mu);
};
