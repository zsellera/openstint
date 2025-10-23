#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <memory.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>

// Misc other
static inline uint32_t parity4(uint32_t v) {
    /* http://graphics.stanford.edu/~seander/bithacks.html#ParityParallel */
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v &= 0xf;
    return (uint32_t) ((0x6996 >> v) & 1);
}

void conv_encode(uint8_t in[], uint8_t out[], int N, uint32_t poly_a, uint32_t poly_b) {
    uint32_t shr = 0;
    for (int i=0; i<N; ++i) {
        for (int j=0; j<8; ++j) {
            shr <<= 1;
            shr |= (in[i] >> (7-j)) & 1;
            out[2*(8*i+j)] = parity4(shr & poly_a);
            out[2*(8*i+j)+1] = parity4(shr & poly_b);
        }
    }
}

/**
 * There are encoder examples at https://github.com/condac/openAST/
 * However, the code above suggest a K=24, r=1/2 convolutional encoder, with
 * polynoms of 0xEEC20F and 0xEEC20D. This makes little sense to me, as
 * K=24 is practically impossible to correctly decode in real time with a
 * trellis (unless some trick is present somewher), and higher orders produce
 * marginal gains over K~=9 anyway.
 * 
 * This code brute-force polynoms, maybe there is a lower order solution
 * previous attempts were missing (hint: there isn't).
 * 
 * To run this code, enter a transponder message w/o the preamle to `expected`,
 * and the null-terminated original message to `data`.
 * 
 * Example: your transponder id is "1234567", or "0b00010010_11010110_10000111".
 * 
 * To get the pre-encoded message, reverse the binary word and break into 
 * chunks of 3. You'll end up with 24/3=8 chunks:
 * => 111_000_010_110_101_101_001_000_
 * 
 * Suffix each chunk with a bit from a "status_nr", in my transponder, it was 00000101
 * => 1110 0000 0100 1100 1010 1011 0010 0001
 * 
 * Termiate it with null, and set this as `data[]`. This word will be encoded by the
 * convolutional encoder. The `expected` is the result of the encoder.
 */
int main() {
    uint32_t known_pa = 0xEEC20F;
    uint32_t known_pb = 0xEEC20D;

    uint8_t expected[] = { 0xDA, 0x30, 0x04, 0x18, 0x2E, 0x2E, 0x82, 0xF0, 0x8C, 0xFC };
    uint8_t data[] = { 0b11100000, 0b01001100, 0b10101011, 0b00100001, 0b0000000 };

    uint8_t expected_coded[2*5*8];
    for (int i=0; i<10; i++) {
        uint8_t s = expected[i];
        for (int j=0; j<8; ++j) {
            expected_coded[8*i+j] = (s >> (7-j)) & 1;
        }
    }
    printf("expected message: ");
    for (int i=0; i<10*8; ++i) printf("%d", expected_coded[i]);
    printf("\n");

    uint8_t coded[2*5*8];
    conv_encode(data, coded, 5, known_pa, known_pb);

    printf("encoded message:  ");
    for (int i=0; i<10*8; ++i) printf("%d", coded[i]);
    printf("\n");
    
    uint32_t max = 0xffffff;
    for (uint32_t polya=0; polya<max; polya++) {
        printf("%.6X\n", polya);
        for (uint32_t polyb=(polya&0xfffff8); polyb<=(polya | 0x8); polyb++) {
            conv_encode(data, coded, 5, polya, polyb);

            bool eq = true;
            for (int i=0; i<5*8*2; i++) eq = eq && (expected_coded[i] == coded[i]);
            if (eq) {
                printf("%.4X %.4X\n", polya, polyb);
                return 0;
            }
        }
    }

    printf("\ndone\n");

    return 0;
}