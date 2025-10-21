#include "framesync.hpp"

#include <numeric>
#include <functional>
#include <cmath>
#include <complex>

#include <cstdio>
#include <cstdlib>
#include <iostream>

#include <liquid/liquid.h>

#define FRAMESYNC_ADJUST_SAMPLE_COUNT_LIMIT 128


namespace {
    template <typename To, typename From>
    constexpr std::complex<To> complex_cast(const std::complex<From>& c) noexcept {
        return {static_cast<To>(c.real()), static_cast<To>(c.imag())};
    }
}

float Frame::rssi() {
    // guard clause:
    if (len <= 0) {
        return 0.0f;
    }

    // rssi is the log2 of RMS
    float mag2_sum = std::transform_reduce(
        data, data + len, // range
        0.0f, // initial sum
        std::plus<>(),
        [this](const std::complex<float> &c) {
            return std::norm(c);
        }
    );
    return std::log2f(std::sqrtf(mag2_sum / len));
}

FrameParser::FrameParser(float _trigger_sigma, int _pos_trg_required, int _neg_trg_required) {
    this->trigger_sigma = _trigger_sigma;
    this->pos_trg_required = _pos_trg_required;
    this->neg_trg_required = _neg_trg_required;
    
    this->state = PS_SEEK;
    this->pos_trg_count = 0;
    this->neg_trg_count = 0;

    this->dc_offset = 0.0f;
    this->noise2 = 0.0;

    this->q_symsync = symsync_crcf_create_rnyquist(
        LIQUID_FIRFILT_RRC,
        4, // samples per symbol
        5, // filter length
        0.35f, // filter excess bandwidth
        8 // number of polyphase filters in bank
    );
}

FrameParser::~FrameParser() {
    if (current_frame != nullptr) {
        delete current_frame;
    }
    symsync_crcf_destroy(q_symsync);
}


void FrameParser::adjust_signal_characteristics(const std::complex<int32_t> sum, const uint32_t err_sum, int N) {
    if (N >= FRAMESYNC_ADJUST_SAMPLE_COUNT_LIMIT) {
        std::complex<float> sample_dc_offset = complex_cast<float>(sum) / static_cast<float>(N);
        float sample_noise2 = static_cast<float>(err_sum) / static_cast<float>(N);
        
        // longer samples should adjust the noise/offset parameters more
        // the numbers are absolutely arbitrary here
        float adjust_factor = static_cast<float>(N) / (1000 * FRAMESYNC_ADJUST_SAMPLE_COUNT_LIMIT);
        if (adjust_factor > 1.0f) {
            adjust_factor = 1.0f;
        }

        dc_offset = (1.0f - adjust_factor) * dc_offset + adjust_factor * sample_dc_offset;
        noise2 = (1.0f - adjust_factor) * noise2 + adjust_factor * sample_noise2;
    }
}

void FrameParser::create_empty_frame() {
    // reset frame:
    if (current_frame != nullptr) {
        delete current_frame;
    }
    current_frame = new Frame();

    // clear symbol sync buffers:
    symsync_crcf_reset(q_symsync);
}

void FrameParser::fill_frame(const std::complex<int8_t> src[], int beg_idx, int end_idx) {
    int sample_count = end_idx - beg_idx;
    int space_left = FRAMESYNC_MAX_SYMBOLS - current_frame->len;
    int space_expected = sample_count / 4;
    if (space_expected > space_left) {
        // adjust to reality
        sample_count = 4 * space_left;
        end_idx = beg_idx + sample_count;
    }
    if (end_idx <= beg_idx) {
        return; // nothing to do here
    }

    // move int8 data to a temporary buffer:
    std::complex<float> *in = new std::complex<float>[sample_count];
    std::transform(src + beg_idx, src + end_idx, in,
                   [this](const std::complex<int8_t> c) {
                       return std::complex<float>(
                           static_cast<float>(c.real()) - dc_offset.real(),
                           static_cast<float>(c.imag()) - dc_offset.imag());
                   });
    // run symbol sync algo:
    unsigned int num_written;   // number of values written to buffer
    symsync_crcf_execute(q_symsync, in, sample_count, current_frame->data + current_frame->len, &num_written);
    // symsync_crcf_print(q_symsync);
    current_frame->len += num_written;
    // free up temp buffer:
    delete[] in;
}

std::unique_ptr<Frame> FrameParser::next_frame(const std::complex<int8_t> buffer[], int size, int& idx) {
    // std::cout << "dc_offset: " << dc_offset << " noise: " << noise2 << std::endl;
    // DC offset and triggering:
    int32_t trigger_lvl = static_cast<int32_t>(noise2 * trigger_sigma) + 1; // +1 LSB
    std::complex<int8_t> offset = complex_cast<int8_t>(dc_offset);
    
    // accumulators for DC offset and noise level maintenance:
    std::complex<int> sig_acc { 0, 0 }; // sum of all non-frame signals
    float mag2_acc = 0.0f; // sum of all average squared error
    int beg_idx = idx; // used for calculating the number of processed samples

    // identify frames:
    int frame_start_idx = 0; // to be updated; start of an identified frame
    for (; idx<size; idx++) {
        // remove dc offset, calculate mag(z):
        std::complex<int8_t> z = buffer[idx] - offset;
        int32_t mag2 = std::norm(complex_cast<int32_t>(z));

        // update dc offset and noise accumulators:
        sig_acc += complex_cast<int32_t>(buffer[idx]);
        mag2_acc += mag2;

        // run the state machine
        if (state == PS_SEEK) {
            if (mag2 >= trigger_lvl) {
                if ((++pos_trg_count) >= pos_trg_required) {
                    // change state:
                    state = PS_PROCESS;
                    neg_trg_count = 0;
                    // initialize an empty frame:
                    create_empty_frame();
                    frame_start_idx = idx;
                }
            } else {
                // reset state:
                pos_trg_count = 0;
            }
        } else if (state == PS_PROCESS) {
            if (mag2 < trigger_lvl) {
                if ((++neg_trg_count) >= neg_trg_required) {
                    state = PS_SEEK;
                    neg_trg_count = 0;
                    pos_trg_count = 0;
                    fill_frame(buffer, frame_start_idx, idx);
                    // adjust dc offset and noise, based on the measured blanks:
                    adjust_signal_characteristics(sig_acc, mag2_acc, idx - beg_idx);
                    // return the frame while transfering its ownership:
                    Frame *frame = current_frame;
                    current_frame = nullptr;
                    return std::unique_ptr<Frame>(frame);
                }
            } else {
                neg_trg_count = 0;
            }
        }
    }

    // parsed everything and parsing is still in progress
    if (state == PS_PROCESS) {
        fill_frame(buffer, frame_start_idx, idx-1);
    }

    // adjust dc offset and noise before exiting
    adjust_signal_characteristics(sig_acc, mag2_acc, idx - beg_idx);

    // no frame found yet
    return std::unique_ptr<Frame>(nullptr);
}