#include "transponder.hpp"

#include <bit>
#include <string>

#include <stdio.h>

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

int decode_legacy(const uint8_t *softbits, uint32_t *transponder_id) {
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
    // Suffix each chunk with a bit from a "scrambler_byte", in my transponder, it was 00000101
    // 1110 0000 0100 1100 1010 1011 0010 0001
    uint32_t tid = 0; // transponder_id
    for (int i=0; i<32; i++) {
        if (i % 4 != 0) { // skip every 4th scrambling bit
            uint32_t bitmask = (1 << i);
            uint32_t bit = (message & bitmask) ? 1 : 0;
            tid = (tid << 1) | bit;
        }
    }
    *transponder_id = tid;
    return (trail == 0);
}