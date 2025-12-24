#include "sdr_rtlsdr.hpp"
#include <cstdio>
#include <cstring>
#include <format>

SdrRTLSDR::SdrRTLSDR() : device(nullptr), streaming(false) {}

SdrRTLSDR::~SdrRTLSDR() {
  if (device) {
    close();
  }
}

bool SdrRTLSDR::initialize() {
  // RTL-SDR doesn't need separate initialization
  return true;
}

bool SdrRTLSDR::open(const char *serial) {
  int device_count = rtlsdr_get_device_count();
  if (device_count == 0) {
    last_error = "No RTL-SDR devices found";
    return false;
  }

  int device_index = 0;

  // If serial number specified, find matching device
  if (serial != nullptr) {
    bool found = false;
    for (int i = 0; i < device_count; i++) {
      char manufact[256], product[256], sn[256];
      if (rtlsdr_get_device_usb_strings(i, manufact, product, sn) == 0) {
        if (std::strcmp(serial, sn) == 0) {
          device_index = i;
          found = true;
          break;
        }
      }
    }
    if (!found) {
      last_error = std::format("RTL-SDR with serial {} not found", serial);
      return false;
    }
  }

  // Open device
  int result = rtlsdr_open(&device, device_index);
  if (result != 0) {
    last_error = std::format("Failed to open RTL-SDR device #{}", device_index);
    return false;
  }

  // Get device info
  const char *name = rtlsdr_get_device_name(device_index);
  char manufact[256], product[256], sn[256];
  if (rtlsdr_get_device_usb_strings(device_index, manufact, product, sn) == 0) {
    device_info = std::format("{} (SN: {})", name, sn);
  } else {
    device_info = std::format("{}", name);
  }

  return true;
}

bool SdrRTLSDR::configure(const SdrConfig &config) {
  int result;

  // Enable direct sampling for frequencies below 24 MHz (like 5 MHz)
  // Direct sampling mode 2 = Q-branch
  if (config.center_freq_hz < 24000000) {
    result = rtlsdr_set_direct_sampling(device, 2);
    if (result != 0) {
      last_error = "Failed to enable direct sampling mode (Q-branch)";
      return false;
    }
  }

  // Set center frequency
  result = rtlsdr_set_center_freq(device, config.center_freq_hz);
  if (result != 0) {
    last_error = std::format("Failed to set center frequency to {} Hz",
                             config.center_freq_hz);
    return false;
  }

  // Set sample rate
  result = rtlsdr_set_sample_rate(device, config.sample_rate);
  if (result != 0) {
    // Fallback: Try 2.5 MHz and upsample if 5 MHz fails
    uint32_t fallback_rate = 2500000;
    std::fprintf(
        stderr,
        "Warning: Failed to set sample rate to %u Hz, trying fallback %u Hz\n",
        config.sample_rate, fallback_rate);

    result = rtlsdr_set_sample_rate(device, fallback_rate);
    if (result != 0) {
      last_error = std::format("Failed to set sample rate to {} Hz or {} Hz",
                               config.sample_rate, fallback_rate);
      return false;
    }

    // Initialize resampler (upsample by 2.0x)
    upsample_rate = (float)config.sample_rate / (float)fallback_rate;
    unsigned int m = 7; // filter semi-length
    float as = 60.0f;   // stop-band attenuation [dB]

    if (upsampler) {
      resamp_crcf_destroy(upsampler);
    }
    upsampler = resamp_crcf_create(upsample_rate, m, 0.45f, as, 32);
    std::fprintf(stderr, "Initialized 2:1 upsampler (%.2f MSPS -> %.2f MSPS)\n",
                 fallback_rate / 1e6, config.sample_rate / 1e6);
  } else {
    // success at native rate, clean up any old resampler
    if (upsampler) {
      resamp_crcf_destroy(upsampler);
      upsampler = nullptr;
    }
    upsample_rate = 1.0f;
  }

  // Configure gain
  map_gain(config.unified_gain);

  // Enable bias-tee if requested
  if (config.bias_tee) {
    result = rtlsdr_set_bias_tee(device, 1);
    if (result != 0) {
      std::fprintf(
          stderr,
          "Warning: Failed to enable bias-tee (may not be supported)\n");
    }
  }

  // Reset buffer to clear any stale data
  rtlsdr_reset_buffer(device);

  return true;
}

bool SdrRTLSDR::start_rx(SdrCallback callback) {
  user_callback = callback;
  streaming = true;

  // Start async reading in background thread
  // The rtlsdr_read_async call blocks, so we run it in the calling thread
  // Note: This is different from HackRF which uses its own thread
  int result = rtlsdr_read_async(device, rx_callback_wrapper, this, 0, 0);

  streaming = false;

  if (result != 0) {
    last_error = std::format("rtlsdr_read_async failed with error {}", result);
    return false;
  }

  return true;
}

bool SdrRTLSDR::stop_rx() {
  if (!device || !streaming) {
    return true;
  }

  // Cancel async reading
  int result = rtlsdr_cancel_async(device);
  if (result != 0) {
    last_error = std::format("Failed to cancel async reading: {}", result);
    return false;
  }

  streaming = false;
  return true;
}

bool SdrRTLSDR::close() {
  if (!device) {
    return true;
  }

  stop_rx();

  int result = rtlsdr_close(device);
  if (result != 0) {
    last_error = std::format("Failed to close RTL-SDR device: {}", result);
  }

  if (upsampler) {
    resamp_crcf_destroy(upsampler);
    upsampler = nullptr;
  }

  device = nullptr;
  return result == 0;
}

bool SdrRTLSDR::is_streaming() const { return streaming; }

std::string SdrRTLSDR::get_device_info() const { return device_info; }

// Static callback wrapper for RTL-SDR C API
void SdrRTLSDR::rx_callback_wrapper(unsigned char *buf, uint32_t len,
                                    void *ctx) {
  auto *self = static_cast<SdrRTLSDR *>(ctx);

  if (!self || !self->user_callback) {
    return;
  }

  // RTL-SDR provides unsigned uint8 samples (I,Q interleaved)
  // Convert to signed float for resampler: signed = (unsigned - 127) / 128.0
  uint32_t sample_count = len / 2;

  if (self->upsampler) {
    // Upsampling path (e.g. 2.5 MSPS -> 5.0 MSPS)
    if (self->float_buffer.size() < sample_count) {
      self->float_buffer.resize(sample_count);
    }

    for (uint32_t i = 0; i < sample_count; ++i) {
      float i_val = (static_cast<float>(buf[2 * i]) - 127.0f) / 128.0f;
      float q_val = (static_cast<float>(buf[2 * i + 1]) - 127.0f) / 128.0f;
      self->float_buffer[i] = std::complex<float>(i_val, q_val);
    }

    // Calculate output size (upsample_rate * sample_count + filter delay)
    uint32_t out_count = (uint32_t)(self->upsample_rate * sample_count) + 16;
    if (self->conversion_buffer.size() < out_count) {
      self->conversion_buffer.resize(out_count);
    }

    std::vector<std::complex<float>> resampled_floats(out_count);
    uint32_t num_written = 0;
    resamp_crcf_execute_block(self->upsampler, self->float_buffer.data(),
                              sample_count, resampled_floats.data(),
                              &num_written);

    // Convert resampled floats back to int8
    for (uint32_t i = 0; i < num_written; ++i) {
      int8_t i_val = static_cast<int8_t>(resampled_floats[i].real() * 127.0f);
      int8_t q_val = static_cast<int8_t>(resampled_floats[i].imag() * 127.0f);
      self->conversion_buffer[i] = std::complex<int8_t>(i_val, q_val);
    }

    self->user_callback(self->conversion_buffer.data(), num_written);
  } else {
    // Direct path (unsigned -> signed)
    if (self->conversion_buffer.size() < sample_count) {
      self->conversion_buffer.resize(sample_count);
    }

    for (uint32_t i = 0; i < sample_count; ++i) {
      int8_t i_val = static_cast<int8_t>(buf[2 * i] - 127);
      int8_t q_val = static_cast<int8_t>(buf[2 * i + 1] - 127);
      self->conversion_buffer[i] = std::complex<int8_t>(i_val, q_val);
    }

    self->user_callback(self->conversion_buffer.data(), sample_count);
  }
}

// Helper to map unified gain (0-100) to RTL-SDR tuner gain
void SdrRTLSDR::map_gain(uint8_t unified_gain) {
  // Get available tuner gains
  int num_gains = rtlsdr_get_tuner_gains(device, nullptr);
  if (num_gains <= 0) {
    std::fprintf(stderr, "Warning: Failed to get tuner gains\n");
    return;
  }

  std::vector<int> gains(num_gains);
  rtlsdr_get_tuner_gains(device, gains.data());

  // Map unified gain (0-100) to available gains
  int idx = (unified_gain * (num_gains - 1)) / 100;
  int gain_tenths_db = gains[idx];

  // Set manual gain mode
  rtlsdr_set_tuner_gain_mode(device, 1);

  // Set gain (in tenths of dB)
  int result = rtlsdr_set_tuner_gain(device, gain_tenths_db);
  if (result != 0) {
    std::fprintf(stderr, "Warning: Failed to set tuner gain\n");
  } else {
    std::fprintf(stderr, "RTL-SDR gain set to %.1f dB\n",
                 gain_tenths_db / 10.0);
  }
}
