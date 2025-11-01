#pragma once

#include <string>

template<unsigned int window_len, typename T>
struct SummingBuffer {
    T buffer[window_len] = {0};
    T sum = {0};
    int tail = 0;

    void reset() {
        std::memset(buffer, 0, sizeof(buffer));
        std::memset(&sum, 0, sizeof(sum));
        tail = 0;
    }

    T push(T val) {
        sum += val - buffer[tail];
        buffer[tail] = val;
        tail = (tail + 1) % window_len;
        return sum;
    }
};
