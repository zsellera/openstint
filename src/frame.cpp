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


Frame::Frame() {
    preamble_size = payload_size = 0;
    softbits.reserve(FRAME_MAX_SYMBOL_SPACE);
    timestamp = 0;
    symbol_magnitude = evm_sum = 0;
}

Frame::Frame(TransponderType _ttype, uint64_t _ts, float preamble_energy)
    : transponder_type(_ttype), timestamp(_ts), evm_sum(0) {
    softbits.reserve(FRAME_MAX_SYMBOL_SPACE);
    payload_size = transponder_props(transponder_type).payload_size;
    preamble_size = 16;
    // note here: the preamble detector works with peak symbols, hence
    // practically with peak-to-peak values. preamble_energy is amplitude^2
    // for normalization post-symsync, we're better off with RMS, hence this transform
    symbol_magnitude = std::sqrt(preamble_energy) / std::sqrt(2.0f); // amplitude^2 to RMS
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
    // symbol_magnitude is in RMS
    return 20.0f * std::log10(symbol_magnitude / ADC_FULL_SCALE);
}

float Frame::evm() const {
    return evm_sum / softbits.size();
}

std::ostream& operator <<(std::ostream& os, const Frame& f) {
    std::stringstream s;
    std::copy(f.softbits.begin(), f.softbits.end(), std::ostream_iterator<int>(s, ", "));
    return os << transponder_props(f.transponder_type).prefix << " T:" << f.timestamp << " RSSI:" << f.rssi() << " EVM:" << f.evm() << " [" << s.str() << "]";
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

    // run 4 circular buffers in parallel
    for (int i=0; i<samples_per_symbol; i++) {
        buffers[i].push(sb[i], mag2s[i]);
    }

    // select the best-looking buffer to compute preamble-match
    uint32_t wes[samples_per_symbol];
    for (int i=0; i<samples_per_symbol; i++) { 
        wes[i] = buffers[i].window_energy;
    }
    int idx = std::distance(wes, std::max_element(wes, wes+4)); // ~maxarg

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
    uint32_t wes[4] = { buffers[0].window_energy, buffers[1].window_energy, buffers[2].window_energy, buffers[3].window_energy }; 
    int idx = std::distance(wes, std::max_element(wes, wes+4)); // ~maxarg

    return static_cast<float>(buffers[idx].window_energy) / 16.0f;
}

float FrameDetector::noise_energy() const {
    return variance;
}

std::complex<int8_t> FrameDetector::dc_offset() const {
    return offset;
}

SymbolReader::SymbolReader() {
    symsync = symsync_crcf_create_rnyquist(
        LIQUID_FIRFILT_RRC,
        samples_per_symbol,
        filter_delay, // filter length (delay!)
        0.5f, // filter excess bandwidth
        16 // number of polyphase filters in bank
    );
    bpsk_modem = modemcf_create(LIQUID_MODEM_BPSK);
}

SymbolReader::~SymbolReader() {
    symsync_crcf_destroy(symsync);
    modemcf_destroy(bpsk_modem);
}

uint32_t SymbolReader::read_single(Frame *dst, float scale, const std::complex<int8_t> offset, const std::complex<int8_t> *src) {
    // convert i8 to f32
    std::complex<float> bf[samples_per_symbol];
    for (int j=0; j<samples_per_symbol; ++j) { // something like vcvt_s32_f32
        bf[j] = complex_cast<float>(src[j]-offset);
    }
    for (int j=0; j<samples_per_symbol; ++j) {
         bf[j] /= scale; // VMUL.F32
    }
    // symbol sync
    std::complex<float> symbols[samples_per_symbol];
    uint32_t symbols_written;
    symsync_crcf_execute(
        symsync, 
        bf, 
        samples_per_symbol,
        symbols,
        &symbols_written
    );
    // demodulate
    if (dst) {
        for (auto i=0u; i<symbols_written; ++i) {
            // PSK phase tracking (bpsk => order-2)
            auto sum = symbol2_buffer.push(symbols[i] * symbols[i]);
            float phase = std::arg(sum);
            std::complex<float> correction = std::polar(1.0f, -phase/2.0f);
            // actually demodulate:
            unsigned int bit; // bit-level decoding
            uint8_t soft_bit; // how likely the symbol is
            modemcf_demodulate_soft(bpsk_modem, symbols[i]*correction, &bit, &soft_bit);
            dst->softbits.push_back(soft_bit);
            dst->evm_sum += modemcf_get_demodulator_evm(bpsk_modem);
        }
    }
    return symbols_written;
}

void SymbolReader::read_preamble0(Frame *dst, float scale, std::complex<int8_t> offset, const std::complex<int8_t> *src, int end) {
    // as differential-encoded bits are BPSK-modulated, we need the -1th symbol
    // to correctly decode the first bit => read (preamble_length+1)
    // note, as min(end) >= 4, MAX_PREAMBLE can indeed be 16 for 16-bit preambles
    int start = end - (samples_per_symbol * (preamble_length+1));
    if (start < 0) {
        // the actual buffer does not contain all data, must read from
        // the previous buffer as well
        start += (MAX_PREAMBLE * samples_per_symbol); // reserve buffer's size
        for (int i=start; i<MAX_PREAMBLE * samples_per_symbol; i+=4) {
            read_single(dst, scale, offset, reserve_buffer+i);
        }
        start = 0;
    }
    for (int i=start; i<end; i+=4) {
        read_single(dst, scale, offset, src+i);
    }
}

void SymbolReader::read_preamble(Frame *dst, std::complex<int8_t> offset, const std::complex<int8_t> *src, int end) {
    symsync_crcf_reset(symsync);
    symsync_crcf_set_lf_bw(symsync, 0.001f);
    symsync_crcf_unlock(symsync);
    modem_reset(bpsk_modem);
    symbol2_buffer.reset();

    // New burst received, the symbol sync block has to lock on
    // Read the preamble, but do not save it, as it likely contain
    // some junk. The preambles are designed for quick symsync lock.
    read_preamble0(nullptr, dst->symbol_magnitude, offset, src, end);
    read_preamble0(nullptr, dst->symbol_magnitude, offset, src, end);
    // symsync_crcf_lock(symsync);
    // re-read preamble for real now
    read_preamble0(dst, dst->symbol_magnitude, offset, src, end);
}

void SymbolReader::update_reserve_buffer(const std::complex<int8_t> *src, int end) {
    const int n_preserved = MAX_PREAMBLE * samples_per_symbol;
    std::memcpy(reserve_buffer, src + end - n_preserved, n_preserved * sizeof(std::complex<uint8_t>));
}

void SymbolReader::read_symbol(Frame *dst, std::complex<int8_t> offset, const std::complex<int8_t> *src) {
    read_single(dst, dst->symbol_magnitude, offset, src);
}

bool SymbolReader::is_frame_complete(const Frame *f) {
    // we read the preamble + -1th bit to initialize differential-BPSK demodulation
    // payload, obviously
    // the symbol-sync's filters has their own delay
    return f->softbits.size() > (f->preamble_size + 1 + f->payload_size + 2*filter_delay);
}