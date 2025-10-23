#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <memory.h>
#include <math.h>
#include <time.h>
#include <liquid/liquid.h>

#define M 100000

int main() {
    crc_scheme crc_scheme = LIQUID_CRC_8;
    modemcf dpsk_mod = modemcf_create(LIQUID_MODEM_DPSK2);
    modemcf dpsk_demod = modemcf_create(LIQUID_MODEM_DPSK2);
    float SNRdB = 6.0f;

    // quick init
    srand(time(NULL));

    // data to send:
    uint8_t data[4];
    data[0] = rand() & 0xff;
    data[1] = rand() & 0xff;
    data[2] = rand() & 0xff;
    data[3] = (uint8_t) crc_generate_key(crc_scheme, data, 3);

    // direct encode with differential-BPSK
    float complex tx[4*8];
    for (int i=0; i<4; i++) {
        for (int j=0; j<8; j++) {
            unsigned int s = (data[i] >> (7-j)) & 0x1;
            modem_modulate(dpsk_mod, s, tx+i*8+j);
        }
    }

    // statistics
    int bit_errors = 0;
    int valid_rx = 0;
    int crc_falsepositives = 0, crc_falsenegatives = 0;

    // add random noise
    float nstd = powf(10.0f, -SNRdB/20.0f);
    for (int c=0; c<M; c++) {
        float complex rx[4*8];
        memcpy(rx, tx, sizeof(rx));
        for (int i=0; i<4*8; i++) rx[i] += nstd*(randnf() + _Complex_I*randnf())/sqrtf(2.0f);

        // demodulate
        uint8_t recv[4];
        memset(recv, 0, sizeof(recv));
        modem_reset(dpsk_demod);
        for (int i=0; i<4; i++) {
            for (int j=0; j<8; j++) {
                unsigned int s;
                modem_demodulate(dpsk_demod, rx[i*8+j], &s);
                s &= 1;
                recv[i] |= (s << (7-j));
            }
        }

        int crc_pass = crc_validate_message(crc_scheme, recv, 3, recv[3]);
        unsigned int frame_bit_errors = count_bit_errors_array(data, recv, 4);
        unsigned int transponder_bit_errors = count_bit_errors_array(data, recv, 3);

        if (!frame_bit_errors) { ++valid_rx; }
        if (frame_bit_errors && crc_pass) { ++crc_falsepositives; }
        if (!transponder_bit_errors && !crc_pass) { ++crc_falsenegatives; }
    }

    printf("noise added: %.4f\n", nstd);
    printf("success ratio:    %.4f\n", ((float) valid_rx) * 100.0f / M);
    printf("bit error rate:   %.4f\n", ((float) bit_errors) * 100.0f / (M * 4 * 8));
    printf("false negatives:  %.4f\n", ((float) crc_falsenegatives) * 100.0f / M);
    printf("false positives:  %.4f\n", ((float) crc_falsepositives) * 100.0f / M);
    printf("done.\n");
    return 0;
}