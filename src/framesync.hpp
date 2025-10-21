#pragma once

#include <memory>
#include <complex>

#include <liquid/liquid.h>

#define FRAMESYNC_MAX_SYMBOLS 160
#define FRAMESYNC_BUF_SIZE    1024

struct Frame {
    size_t len;
    std::complex<float> data[FRAMESYNC_BUF_SIZE];

    Frame() : len(0) {};
    
    float rssi();
};

class FrameParser {
    float trigger_sigma;
    int pos_trg_required;
    int neg_trg_required;

    symsync_crcf q_symsync;
    Frame *current_frame;

    std::complex<float> dc_offset = 0.0f;
    float noise2 = 0.0f;

    enum ParserState { PS_SEEK, PS_PROCESS };
    ParserState state;
    int pos_trg_count;
    int neg_trg_count;
public:
    FrameParser(float trigger_sigma, int pos_trg_required, int neg_trg_required);
    ~FrameParser();

    std::unique_ptr<Frame> next_frame(const std::complex<int8_t> buffer[], int size, int& idx);

private:
    void create_empty_frame();
    void fill_frame(const std::complex<int8_t> src[], int beg_idx, int end_idx);
    void adjust_signal_characteristics(const std::complex<int32_t> sum, const uint32_t err_sum, int N);
};
