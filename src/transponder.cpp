#include "transponder.hpp"

#include <bit>
#include <string>

extern "C" {
#include <fec.h>
}
#include <liquid/liquid.h>

static void *viterbi_decoder;
static crc_scheme crc8_scheme = LIQUID_CRC_8;

void init_transponders() {
    viterbi_decoder = create_viterbi29(32);
}

int decode_openstint(const uint8_t *softbits, uint32_t *transponder_id) {
    uint8_t decoded[4];

    init_viterbi29(viterbi_decoder, 0);
    update_viterbi29_blk(viterbi_decoder, const_cast<uint8_t*>(softbits), 32+8); // khm...
    chainback_viterbi29(viterbi_decoder, decoded, 32, 0);
    
    *transponder_id = (static_cast<uint32_t>(decoded[0]) << 16) | (static_cast<uint32_t>(decoded[1]) << 8) | static_cast<uint32_t>(decoded[2]);
    return crc_validate_message(crc8_scheme, decoded, 3, decoded[3]);
}

int decode_rc3(const uint8_t *softbits, uint32_t *transponder_id, uint8_t *status_code) {
    // RC3 use a K=24, r=1/2 convolutional encoder with polynoms 0xEEC20F and 0xEEC20D
    // Decoding this properly with error correction must have some unknown trick. However,
    // we can do non-trivial decoding as well.
    // Note, the generating polynoms differ only in one bit (0xf=1111 vs 0xd=1101). 
    // This reduces the complexity to:
    // bit0 = parity(SHREG & 0xEEC20C) ^ SHREG[1] ^ SHREG[0]
    // bit1 = parity(SHREG & 0xEEC20C) ^            SHREG[0]
    // As such, we know: 
    // bit0 ^ bit1 = SHREG[1]
    // SHREG[0] = bit0 ^ parity(SHREG & 0xEEC20C) ^ SHREG[1]
    // SHREG[0] = bit1 ^ parity(SHREG & 0xEEC20C)
    // we can use these for a makeshift error correction
    
    uint64_t shreg = 0; // shift register
    bool last_ok = true; // last 2-bits were successfully decoded
    
    // Before encoding, the 24 bit transponder id is scrambled with extra 8 bits,
    // resulting in 32 bits. This is further appended with 0x00 so viterbi-decoder (???)
    // can process it. The rate=1/2 encoder generates 2x40=80 bits in total.
    int sym = 0, prev_sym = 0;  // differential decoder
    for (int i=0; i<80; i+=2) {
        int p = std::popcount(shreg & 0xEEC20C) % 2; // parity bit from SHREG

        // differential-BPSK is decoded here as well (sym ^ prev_sym magic):
        sym = (softbits[i+0] > 127) ? 1 : 0;
        int b0 = sym ^ prev_sym;
        prev_sym = (softbits[i+1] > 127) ? 1 : 0;
        int b1 = prev_sym ^ sym; // decode softbit bit1

        int shreg1 = (shreg & 2) ? 1 : 0; // shift register last-1 bit
        // two estimates for SHREG[0] (should be equal):
        int shreg0p0 = p ^ shreg1 ^ b0;
        int shreg0p1 = p ^ b1;
        if (last_ok) { // no error correction for SHREG[1] is needed
            last_ok = (shreg0p0 == shreg0p1);
            if (last_ok) {
                shreg |= shreg0p0; // high certainty, write bit to SHREG
            }
        } else { // must correct SHREG[1] based on bit0 and bit1
            int shreg1p = b0 ^ b1; // SHREG[1] guesstimate; see top comment
            // SHREG[0]'  = b0 ^ PAR(...) ^ SHREG[1] = b0 ^ PAR(...) ^ (b0 ^ b1) = PAR(...) ^ b1 = shreg0p1
            shreg |= (shreg1p << 1) | shreg0p1;
            last_ok = true;
        }
        shreg <<= 1; // no matter if we have the last bit correctly, shift it
    }

    // error detection
    shreg >>= 1;
    uint32_t trail = static_cast<uint32_t>(shreg & 0xff);
    uint32_t message = static_cast<uint32_t>((shreg>>8) & 0xffffffff);

    // Example: your transponder id is "1234567", or "0b00010010_11010110_10000111".
    // To get the pre-encoded message, reverse the binary word and break into 
    // chunks of 3. You'll end up with 24/3=8 chunks:
    // 111 000 010 110 101 101 001 000
    // Suffix each chunk with a bit from a "status code",
    // in my RC4 hybrid transponder, it was 00000101
    // 1110 0000 0100 1100 1010 1011 0010 0001
    uint32_t tid = 0; // transponder_id
    uint8_t status = 0; // status code
    for (int i=0; i<32; i++) {
        uint32_t bitmask = (1 << i);
        uint32_t bit = (message & bitmask) ? 1 : 0;

        if (i % 4 != 0) { // every 4th is status bit
            tid = (tid << 1) | bit;
        } else {
            status = (status << 1) | bit;
        }
    }
    *transponder_id = tid;
    *status_code = status;

    // the last byte must be zero (tail==0 error check)
    return (trail == 0);
}

int decode_vostok(const uint8_t *softbits, uint32_t *transponder_id) {
    // Vostok transponders reuse the RC3 preamble, but the payload is not
    // convolutionally encoded at all. Once the differential-BPSK is undone, an
    // 80 bit frame is plain: a fixed header, the transponder id sent twice, and
    // two running XOR checksums.
    //
    //   byte:   0    1  |  2  3  4  |  5  |  6  7  8  |  9
    //          0x63 0x1A|    id     | c1  |    id     | c2
    //
    // The id is a 24 bit big-endian word, so 0x4A6151 is transponder 4874577.
    // Each checksum is the XOR of every byte before it:
    //
    //   c1 = 0x63 ^ 0x1A ^ id[0] ^ id[1] ^ id[2]   (XOR of bytes 0..4)
    //   c2 = c1 ^ id[0] ^ id[1] ^ id[2] = c1 ^ 0x79  (XOR of bytes 0..8)
    //
    // so c2 is fully determined by c1, and c1 ^ c2 == 0x79 in every frame.

    // differential-decode into 10 bytes, MSB first. prev=0 as in decode_rc3.
    uint8_t frame[10] = {0};
    int prev = 0;
    for (int i = 0; i < 80; i++) {
        int raw = (softbits[i] > 127) ? 1 : 0;
        frame[i / 8] |= static_cast<uint8_t>((raw ^ prev) << (7 - (i % 8)));
        prev = raw;
    }

    if (frame[0] != 0x63 || frame[1] != 0x1a) { // fixed header
        return 0;
    }
    // both copies of the transponder id must agree
    if (frame[2] != frame[6] || frame[3] != frame[7] || frame[4] != frame[8]) {
        return 0;
    }
    uint8_t c1 = 0;
    for (int i = 0; i < 5; i++) {
        c1 ^= frame[i];
    }
    if (c1 != frame[5] || static_cast<uint8_t>(c1 ^ 0x79) != frame[9]) {
        return 0;
    }

    *transponder_id = (static_cast<uint32_t>(frame[2]) << 16)
                    | (static_cast<uint32_t>(frame[3]) << 8)
                    | static_cast<uint32_t>(frame[4]);

    return 1;
}

void AmbRcBlacklist::process(uint64_t timestamp, uint8_t status_code, uint32_t transponder_id) {
    // not a candidate status/validation message:
    if ((status_code & 0xf8) != 0xf8) return; // not an AmbRc message
    if ((status_code & 0x07) == 0) return; // no counter set

    uint8_t msb8 = static_cast<uint8_t>((transponder_id >> 16) & 0xff);
    if (status_code != 0xff) { // status/validation message for sure
        msb8_timestamps[msb8] = timestamp;
    } else { // might be a status/validation message
        auto it = msb8_timestamps.find(msb8);
        if (it != msb8_timestamps.end() && timestamp <= (it->second + 250000ul)) {
            banned_transponders.insert(transponder_id);
        }
    }
}

bool AmbRcBlacklist::check_banned(uint32_t transponder_id) const {
    return banned_transponders.contains(transponder_id);
}
