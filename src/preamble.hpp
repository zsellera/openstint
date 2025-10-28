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

    int16_t dot(const int8_t* buffer, int phase) const {
        // -O2 compiles to something SIMD, like SMULL and SMLAL
        int16_t acc = 0;
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
    // store baseband signals:
    int8_t buff_i[bit_count] = {0};
    int8_t buff_q[bit_count] = {0};
    // also store baseband energy for t-statisctics
    uint32_t buff_e[bit_count] = {0};
    uint32_t window_energy = 0; // sum of all buff_e, buffered here

public:
    void push(std::complex<int8_t> symbol, uint32_t symbol_energy) {
        // update window energy
        window_energy += symbol_energy - buff_e[phase];
        buff_e[phase] = symbol_energy;
        // update circular buffer
        buff_i[phase] = symbol.real();
        buff_q[phase] = symbol.imag();
        // inc phase
        phase = (phase + 1) % bit_count;
    }
    
    float match_preamble(const Preamble<T> &sync_word) {
        // guard against divide-by-zero:
        if (window_energy == 0) {
            return 0.0f;
        }

        // run matched filter on both baseband components:
        int32_t dotprod_i = sync_word.dot(buff_i, phase);
        int32_t dotprod_q = sync_word.dot(buff_q, phase);
        
        // correlation result squared:
        int32_t c2 = dotprod_i * dotprod_i + dotprod_q * dotprod_q;

        // create a statistics that can predict how well
        // the pattern fits to the sample.
        return static_cast<float>(c2) / (window_energy * 16);
    }

    uint32_t energy() const { return window_energy; }
};