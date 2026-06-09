#pragma once

#include <cstdint>
#include <type_traits>
#include <complex>

template<typename T>
class Preamble {
    static_assert(std::is_unsigned<T>::value, "Preamble template parameter must be an unsigned integer type");

    static constexpr int bit_count = sizeof(T) * 8;
    T preamble_word;
    int8_t pattern[bit_count][bit_count];

public:
    constexpr Preamble(T preamble) noexcept {
        preamble_word = preamble;

        // Fill the 0th row with ±1 according to bits
        T mask = 1 << (bit_count - 1);
        for (int i=0; i<bit_count; ++i) {
            pattern[0][i] = (preamble & mask) ? +1 : -1;
            preamble <<= 1;
        }

        // generate shifted rows
        for (int i=1; i<bit_count; ++i) {
            pattern[i][0] = pattern[i-1][bit_count-1];
            for (int j=1; j<bit_count; ++j) {
                pattern[i][j] = pattern[i-1][j-1];
            }
        }
    }

    int32_t dot(const int16_t* buffer, int phase) const {
        // -O2 compiles to something SIMD, like SMULL and SMLAL
        int32_t acc = 0;
        for (int i = 0; i < bit_count; ++i) {
            acc += buffer[i] * pattern[phase][i];
        }
        return acc;
    }

    constexpr T word() const { return preamble_word; };
};

template<typename T>
struct CircBuff {
    static_assert(std::is_unsigned<T>::value, "Preamble template parameter must be an unsigned integer type");
    static constexpr int bit_count = sizeof(T) * 8;

    // current "tail" of the circular buffer
    int phase = 0;
    // store differentially-demodulated baseband signals
    int16_t buff[bit_count] = {0};
    // store energy of each sample
    uint32_t buff_e[bit_count] = {0};
    // running sum of sample energy
    uint32_t window_energy = 0;

public:
    void push(int16_t v) {
        uint32_t e = static_cast<int32_t>(v) * v;
        window_energy += e - buff_e[phase];
        buff[phase] = v;
        buff_e[phase] = e;

        // inc phase
        phase = (phase + 1) % bit_count;
    }

    float match_preamble(const Preamble<T> &sync_word) {
        // guard against divide-by-zero:
        if (window_energy == 0) {
            return 0.0f;
        }

        // matched filter (real channel only):
        int32_t dotprod = sync_word.dot(buff, phase);

        // normalized correlation power. dividing the squared correlation by
        // (signal energy * N) keeps this in [0,1] and =1 only when the buffer
        // matches the preamble's shape across all taps. a spike has the same
        // dotprod but N-times the energy, so it scores ~1/N and is rejected.
        int64_t c2 = static_cast<int64_t>(dotprod) * dotprod;
        return static_cast<float>(c2) / (window_energy * bit_count);
    }
};
