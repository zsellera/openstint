#include "frame.hpp"
#include "transponder.hpp"

#include <bit>
#include <complex>
#include <cstring>
#include <iterator>
#include <algorithm>
#include <iostream>

#include "complex_cast.hpp"

#define FRAME_MAX_SYMBOL_SPACE 128
#define PREAMBLE_MAX_BIT_ERRORS 2
#define STATS_UPDATE_THRESHOLD (1<<12)

#define PREAMBLE_THRESHOLD 0.68f
#define PREAMBLE_15BIT_PENALTY (15.0f/16.0f)

// preamble matching
static inline const Preamble<uint16_t> p_openstint(transponder_props(TransponderProtocol::OpenStint).dpsk_preamble, PREAMBLE_THRESHOLD*PREAMBLE_15BIT_PENALTY);
static inline const Preamble<uint16_t> p_rc3(transponder_props(TransponderProtocol::RC3).dpsk_preamble, PREAMBLE_THRESHOLD*PREAMBLE_15BIT_PENALTY);
static inline const Preamble<uint16_t> p_rc4(transponder_props(TransponderProtocol::RC4).dpsk_preamble, PREAMBLE_THRESHOLD);


Frame::Frame() {
    preamble_size = payload_size = 0;
    softbits.reserve(FRAME_MAX_SYMBOL_SPACE);
    symbols.reserve(FRAME_MAX_SYMBOL_SPACE);
    timestamp = 0;
    timecode = 0;
    preamble_metric = 0;
}

Frame::Frame(TransponderProtocol _ttype, float _pm, uint64_t _ts, uint64_t _tc)
    : transponder_protocol(_ttype), preamble_metric(_pm), timestamp(_ts), timecode(_tc) {
    softbits.reserve(FRAME_MAX_SYMBOL_SPACE);
    symbols.reserve(FRAME_MAX_SYMBOL_SPACE);
    payload_size = transponder_props(transponder_protocol).payload_size;
    preamble_size = 16;
}

uint32_t concat_bits32(uint8_t *soft_bits) {
    uint32_t v = 0;
    for (int i=0; i<32; i++) {
        v <<= 1;
        if (soft_bits[i] & 0x80) {
            v |= 1u;
        }
    }
    return v;
}

int preamble_pos(uint32_t sof, uint16_t preamble) {
    const int preamble_size = 16;
    const uint32_t mask = (1 << preamble_size) - 1;

    uint32_t pp = static_cast<uint32_t>(preamble);
    for (int i=0; i<=(32-preamble_size); i++) {
        uint32_t result = (sof & mask) ^ pp;
        if (std::popcount(result) <= PREAMBLE_MAX_BIT_ERRORS) {
            return 32 - preamble_size - i;
        } else {
            sof >>= 1;
        }
    }
    return -1;
}

const uint8_t* Frame::bits() {
    if (softbits.size() < 32) {
        return nullptr;
    }

    // start-of-frame 32 bits contain the preamble
    uint32_t sof = concat_bits32(softbits.data());
    int pos = preamble_pos(sof, transponder_props(transponder_protocol).preamble);
    if (pos < 0) {
        // try with bits inverted:
        pos = preamble_pos(~sof, transponder_props(transponder_protocol).preamble);
        if (pos < 0) { // preamble not found
            return nullptr;
        } else { // preamble found, but BPSK does not know the correct phase
            std::transform(
                softbits.begin(), softbits.end(),
                softbits.begin(),
                [](uint8_t x) { return 0xff-x; }
            );
        }
    }
    if (softbits.size() < pos + preamble_size + payload_size) {
        // could not read enough bits (this should be an exception btw...)
        return nullptr;
    }

    return softbits.data() + pos + preamble_size;
}

float Frame::rssi() const {
    return 20.0f * std::log10(symbol_magnitude()) - 20.f * std::log10(ADC_FULL_SCALE);
}

float Frame::evm() const {
    return evm_sum / (payload_size + preamble_size + SymbolReader::fseq_syms);
}

float Frame::symbol_magnitude() const {
    return 1.0f / symbol_scale;
}

std::ostream& operator <<(std::ostream& os, const Frame& f) {
    std::stringstream ssym;
    std::transform(f.symbols.begin(), f.symbols.end(),
        std::ostream_iterator<std::string>(ssym, ", "),
        [](const std::complex<float>& c) {
            std::stringstream ss;
            float re = c.real(), im = c.imag();
            if (re != 0 || im == 0) ss << re;
            if (im != 0) {
                if (im >= 0 && re != 0) ss << "+";
                ss << im << "j";
            }
            return ss.str();
        });
    std::stringstream sbits;
    std::copy(f.softbits.begin(), f.softbits.end(), std::ostream_iterator<int>(sbits, ", "));
    return os << transponder_props(f.transponder_protocol).prefix
              << " TS:" << (f.timestamp/1000)
              << " TC:" << f.timecode
              << " M:" << f.preamble_metric
              << " RSSI:" << f.rssi()
              << " EVM:" << f.evm()
              << " FREQ:" << (f.phase_per_symbol / (2.0f * 3.14) * 1250000.0f)
              << " MAG:" << f.symbol_magnitude()
              << " SYMBOLS:[" << ssym.str() << "]"
              << " SOFTBITS:[" << sbits.str() << "]";
}

std::optional<DetectionResult> FrameDetector::process_baseband(const std::complex<int8_t> *samples) {
    // Preamble detection works on differential-encoded signals;
    // This is tolerant to larger frequency offsets.
    // 
    // To differentially demodulate: z[i] = r[i] * conj(r[i-1])
    // In Euler-form, a received symbol is:
    // r[t] = A*e^{j(φ+Δω)t}*c[t]
    //   - c[t] ∈ {±1}
    //   - c[t] = c[t-1]*a[t] (differential encoding, a[t] is the preabmble pre-encoding, ∈{±1})
    //   - φ, Δω: carrier initial phase, per-symbol offset
    // Note: unknow φ (receiver's local oscillator isn't phase-locked to the transmitter),
    //       and Δω ≠ 0 because the two crystals never match exactly
    // Note: a[t] = c[t]*c[t-1] (given the ∈{±1})
    // 
    // r[t] * r'[t-1] = (A*e^{j(φ+Δω)t}*c[t]) * (A*e^{-j(φ+Δω)(t-1)}*c[t-1])
    //                = A^2 * e^{jΔω} * c[t] * c[t-1]
    //                = A^2 * e^{jΔω} * a[t]
    // For small Δω, e^{jΔω}~=1; the conjugate product cancels the (unknown) carrier phase
    // and removes the per-symbol rotation from any frequency offset, leaving a practically
    // real-valued ±|A|^2 sequence, that is the differentially-encoded preamble bit pattern.
    std::complex<int32_t> r[samples_per_symbol];
    for (int i=0; i<samples_per_symbol; i++) {
        r[i] = complex_cast<int32_t>(samples[i]) - offset;
    }
    for (int i=0; i<samples_per_symbol; i++) {
        std::complex<int32_t> z = r[i] * std::conj(last_samples[i]);
        int32_t zr = std::clamp(std::real(z), (int32_t)INT16_MIN, (int32_t)INT16_MAX);
        buffers[i].push(static_cast<int16_t>(zr), zr*zr);
    }
    for (int i=0; i<samples_per_symbol; i++) {
        last_samples[i] = r[i];
    }

    // select the best-looking buffer to compute preamble-match
    uint32_t wes[samples_per_symbol];
    for (int i=0; i<samples_per_symbol; i++) { 
        wes[i] = buffers[i].window_energy;
    }
    int idx = std::distance(wes, std::max_element(wes, wes+samples_per_symbol)); // ~maxarg

    // update statistics (sample first element)
    s1 += samples[0];
    s2 += std::norm(r[0]);
    n++;
    
    if (buffers[idx].match_preamble(p_rc4)) {       
        return {{ TransponderProtocol::RC4, buffers[idx].calc_metric(p_rc4) }};
    }

    // different manufacturers use different init sequence, DPSK first bit differs!
    // depending on threshold, we could loose 25-50% of messages with the wrong preamble!
    // fix: set msb to 0, sligthly lower threshold, match rc3 last to prevent early false-match
    buffers[idx].clear_next(); // do not use MSB for matching

    // v1 transponder use the correct init sequence (-1 -1 -1 -1)
    // v2-beta used incorrect; to keep those tranponders alive, match on 15 bits only
    // new transpoders are fixed, this affects ~5 team/people
    if (buffers[idx].match_preamble(p_openstint)) {
        return {{ TransponderProtocol::OpenStint, buffers[idx].calc_metric(p_openstint) }};
    }

    // - AmbRC/RCHG/MRT use 0xF916 dpsk preamble
    // - RC4Hybrid use 0x7916
    if (buffers[idx].match_preamble(p_rc3)) {
        return {{ TransponderProtocol::RC3, buffers[idx].calc_metric(p_rc3) }};
    }
    return std::nullopt;
}

void FrameDetector::update_statistics() {
    if (n > STATS_UPDATE_THRESHOLD) {
        offset = complex_cast<int8_t>(s1 / n);
        offset_hires = complex_cast<float>(s1) / static_cast<float>(n);
        variance = static_cast<float>(s2) / (n - 1); // sample's variance (vs population variance)
        reset_statistics_counters();
    }
}

void FrameDetector::reset_statistics_counters() {
    s1 = std::complex<int32_t>(0, 0);
    s2 = 0;
    n = 0;
}

float FrameDetector::symbol_energy() const {
    uint32_t max_energy = buffers[0].window_energy;
    for (int i=1; i<samples_per_symbol; i++) {
        if (buffers[i].window_energy > max_energy) {
            max_energy = buffers[i].window_energy;
        }
    }
    // there are 16 symbols in each buffer
    return static_cast<float>(max_energy) / 16.0f;
}

float FrameDetector::noise_energy() const {
    return variance;
}

std::complex<float> FrameDetector::dc_offset() const {
    return offset_hires;
}

SymbolReader::SymbolReader() {
    std::complex<float> h[fseq_syms * samples_per_symbol] = {0};
    sym_eq = eqlms_cccf_create(h, fseq_syms * samples_per_symbol);
    // sym_eq = eqlms_cccf_create_lowpass(fseq_syms * samples_per_symbol, 0.5f);
    bpsk_modem = modemcf_create(LIQUID_MODEM_BPSK);
}

SymbolReader::~SymbolReader() {
    eqlms_cccf_destroy(sym_eq);
    modemcf_destroy(bpsk_modem);
}

template <int N>
float window_energy(const std::complex<float> x[]) {
    float acc = {0};
    for (int i=0; i<N; i++) {
        acc += std::norm(x[i]);
    }
    return acc;
}

void SymbolReader::train_preamble(Frame *frame, const std::complex<int8_t> *src, int end, std::complex<float> dc_offset) {
    // load from SDR buffer to an internal one
    load_preamble_buffer(src, end, dc_offset);

    // setup AGC based on preamble
    frame->symbol_scale = 1.41f / std::sqrt(window_energy<preamble_buffer_size>(preamble_buffer)/static_cast<float>(preamble_symbol_count));

    // the preamble starts after the fseq_halflen lead symbols reserved for the EQ
    auto [phase0, phase_per_symbol] = estimate_phase_freq(frame, 2 /*shift*/);
    frame->phase = phase0 - phase_per_symbol*fseq_halflen; // set to init sequence
    frame->phase_per_symbol = phase_per_symbol;

    // scale & rotate buffer (do it once, so EQ training is faster)
    for (int i=0; i<preamble_buffer_size; i++) {
        float k = static_cast<float>(i) / samples_per_symbol - static_cast<float>(fseq_halflen);
        preamble_buffer[i] *= std::polar(frame->symbol_scale, -k*phase_per_symbol - phase0);
    }
    
    // train EQ filter
    eqlms_cccf_reset(sym_eq); // reset the original parameters
    train_fseq(frame, eq_mu_train*3.0f);
    train_fseq(frame, eq_mu_train);
    train_fseq(frame, eq_mu_train);

    // normal operation
    eqlms_cccf_set_bw(sym_eq, eq_mu_track);
}

void SymbolReader::load_preamble_buffer(const std::complex<int8_t> *src, int end, std::complex<float> dc_offset) {
    // read symbols to resampled buffer
    for (int i=0; i<preamble_buffer_size; i++) {
        int sample_idx = end - preamble_buffer_size + i;
        if (sample_idx < 0) {
            // do not read before the current buffer, use the reserve from the previous
            sample_idx += reserve_buffer_size;
            preamble_buffer[i] = complex_cast<float>(reserve_buffer[sample_idx]) - dc_offset;
        } else {
            preamble_buffer[i] = complex_cast<float>(src[sample_idx]) - dc_offset;
        }
    }
}

std::pair<float, float> SymbolReader::estimate_phase_freq(Frame *frame, int shift) {
    const int n = preamble_length * samples_per_symbol;
    const int start = fseq_halflen * samples_per_symbol;
    const auto &preamble_up = transponder_props(frame->transponder_protocol).preamble_up;

    // y = sample-spaced preamble with the BPSK (±1) modulation stripped off
    // multiply by known preamble rotates symbols to a single point (per sample/phase)
    std::complex<float> y[n];
    for (int i=0; i<n; i++) {
        y[i] = preamble_buffer[start + i] * preamble_up[i];
    }

    // frequency estimate: angle of the lag-(samples_per_symbol*shift) autocorrelation, per symbol
    std::complex<float> acc = {0.0f, 0.0f};
    for (int i=samples_per_symbol*shift; i<n; i++) {
        acc += y[i] * std::conj(y[i - samples_per_symbol*shift]);
    }
    float dphi = std::arg(acc) / static_cast<float>(shift); // rad / symbol

    // phase at 'start': derotate by the estimated frequency, then take the overall angle
    std::complex<float> psum = {0.0f, 0.0f};
    for (int i=0; i<n; i++) {
        float k = static_cast<float>(i) / samples_per_symbol;
        psum += y[i] * std::polar(1.0f, -k * dphi);
    }
    float ph0 = std::arg(psum);

    return {ph0, dphi};
}

void SymbolReader::train_fseq(Frame *frame, float mu) {
    const auto &preamble_syms = transponder_props(frame->transponder_protocol).preamble_syms;

    // set the LMS learning rate for this epoch
    eqlms_cccf_set_bw(sym_eq, mu);

    // prime the filter with a full window (fseq_syms symbols): fseq_halflen lead +
    // the first preamble symbol + fseq_halflen trailing, so the first execute()
    // output is centered on the first known preamble symbol
    int idx = 0;
    for (int i=0; i<fseq_syms * samples_per_symbol; i++) {
        eqlms_cccf_push(sym_eq, preamble_buffer[idx++]);
    }

    // from here on, advance one symbol (all samples_per_symbol samples) at a time,
    // equalize, and train towards the known preamble symbol; the window stays
    // centered on the symbol being trained
    for (int s=0; s<preamble_length; s++) {
        if (s > 0) {
            for (int j=0; j<samples_per_symbol; j++) {
                eqlms_cccf_push(sym_eq, preamble_buffer[idx++]);
            }
        }
        std::complex<float> d_hat;
        eqlms_cccf_execute(sym_eq, &d_hat);
        eqlms_cccf_step(sym_eq, preamble_syms[s], d_hat);
    }
}

void SymbolReader::read_preamble(Frame *frame, const std::complex<int8_t> *src, int end, std::complex<float> dc_offset) {
    // read preamble as regular data
    const int sample_count = preamble_symbol_count * samples_per_symbol;
    for (int i=0; i<preamble_symbol_count; i++) {
        int sample_idx = end - sample_count + i*samples_per_symbol;
        if (sample_idx < 0) {
            // do not read before the current buffer, use the reserve from the previous
            sample_idx += reserve_buffer_size;
            read_symbol(frame, reserve_buffer+sample_idx, dc_offset);
        } else {
            read_symbol(frame, src+sample_idx, dc_offset);
        }
    }
}

void SymbolReader::read_symbol(Frame *frame, const std::complex<int8_t> *src, std::complex<float> dc_offset) {
    // scale & derotate this symbol's samples (same normalization the EQ was
    // trained with): the carrier phase advances by phase_per_symbol/samples_per_symbol
    // for every sample. Then feed them to the fractionally-spaced equalizer.
    const float phase_step = frame->phase_per_symbol / samples_per_symbol;
    for (int i=0; i<samples_per_symbol; i++) {
        std::complex<float> correction = std::polar(frame->symbol_scale, -(frame->phase + i*phase_step));
        std::complex<float> sample = (complex_cast<float>(src[i]) - dc_offset) * correction;
        eqlms_cccf_push(sym_eq, sample);
    }

    // downsample: the EQ produces one equalized symbol per samples_per_symbol
    std::complex<float> symbol;
    eqlms_cccf_execute(sym_eq, &symbol);

    // closed-loop carrier tracking on the equalized symbol
    costas_tune_correction(frame, symbol);

    // soft demodulate and store
    unsigned int bit; // bit-level decoding
    uint8_t soft_bit; // how likely the symbol is
    modemcf_demodulate_soft(bpsk_modem, symbol, &bit, &soft_bit);
    frame->softbits.push_back(soft_bit);
    frame->symbols.push_back(symbol);
    frame->evm_sum += modemcf_get_demodulator_evm(bpsk_modem);

    // decision-directed (blind) EQ update toward the demodulated symbol
    std::complex<float> d_prime;
    modemcf_get_demodulator_sample(bpsk_modem, &d_prime);
    eqlms_cccf_step(sym_eq, d_prime, symbol);
}

void SymbolReader::update_reserve_buffer(const std::complex<int8_t> *src, int end) {
    std::memcpy(
        reserve_buffer,
        src + end - reserve_buffer_size,
        reserve_buffer_size * sizeof(std::complex<uint8_t>)
    );
}

bool SymbolReader::is_frame_complete(const Frame *f) {
    // we read the preamble + -1th bit to initialize differential-BPSK demodulation
    // payload, obviously
    // the symbol-sync's filters has their own delay
    return f->softbits.size() > (f->preamble_size + f->payload_size + fseq_syms);
}

void SymbolReader::costas_tune_correction(Frame *frame, std::complex<float> symbol) {
    float error = std::arg(symbol*symbol) / 2.0f; // phase; slower than real*imag, but much better
    frame->phase_per_symbol += costas_i * error;
    frame->phase += frame->phase_per_symbol + costas_p * error;
}
