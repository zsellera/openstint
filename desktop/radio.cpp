#include "radio.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "commons.hpp"

static const uint64_t CENTER_FREQ_HZ = 5000000ULL;
static const uint32_t TUNER_BANDWIDTH_HZ = 1700000; // see main_rtlsdr.cpp for why 1.7 MHz
static const size_t CHUNK_BYTES = 2 * 16384;        // 16384 IQ samples, ~6.5 ms at 2.5 MSPS
static const uint32_t ASYNC_BUFFERS = 12;

// measure every Nth buffer only
static const uint32_t ENERGY_EVERY_NTH_BUFFER = 32;
// Transponders transmit short bursts with silence inbetween, so the buffer's mean
// power mostly measures the silence. Report the strongest 64-symbol stretch
// instead (51 us at 2.5 MSPS), shorter than a frame: that is what clips.
static const uint32_t ENERGY_WINDOW_SAMPLES = 64 * SAMPLES_PER_SYMBOL;

static int64_t steady_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::vector<RtlSdrDeviceInfo> enumerate_rtlsdr_devices() {
    std::vector<RtlSdrDeviceInfo> devices;
    const uint32_t count = rtlsdr_get_device_count();
    for (uint32_t i = 0; i < count; ++i) {
        char manufact[256] = {0}, product[256] = {0}, serial[256] = {0};
        RtlSdrDeviceInfo info{i, {}, false};
        if (rtlsdr_get_device_usb_strings(i, manufact, product, serial) == 0 && product[0]) {
            info.likely_v4 = std::strstr(product, "V4") != nullptr;
            info.label = std::string(product);
            if (serial[0]) {
                info.label += " (SN " + std::string(serial) + ")";
            }
        } else {
            const char* name = rtlsdr_get_device_name(i);
            info.label = name ? name : "RTL-SDR";
        }
        info.label = "[" + std::to_string(i) + "] " + info.label;
        devices.push_back(std::move(info));
    }
    return devices;
}

RtlSdrRadio::~RtlSdrRadio() {
    stop();
}

bool RtlSdrRadio::fail(const std::string& what, int code, std::string& error) {
    error = what + " failed (" + std::to_string(code) + ")";
    rtlsdr_close(device);
    device = nullptr;
    return false;
}

bool RtlSdrRadio::start(uint32_t index, int gain_tenths_db, bool bias_tee, std::string& error) {
    stop();

    int r = rtlsdr_open(&device, index);
    if (r != 0 || device == nullptr) {
        device = nullptr;
        error = "rtlsdr_open() failed (" + std::to_string(r) + "). Is the device used by another program?";
        return false;
    }

    // V4 detection, same as main_rtlsdr.cpp: USB product string first, tuner type as fallback
    bool is_v4 = false;
    char manufact[256] = {0}, product[256] = {0}, serial[256] = {0};
    if (rtlsdr_get_usb_strings(device, manufact, product, serial) == 0) {
        is_v4 = std::strstr(product, "V4") != nullptr;
    }
    const rtlsdr_tuner tuner = rtlsdr_get_tuner_type(device);
    if (!is_v4) {
        is_v4 = (tuner == RTLSDR_TUNER_R828D);
    }

    direct = !is_v4;
    if (direct) {
        // older dongles need direct sampling (Q-branch) for HF; tuner gain does not apply
        r = rtlsdr_set_direct_sampling(device, 2);
        if (r != 0) {
            return fail("rtlsdr_set_direct_sampling()", r, error);
        }
        desc = "direct sampling (Q-branch)";
    } else {
        desc = "native HF (V4)";
    }

    r = rtlsdr_set_center_freq(device, CENTER_FREQ_HZ);
    if (r != 0) {
        return fail("rtlsdr_set_center_freq()", r, error);
    }
    r = rtlsdr_set_sample_rate(device, SAMPLE_RATE);
    if (r != 0) {
        return fail("rtlsdr_set_sample_rate()", r, error);
    }
    rtlsdr_set_tuner_bandwidth(device, TUNER_BANDWIDTH_HZ); // best effort
    rtlsdr_set_tuner_gain_mode(device, 1);                   // manual gain
    if (!direct) {
        rtlsdr_set_tuner_gain(device, gain_tenths_db);
    }
    rtlsdr_set_bias_tee(device, bias_tee ? 1 : 0);
    rtlsdr_reset_buffer(device);

    buffer_counter = 0;
    last_rx_ms = steady_ms();
    is_streaming = true;
    rx_thread = std::thread([this]() {
        rtlsdr_read_async(device, &RtlSdrRadio::rx_callback, this, ASYNC_BUFFERS, CHUNK_BYTES);
        is_streaming = false;
    });
    return true;
}

void RtlSdrRadio::stop() {
    if (device == nullptr) {
        return;
    }
    rtlsdr_cancel_async(device);
    if (rx_thread.joinable()) {
        rx_thread.join();
    }
    rtlsdr_set_bias_tee(device, 0);
    rtlsdr_close(device);
    device = nullptr;
    is_streaming = false;
}

int64_t RtlSdrRadio::ms_since_last_rx() const {
    return steady_ms() - last_rx_ms;
}

int RtlSdrRadio::set_gain(int gain_tenths_db) {
    if (device == nullptr || direct) {
        return gain_tenths_db;
    }
    rtlsdr_set_tuner_gain(device, gain_tenths_db);
    return rtlsdr_get_tuner_gain(device);
}

void RtlSdrRadio::set_bias_tee(bool enabled) {
    if (device != nullptr) {
        rtlsdr_set_bias_tee(device, enabled ? 1 : 0);
    }
}

std::vector<float> RtlSdrRadio::take_energy() {
    std::lock_guard<std::mutex> lock(energy_mutex);
    std::vector<float> out;
    out.swap(energy);
    return out;
}

void RtlSdrRadio::rx_callback(unsigned char* buf, uint32_t len, void* ctx) {
    static_cast<RtlSdrRadio*>(ctx)->on_samples(buf, len);
}

void RtlSdrRadio::on_samples(const unsigned char* buf, uint32_t len) {
    last_rx_ms = steady_ms();

    const uint32_t sample_count = len / 2;
    if (conversion_buffer.size() < sample_count) {
        conversion_buffer.resize(sample_count);
    }
    // unsigned (DC at 128) -> signed, which the decoder expects
    for (uint32_t i = 0; i < sample_count; ++i) {
        conversion_buffer[i] = std::complex<int8_t>(
            static_cast<int8_t>(buf[2 * i] - 128),
            static_cast<int8_t>(buf[2 * i + 1] - 128));
    }

    detect_frames(conversion_buffer.data(), sample_count, steady_now_us());

    if (++buffer_counter % ENERGY_EVERY_NTH_BUFFER != 0 || sample_count == 0) {
        return;
    }
    // sliding window sum of |x|^2, keep the maximum
    auto power = [this](uint32_t i) {
        const int32_t re = conversion_buffer[i].real();
        const int32_t im = conversion_buffer[i].imag();
        return static_cast<int64_t>(re * re + im * im);
    };
    const uint32_t window = std::min(ENERGY_WINDOW_SAMPLES, sample_count);
    int64_t sum = 0;
    for (uint32_t i = 0; i < window; ++i) {
        sum += power(i);
    }
    int64_t max_sum = sum;
    for (uint32_t i = window; i < sample_count; ++i) {
        sum += power(i) - power(i - window);
        max_sum = std::max(max_sum, sum);
    }
    // mean power of that window relative to the full-scale vector magnitude, the scale frame RSSI uses
    const double peak_power = static_cast<double>(max_sum) / window;
    const double dbfs = 10.0 * std::log10(std::max(peak_power, 1e-3) / (ADC_FULL_SCALE * ADC_FULL_SCALE));

    std::lock_guard<std::mutex> lock(energy_mutex);
    energy.push_back(static_cast<float>(dbfs));
}
