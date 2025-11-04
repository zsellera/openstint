#pragma once

#include <algorithm>

template<unsigned int window_len, typename T>
struct SummingBuffer {
    T buffer[window_len] = {0};
    T sum = {0};
    int tail = 0;

    void reset() {
        std::fill(buffer, buffer + window_len, T {});
        sum = {0};
        tail = 0;
    }

    T push(T val) {
        sum += val - buffer[tail];
        buffer[tail] = val;
        tail = (tail + 1) % window_len;
        return sum;
    }
};
