#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <memory.h>
#include <math.h>
#include <time.h>

#include <fec.h>
#include <liquid/liquid.h>

// number of octetts
#define N 4

// number of test messages
#define M 100000


// Misc other
static inline uint8_t parity1(uint8_t x) {
    /* http://graphics.stanford.edu/~seander/bithacks.html#ParityParallel */
    x ^= x >> 4;
    x &= 0xf;
    return (uint8_t) ((0x6996 >> x) & 1);
}

static inline uint32_t parity4(uint32_t v) {
    /* http://graphics.stanford.edu/~seander/bithacks.html#ParityParallel */
    v ^= v >> 16;
    v ^= v >> 8;
    return parity1((uint8_t) v);
}

void conv_encode(uint8_t in[], uint8_t out[]) {
    // memset(out, 0, 2*N);
    uint32_t shr = 0;
    for (int i=0; i<N; ++i) {
        for (int j=0; j<8; ++j) {
            shr <<= 1;
            shr |= (in[i] >> (7-j)) & 1;
            out[2*(8*i+j)] = (0 - parity4(shr & V27POLYA));
            out[2*(8*i+j)+1] = (0 - parity4(shr & V27POLYB));
        }
    }
    for (int i=0; i<8; ++i) { // trailing bits
        shr <<= 1;
        shr &= 0xffe;
        out[2*N*8+2*i] = (0 - parity4(shr & V27POLYA));
        out[2*N*8+2*i+1] = (0 - parity4(shr & V27POLYB));
    }
}

int main() {
    crc_scheme crc_scheme = LIQUID_CRC_8;
    modemcf dpsk_mod = modemcf_create(LIQUID_MODEM_DPSK2);
    modemcf dpsk_demod = modemcf_create(LIQUID_MODEM_DPSK2);

    // SNR
    float SNRdB = 6.0f;

    // quick init
    srand(time(NULL));

    // data to send:
    uint8_t data[4];
    data[0] = rand() & 0xff;
    data[1] = rand() & 0xff;
    data[2] = rand() & 0xff;
    data[3] = (uint8_t) crc_generate_key(crc_scheme, data, 3);
    printf("original message: ");
    for (int i=0; i<N; ++i) printf("%.2X ", data[i]);
    printf("\n");

    // apply convolutional encoder:
    uint8_t msg_enc[2*N*8+12];   // encoded data message, 1 byte per bit
    memset(msg_enc, 0, 2*N*8+12);
    conv_encode(data, msg_enc);
    printf("encoded message:  ");
    for (int i=0; i<2*N*8+12; ++i) printf("%.2X ", msg_enc[i]);
    printf("\n");

    // test libfec viterbi:
    unsigned char msg_dec[N];
    void *vp = create_viterbi27(2*N*8);
    // int polys[] = {V27POLYA, V27POLYB};
    // set_viterbi27_polynomial(polys);
    init_viterbi27(vp, 0);
    update_viterbi27_blk(vp, msg_enc, N*8+12);
    chainback_viterbi27(vp, msg_dec, N*8, 0);
    printf("decoded message:  ");
    for (int i=0; i<N; ++i) printf("%.2X ", msg_dec[i]);
    printf("\n");

    // direct encode with differential-BPSK
    float complex tx[2*N*8+12];
    for (int i=0; i<(2*N*8+12); i++) {
        unsigned int s = (msg_enc[i] > 128) ? 1 : 0;
        modem_modulate(dpsk_mod, s, tx+i);
    }

    // statistics
    int bit_errors = 0;
    int valid_rx = 0;
    int crc_falsepositive = 0;

    // // add random noise
    float nstd = powf(10.0f, -SNRdB/20.0f);
    for (int c=0; c<M; c++) {
        float complex rx[2*N*8+12];
        memcpy(rx, tx, (2*N*8+12) * sizeof(float complex));
        for (int i=0; i<(2*N*8+12); i++) rx[i] += nstd*(randnf() + _Complex_I*randnf())/sqrtf(2.0f);

        // demodulate
        uint8_t recv[2*N*8+12];
        memset(recv, 0, sizeof(recv));
        modem_reset(dpsk_demod);
        for (int i=0; i<(2*N*8+12); i++) {
            unsigned int s;
            modem_demodulate_soft(dpsk_demod, rx[i], &s, &recv[i]);
            // recv[i] = (s == 0) ? 0 : 0xff;
        }

        // printf("dpsk message:  ");
        // for (int i=0; i<2*N*8+12; ++i) printf("%.2X ", recv[i]);
        // printf("\n");

        // fec decode
        uint8_t msg_dec[4];
        init_viterbi27(vp, 0);
        update_viterbi27_blk(vp, recv, N*8+12);
        chainback_viterbi27(vp, msg_dec, N*8, 0);
        // printf("decoded message:  ");
        // for (int i=0; i<N; ++i) printf("%.2X ", msg_dec[i]);
        // printf("\n");

        // error check:
        unsigned int num_bit_errors = count_bit_errors_array(data, msg_dec, 4);
        bit_errors += num_bit_errors;
        if (num_bit_errors == 0) {
            ++valid_rx;
        }
        int crc_pass = crc_validate_message(crc_scheme, msg_dec, 3, msg_dec[3]);
        if (crc_pass && num_bit_errors) { crc_falsepositive++; }
    }

    printf("noise added: %.4f\n", nstd);
    printf("success ratio:    %.4f\n", ((float) valid_rx) * 100.0f / M);
    printf("bit error rate:   %.4f\n", ((float) bit_errors) * 100.0f / M / (4*8));
    printf("false positives:  %.4f\n", ((float) crc_falsepositive) * 100.0f / M);
    printf("done.\n");
    return 0;
}