#pragma once

#include <cstdint>
#include <type_traits>
#include <complex>

template<typename T>
struct CircBuff;

template<typename T>
class Preamble {
    static_assert(std::is_unsigned<T>::value, "Preamble template parameter must be an unsigned integer type");

    static constexpr int bit_count = sizeof(T) * 8;
    int16_t pattern[bit_count][bit_count];
    float threshold;

    friend struct CircBuff<T>;

public:
    constexpr Preamble(T preamble, float _threshold) noexcept : threshold(_threshold) {
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
};

template<typename T>
struct CircBuff {
    static_assert(std::is_unsigned<T>::value, "Preamble template parameter must be an unsigned integer type");
    static constexpr int bit_count = sizeof(T) * 8;
    static constexpr int32_t early_threshold = -bit_count * 3;

    // current "tail" of the circular buffer
    int phase = 0;
    // store baseband signals:
    int16_t buff[bit_count] = {0};
    // also store baseband energy for t-statisctics
    uint64_t buff_e[bit_count] = {0};
    uint64_t window_energy = 0; // sum of all buff_e, buffered here

public:
    void push(int16_t symbol, uint32_t symbol_energy) {
        // update window energy
        window_energy += static_cast<uint64_t>(symbol_energy) - buff_e[phase];
        // update circular buffers
        buff_e[phase] = symbol_energy; // energy buffer
        buff[phase] = symbol; // amplitude-squared buffer
        // inc phase
        phase = (phase + 1) % bit_count;
    }

    void clear_next() {
        window_energy -= buff_e[phase];
        buff_e[phase] = 0;
        buff[phase] = 0;
    }
    
    bool match_preamble(const Preamble<T> &sync_word) {
        // run matched filter against differential signal
        int32_t corr = sync_word.dot(buff, phase);
        
        // early return - there is no valid match below a specified correlation
        // the way the preamble match works is inverted, +A^2 means no change,
        // -A^2 means change in symbols (hence negative)
        if (corr > early_threshold) return false;
        
        // correlation result squared
        // the /4 is an optimization, so dotprod fits to int32
        corr /= 4;

        // create a statistics that can predict how well
        // the pattern fits to the sample.
        return static_cast<float>(corr*corr) > sync_word.threshold * static_cast<float>(window_energy);
    }

    // re-calculate match metric, no compute cost spared
    float calc_metric(const Preamble<T> &sync_word) {
        int64_t corr = static_cast<int64_t>(sync_word.dot(buff, phase));
        return static_cast<float>(corr*corr) / static_cast<float>(16 * window_energy);
    }

    uint32_t energy() const { return window_energy; }
};