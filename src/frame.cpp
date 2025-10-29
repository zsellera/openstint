#include "frame.hpp"
#include "transponder.hpp"

#include <bit>
#include <string>

#include "complex_cast.hpp"

#define FRAME_MAX_SYMBOL_SPACE 128
#define PREAMBLE_MAX_BIT_ERRORS 2
#define STATS_UPDATE_THRESHOLD (1<<12)


Frame::Frame() {
    preamble_size = payload_size = 0;
    softbits.reserve(FRAME_MAX_SYMBOL_SPACE);
    timestamp = 0;
    rssi = snr = 0;
}

Frame::Frame(TransponderType _ttype, uint64_t _ts, float symbol_e2, float noise_e2) 
    : transponder_type(_ttype), timestamp(_ts) {
    softbits.reserve(FRAME_MAX_SYMBOL_SPACE);
    payload_size = transponder_props(transponder_type).payload_size;
    preamble_size = 16; // TODO
    rssi = std::log2f(symbol_e2) / 2.0f;
    float noise_floor = std::log2f(noise_e2) / 2.0f;
    snr = rssi - noise_floor; // log(a/b) = log(a)-log(b)
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

int preamble_pos(uint32_t sof, uint16_t dpsk_preamble) {
    const int preamble_size = 16;
    const uint32_t mask = (1 << preamble_size) - 1;

    uint32_t pp = static_cast<uint32_t>(dpsk_preamble);
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
    int pos = preamble_pos(sof, transponder_props(transponder_type).dpsk_preamble);
    if (pos < 0) { // preamble was not found (ie. corrupted)
        return nullptr;
    }
    if (softbits.size() < pos + preamble_size + payload_size) {
        // could not read enough bits (this should be an exception btw...)
        return nullptr;
    }

    return softbits.data() + pos + preamble_size;
}

std::ostream& operator <<(std::ostream& os, const Frame& f) {
    std::stringstream s;
    copy(f.softbits.begin(), f.softbits.end(), std::ostream_iterator<int>(s, ", "));
    return os << transponder_props(f.transponder_type).prefix << " T:" << f.timestamp << " RSSI:" << f.rssi << " SNR:" << f.snr << " [" << s.str() << "]";
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
        [this](const std::complex<int8_t> s) { return std::norm(complex_cast<int16_t>(s)); }
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
        variance2 = static_cast<float>(s2) / (n - 1); // sample's variance
        reset_statistics_counters();
    }
}

void FrameDetector::reset_statistics_counters() {
    s1 = std::complex<int32_t>(0, 0);
    s2 = 0;
    n = 0;
}

const float FrameDetector::symbol_energy2() {
    uint32_t wes[4] = { buffers[0].window_energy, buffers[1].window_energy, buffers[2].window_energy, buffers[3].window_energy }; 
    int idx = std::distance(wes, std::max_element(wes, wes+4)); // ~maxarg

    return static_cast<float>(buffers[idx].window_energy) / 16.0f;
}

const float FrameDetector::noise_energy2() {
    return variance2;
}

const std::complex<int8_t> FrameDetector::dc_offset() {
    return offset;
}

SymbolReader::SymbolReader() {
    symsync = symsync_crcf_create_rnyquist(
        LIQUID_FIRFILT_RRC,
        samples_per_symbol,
        filter_delay, // filter length (delay!)
        0.3f, // filter excess bandwidth
        8 // number of polyphase filters in bank
    );
    dpsk_modem = modemcf_create(LIQUID_MODEM_DPSK2);
}

SymbolReader::~SymbolReader() {
    symsync_crcf_destroy(symsync);
    modemcf_destroy(dpsk_modem);
}

uint32_t SymbolReader::read_single(Frame *dst, const std::complex<int8_t> offset, const std::complex<int8_t> *src) {
    // convert i8 to f32
    std::complex<float> bf[samples_per_symbol];
    for (int j=0; j<samples_per_symbol; ++j) {
        bf[j] = complex_cast<float>(src[j]-offset);
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
    // dpsk-demod
    for (int i=0; i<symbols_written; ++i) {
        unsigned int bit; // bit-level decoding
        uint8_t soft_bit; // how likely the symbol is
        modem_demodulate_soft(dpsk_modem, symbols[i], &bit, &soft_bit);
        if (dst) {
            dst->softbits.push_back(soft_bit);
        }
    }
    return symbols_written;
}

void SymbolReader::read_preamble0(Frame *dst, std::complex<int8_t> offset, const std::complex<int8_t> *src, int end) {
    // as differential-encoded bits are BPSK-modulated, we need the -1th symbol
    // to correctly decode the first bit => read (preamble_length+1)
    // note, as min(end) >= 4, MAX_PREAMBLE can indeed be 16 for 16-bit preambles
    int start = end - (samples_per_symbol * (preamble_length+1));
    if (start < 0) {
        // the actual buffer does not contain all data, must read from
        // the previous buffer as well
        start += (MAX_PREAMBLE * samples_per_symbol); // reserve buffer's size
        for (int i=start; i<MAX_PREAMBLE * samples_per_symbol; i+=4) {
            read_single(dst, offset, reserve_buffer+i);
        }
        start = 0;
    }
    for (int i=start; i<end; i+=4) {
        read_single(dst, offset, src+i);
    }
}

void SymbolReader::read_preamble(Frame *dst, std::complex<int8_t> offset, const std::complex<int8_t> *src, int end) {
    symsync_crcf_reset(symsync);
    symsync_crcf_set_lf_bw(symsync, 0.001f);
    modem_reset(dpsk_modem);

    // New burst received, the symbol sync block has to lock on
    // Read the preamble, but do not save it, as it likely contain
    // some junk. The preambles are designed for quick symsync lock.
    read_preamble0(nullptr, offset, src, end);
    read_preamble0(nullptr, offset, src, end);
    // re-read preamble for real now
    read_preamble0(dst, offset, src, end);
}

void SymbolReader::update_reserve_buffer(const std::complex<int8_t> *src, int end) {
    const int n_preserved = MAX_PREAMBLE * samples_per_symbol;
    memcpy(reserve_buffer, src + end - n_preserved, n_preserved * sizeof(std::complex<uint8_t>));
}

void SymbolReader::read_symbol(Frame *dst, std::complex<int8_t> offset, const std::complex<int8_t> *src) {
    read_single(dst, offset, src);
}

bool SymbolReader::is_frame_complete(const Frame *f) {
    // we read the preamble + -1th bit to initialize differential-BPSK demodulation
    // payload, obviously
    // the symbol-sync's filters has their own delay
    return f->softbits.size() > (f->preamble_size + 1 + f->payload_size + 2*filter_delay);
}