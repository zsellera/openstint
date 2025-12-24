#pragma once

#include <complex>
#include <cstdint>
#include <string>
#include <memory>
#include <functional>

// Callback type: provides signed int8 IQ samples
using SdrCallback = std::function<void(const std::complex<int8_t>*, uint32_t)>;

// SDR configuration structure
struct SdrConfig {
    uint64_t center_freq_hz;
    uint32_t sample_rate;
    uint32_t baseband_filter_bw;
    uint8_t lna_gain;           // For HackRF: 0-40 in steps of 8
    uint8_t vga_gain;           // For HackRF: 0-62 in steps of 2
    uint8_t unified_gain;       // For RTL-SDR: 0-100
    bool amp_enable;            // Preamp/LNA boost
    bool bias_tee;              // Antenna power
    const char* device_serial;  // Device identifier (nullptr = first device)
};

// Abstract base class for SDR devices
class SdrDevice {
public:
    virtual ~SdrDevice() = default;

    // Device lifecycle
    virtual bool initialize() = 0;
    virtual bool open(const char* serial) = 0;
    virtual bool configure(const SdrConfig& config) = 0;
    virtual bool start_rx(SdrCallback callback) = 0;
    virtual bool stop_rx() = 0;
    virtual bool close() = 0;
    virtual bool is_streaming() const = 0;

    // Device information
    virtual std::string get_device_info() const = 0;
    virtual std::string get_backend_name() const = 0;

    // Error handling
    virtual std::string get_last_error() const = 0;
};

// SDR backend types
enum class SdrBackend {
    HackRF,
    RTL_SDR
};

// Factory function to create SDR device instances
std::unique_ptr<SdrDevice> create_sdr_device(SdrBackend backend);
