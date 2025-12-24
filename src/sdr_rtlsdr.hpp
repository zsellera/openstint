#pragma once

#include "sdr_device.hpp"
#include <atomic>
#include <complex>
#include <liquid/liquid.h>
#include <rtl-sdr.h>
#include <string>
#include <thread>
#include <vector>

class SdrRTLSDR : public SdrDevice {
public:
  SdrRTLSDR();
  ~SdrRTLSDR() override;

  // Device lifecycle
  bool initialize() override;
  bool open(const char *serial) override;
  bool configure(const SdrConfig &config) override;
  bool start_rx(SdrCallback callback) override;
  bool stop_rx() override;
  bool close() override;
  bool is_streaming() const override;

  // Device information
  std::string get_device_info() const override;
  std::string get_backend_name() const override { return "RTL-SDR"; }

  // Error handling
  std::string get_last_error() const override { return last_error; }

private:
  rtlsdr_dev_t *device;
  SdrCallback user_callback;
  std::atomic<bool> streaming;
  std::thread rx_thread;
  bool is_v4 = false;
  std::string last_error;
  std::string device_info;

  // Sample format conversion buffer (unsigned to signed)
  std::vector<std::complex<int8_t>> conversion_buffer;

  // Resampling support (for hardware that doesn't support 5 MSPS)
  resamp_crcf upsampler = nullptr;
  float upsample_rate = 1.0f;
  std::vector<std::complex<float>> float_buffer;
  std::vector<std::complex<float>> resample_tmp_buffer;

  std::complex<float> oscillator = 1.0f;
  std::complex<float> oscillator_step = 1.0f;
  static constexpr float TUNAL_OFFSET = 250000.0f; // 250 kHz offset
  bool invert_iq = false;

  // Static callback for RTL-SDR C API
  static void rx_callback_wrapper(unsigned char *buf, uint32_t len, void *ctx);

  // Helper to map unified gain to RTL-SDR tuner gain
  void map_gain(uint8_t unified_gain);
};
