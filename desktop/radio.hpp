#pragma once

#include <atomic>
#include <complex>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rtl-sdr.h>

struct RtlSdrDeviceInfo {
    uint32_t index;
    std::string label;   // human readable, for the device selector
    bool likely_v4;      // from the USB product string; confirmed by the tuner type on start
};

// lists the connected RTL-SDR dongles (devices already opened by someone may be missing details)
std::vector<RtlSdrDeviceInfo> enumerate_rtlsdr_devices();

// Owns one opened RTL-SDR and its streaming thread. Every received buffer is fed
// to the decoder (detect_frames()); every Nth buffer is also measured for the
// peak energy of a 64-symbol window, which the GUI drains with take_energy().
class RtlSdrRadio {
public:
    ~RtlSdrRadio();

    // opens and configures the device, then starts streaming. returns false and
    // fills `error` on failure.
    bool start(uint32_t index, int gain_tenths_db, bool bias_tee, std::string& error);
    void stop();

    bool running() const { return device != nullptr; }   // opened by start(), until stop()
    bool streaming() const { return is_streaming; }        // false once the rx thread ended on its own
    bool direct_sampling() const { return direct; }        // non-V4 dongle: Q-branch, no tuner gain
    const std::string& description() const { return desc; }
    int64_t ms_since_last_rx() const;

    int set_gain(int gain_tenths_db); // returns the gain actually set, tenths of dB
    void set_bias_tee(bool enabled);

    std::vector<float> take_energy(); // peak window energies (dBFS) measured since the last call

private:
    static void rx_callback(unsigned char* buf, uint32_t len, void* ctx);
    void on_samples(const unsigned char* buf, uint32_t len);
    bool fail(const std::string& what, int code, std::string& error);

    rtlsdr_dev_t* device = nullptr;
    std::thread rx_thread;
    std::atomic<bool> is_streaming{false};
    std::atomic<int64_t> last_rx_ms{0};
    bool direct = false;
    std::string desc;

    std::vector<std::complex<int8_t>> conversion_buffer;
    uint32_t buffer_counter = 0;

    std::mutex energy_mutex;
    std::vector<float> energy;
};
