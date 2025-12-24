#pragma once

#include "sdr_device.hpp"
#include <libhackrf/hackrf.h>
#include <atomic>
#include <string>

class SdrHackRF : public SdrDevice {
public:
    SdrHackRF();
    ~SdrHackRF() override;

    // Device lifecycle
    bool initialize() override;
    bool open(const char* serial) override;
    bool configure(const SdrConfig& config) override;
    bool start_rx(SdrCallback callback) override;
    bool stop_rx() override;
    bool close() override;
    bool is_streaming() const override;

    // Device information
    std::string get_device_info() const override;
    std::string get_backend_name() const override { return "HackRF One"; }

    // Error handling
    std::string get_last_error() const override { return last_error; }

private:
    hackrf_device* device;
    SdrCallback user_callback;
    std::atomic<bool> streaming;
    std::string last_error;
    std::string device_info;

    // Static callback for HackRF C API
    static int rx_callback_wrapper(hackrf_transfer* transfer);
};
