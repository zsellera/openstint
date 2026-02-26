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
#define EQLMS_TRAINING_THRESHOLD 16.0f

Frame::Frame() {
    preamble_size = payload_size = 0;
    softbits.reserve(FRAME_MAX_SYMBOL_SPACE);
    symbols.reserve(FRAME_MAX_SYMBOL_SPACE);
    timestamp = 0;
}

Frame::Frame(TransponderType _ttype, uint64_t _ts)
    : transponder_type(_ttype), timestamp(_ts) {
    softbits.reserve(FRAME_MAX_SYMBOL_SPACE);
    symbols.reserve(FRAME_MAX_SYMBOL_SPACE);
    payload_size = transponder_props(transponder_type).payload_size;
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
    int pos = preamble_pos(sof, transponder_props(transponder_type).bpsk_preamble);
    if (pos < 0) {
        // try with bits inverted:
        pos = preamble_pos(~sof, transponder_props(transponder_type).bpsk_preamble);
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
    return evm_sum / (payload_size + SymbolReader::filter_delay);
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
    return os << transponder_props(f.transponder_type).prefix
              << " T:" << f.timestamp
              << " RSSI:" << f.rssi()
              << " EVM:" << f.evm()
              << " FREQ:" << (f.frequency / (2.0f * 3.14) * 1250000.0f)
              << " MAG:" << f.symbol_magnitude()
              << " SYMSYNC:[" << f.symsync_sym << "," << f.symsync_bank << "]"
              << " SYMBOLS:[" << ssym.str() << "]"
              << " SOFTBITS:[" << sbits.str() << "]";
}

FrameDetector::FrameDetector(float _threshold) : threshold(_threshold) {};

std::optional<TransponderType> FrameDetector::process_baseband(const std::complex<int8_t> *samples) {
    // remove dc-offset + calculate magninude^2 of each sample
    std::complex<int8_t> sb[samples_per_symbol];
    uint16_t mag2s[samples_per_symbol];
    std::transform(
        samples, samples + samples_per_symbol,
        sb, [this](const std::complex<int8_t> s) { return s - this->offset; }
    );
    std::transform(
        sb, sb + samples_per_symbol,
        mag2s,
        [](const std::complex<int8_t> s) { return std::norm(complex_cast<int16_t>(s)); }
    );

    // run SAMPLES_PER_SYMBOL circular buffers in parallel
    for (int i=0; i<samples_per_symbol; i++) {
        buffers[i].push(sb[i], mag2s[i]);
    }

    // select the best-looking buffer to compute preamble-match
    uint32_t wes[samples_per_symbol];
    for (int i=0; i<samples_per_symbol; i++) { 
        wes[i] = buffers[i].window_energy;
    }
    int idx = std::distance(wes, std::max_element(wes, wes+samples_per_symbol)); // ~maxarg

    // update statistics (sample first element)
    s1 += samples[0];
    s2 += mag2s[0];
    n++;

    // run matchers
    if (buffers[idx].match_preamble(p_openstint) > threshold) {
        return TransponderType::OpenStint;
    }
    if (buffers[idx].match_preamble(p_legacy) > threshold) {
        return TransponderType::Legacy;
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
    sym_pfb = firpfb_crcf_create_default(num_filters, filter_delay);
    sym_eq = eqlms_cccf_create(NULL, 3); // a very quick EQ
    eqlms_cccf_set_bw(sym_eq, 1.0f/64);
    bpsk_modem = modemcf_create(LIQUID_MODEM_BPSK);
}

SymbolReader::~SymbolReader() {
    firpfb_crcf_destroy(sym_pfb);
    eqlms_cccf_destroy(sym_eq);
    modemcf_destroy(bpsk_modem);
}

void SymbolReader::read_single(Frame *frame, const std::complex<float> offset, const std::complex<int8_t> *src) {
    // convert i8 to f32
    std::complex<float> bf[samples_per_symbol];
    for (int i=0; i<samples_per_symbol; ++i) { // something like vcvt_s32_f32
        bf[i] = complex_cast<float>(src[i]) - offset;
    }
    // sample the symbol (symbol sync)
    for (int i=0; i<samples_per_symbol; i++) {
        firpfb_crcf_push(sym_pfb, bf[i]);
        if (i == frame->symsync_sym) {
            std::complex<float> symbol;
            firpfb_crcf_execute(sym_pfb, frame->symsync_bank, &symbol);

            symbol *= frame->correction;
            costas_tune_correction(frame, symbol);

            // equalization
            eqlms_cccf_push(sym_eq, symbol);
            eqlms_cccf_execute(sym_eq, &symbol);

            // demodulate
            unsigned int bit; // bit-level decoding
            uint8_t soft_bit; // how likely the symbol is
            modemcf_demodulate_soft(bpsk_modem, symbol, &bit, &soft_bit);
            frame->softbits.push_back(soft_bit);
            frame->symbols.push_back(symbol);
            frame->evm_sum += modemcf_get_demodulator_evm(bpsk_modem);
        }
    }
}

void SymbolReader::read_preamble_symbol(std::complex<float> *dst, std::complex<float> symbol) {
    std::complex<float> s;
    firpfb_crcf_push(sym_pfb, symbol);
    for (unsigned int l=0; l<num_filters; l++) {
        firpfb_crcf_execute(sym_pfb, l, &s);
        dst[l] = s;
    }
}

void SymbolReader::train_preamble(Frame *frame, std::complex<float> offset, const std::complex<int8_t> *src, int end) {
    const int sample_count = preamble_length * samples_per_symbol;
    const int resampled_size = preamble_length * 16; // resample to 16 samples/symbol
    std::complex<float> resampled[resampled_size];

    // read symbols to resampled buffer
    for (int i=0; i<sample_count; i++) {
        int sample_idx = end - sample_count + i;
        if (sample_idx < 0) {
            sample_idx += reserve_buffer_size;
            std::complex<float> symbol = complex_cast<float>(reserve_buffer[sample_idx]) - offset;
            read_preamble_symbol(resampled + i*num_filters, symbol);
        } else {
            std::complex<float> symbol = complex_cast<float>(src[sample_idx]) - offset;
            read_preamble_symbol(resampled + i*num_filters, symbol);
        }
    }
    
    // find best sampling point (where sample magnitude is max):
    // process (preamble_length - filter_delay) symbols (typ. 12)
    float preamble_magnitudes[16] = {0};
    for (int i=(filter_delay*16); i<resampled_size; i++) {
        preamble_magnitudes[i%16] += std::norm(resampled[i]);
    }
    auto max_magnitude = std::max_element(preamble_magnitudes, preamble_magnitudes + 16);
    int sampling_point = std::distance(preamble_magnitudes, max_magnitude);

    // find symbol phase
    std::complex<float> rotated2x0 = {0}, rotated2x1 = {0};
    for (int i=filter_delay; i<preamble_length; i++) {
        auto sampled_symbol = resampled[i*16 + sampling_point];
        auto rr = (sampled_symbol*sampled_symbol);
        // collect phases for first and second halves separately (for frequency offset guesstimation):
        if (i < (preamble_length+filter_delay)/2) {
            rotated2x0 += rr;
        } else {
            rotated2x1 += rr;
        }
    }

    // update frame
    frame->symsync_sym = sampling_point / num_filters;
    frame->symsync_bank = sampling_point % num_filters;
    frame->symbol_scale = 1.0f / std::sqrt((*max_magnitude) / static_cast<float>(preamble_length));
    frame->phase = std::arg(rotated2x0 + rotated2x1) / 2.0f;
    frame->correction = std::polar(frame->symbol_scale, -frame->phase);
    // frequency offset estimation: average the phase of first and second half of the preamble,
    // and find the angle between the two averages. The arg(z0*conj(z1)) is a "known trick" to find
    // the angle. Since it's BPSK, we use the arg(z*z)/2 to have both symbols in a single point.
    // There is (preamble_length-filter_delay)/2 time between the two averages, 2/2 cancels out.
    frame->frequency = std::arg(rotated2x1 * std::conj(rotated2x0)) / (preamble_length - filter_delay);

    // train EQ on preamble
    if ((1.0f / frame->symbol_scale) > EQLMS_TRAINING_THRESHOLD) {
        for (int i=0; i<preamble_length; i++) {
            std::complex<float> symbol = resampled[i*16 + sampling_point] * frame->correction;
            std::complex<float> d_hat, d_prime;
            unsigned int bit; // bit-level decoding

            eqlms_cccf_push(sym_eq, symbol);
            if (i >= filter_delay) {
                eqlms_cccf_execute(sym_eq, &d_hat);
                modemcf_demodulate(bpsk_modem, d_hat, &bit);
                modemcf_get_demodulator_sample(bpsk_modem, &d_prime);
                eqlms_cccf_step(sym_eq, d_prime, d_hat);
            }
        }
    }

    // read preamble to frame (with initial filter response)
    for (int i=0; i<preamble_length; i++) {
        std::complex<float> symbol = resampled[i*16 + sampling_point] * frame->correction;
        costas_tune_correction(frame, symbol);

        // run EQ:
        eqlms_cccf_push(sym_eq, symbol);
        eqlms_cccf_execute(sym_eq, &symbol);

        // demodulate:
        unsigned int bit; // bit-level decoding
        uint8_t soft_bit; // how likely the symbol is
        modemcf_demodulate_soft(bpsk_modem, symbol, &bit, &soft_bit);
        frame->softbits.push_back(soft_bit);
        frame->symbols.push_back(symbol);
    }
}

void SymbolReader::read_preamble(Frame *dst, std::complex<float> offset, const std::complex<int8_t> *src, int end) {
    firpfb_crcf_reset(sym_pfb);
    modem_reset(bpsk_modem);

    // find optimal sampling point, AGC scale and BPSK stating phase
    train_preamble(dst, offset, src, end);
}

void SymbolReader::update_reserve_buffer(const std::complex<int8_t> *src, int end) {
    std::memcpy(
        reserve_buffer,
        src + end - reserve_buffer_size,
        reserve_buffer_size * sizeof(std::complex<uint8_t>)
    );
}

void SymbolReader::read_symbol(Frame *dst, std::complex<float> offset, const std::complex<int8_t> *src) {
    read_single(dst, offset, src);
}

bool SymbolReader::is_frame_complete(const Frame *f) {
    // we read the preamble + -1th bit to initialize differential-BPSK demodulation
    // payload, obviously
    // the symbol-sync's filters has their own delay
    return f->softbits.size() > (f->preamble_size + f->payload_size + filter_delay);
}

void SymbolReader::costas_tune_correction(Frame *frame, std::complex<float> symbol) {
    float error = std::arg(symbol*symbol) / 2.0f; // phase; slower than real*imag, but much better
    frame->frequency += 0.0025f * error;
    frame->phase += frame->frequency + 0.05f * error;
    frame->correction = std::polar(
        frame->symbol_scale,
        -(frame->phase)
    );
}
