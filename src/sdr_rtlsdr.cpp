#include "sdr_rtlsdr.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

  // Detect RTL-SDR Blog V4
  // Check for "V4" in strings or the "R828D" tuner which is V4-exclusive
  if (device_info.find("V4") != std::string::npos ||
      std::string(manufact).find("Blog V4") != std::string::npos ||
      std::string(product).find("Blog V4") != std::string::npos) {
    is_v4 = true;
  }

  return true;
}

bool SdrRTLSDR::configure(const SdrConfig &config) {
  int result;

  // Enable direct sampling for frequencies below 24 MHz (like 5 MHz)
  // Direct sampling mode 2 = Q-branch
  // Note: RTL-SDR Blog V4 should NOT use direct sampling for HF (it has an
  // upconverter)
  if (config.center_freq_hz < 24000000 && !is_v4 &&
      config.direct_sampling_enabled) {
    result = rtlsdr_set_direct_sampling(device, 2);
    if (result != 0) {
      last_error = "Failed to enable direct sampling mode (Q-branch)";
      return false;
    }
  } else if (is_v4 && config.center_freq_hz < 24000000) {
    std::fprintf(stderr, "RTL-SDR Blog V4 detected: using upconverter for HF "
                         "(direct sampling disabled)\n");
  } else if (!config.direct_sampling_enabled &&
             config.center_freq_hz < 24000000) {
    std::fprintf(stderr, "Direct sampling manually disabled for HF\n");
  }

  // Set center frequency with offset tuning to avoid DC spike
  // We tune 250 kHz lower, pushing the DC spike -250kHz away from our target
  // Then we digitally mix the signal back to 0Hz
  uint32_t hardware_freq = config.center_freq_hz;
  if (config.center_freq_hz > TUNAL_OFFSET) {
    hardware_freq -= (uint32_t)TUNAL_OFFSET;
  }
  std::fprintf(
      stderr,
      "[DEBUG] Offset tuning ENABLED. Hardware freq: %u Hz (target: %llu Hz)\n",
      hardware_freq, config.center_freq_hz);

  result = rtlsdr_set_center_freq(device, hardware_freq);
  if (result != 0) {
    last_error =
        std::format("Failed to set center frequency to {} Hz", hardware_freq);
    return false;
  }

  // Set sample rate
  // Note: RTL-SDR hardware is generally unstable above 2.56 MSPS.
  // We use a 2:1 software upsampler to reach the required 5.0 MSPS.
  uint32_t target_rate = config.sample_rate;
  uint32_t hardware_rate = target_rate;

  if (target_rate == 5000000) {
    hardware_rate = 2500000;
    std::fprintf(stderr, "RTL-SDR optimization: Using 2.5 MSPS hardware rate "
                         "with 2:1 upsampler to reach 5.0 MSPS\n");
  }

  result = rtlsdr_set_sample_rate(device, hardware_rate);
  if (result != 0) {
    last_error =
        std::format("Failed to set sample rate to {} Hz", hardware_rate);
    return false;
  }

  if (hardware_rate < target_rate) {
    // Initialize resampler (upsample)
    upsample_rate = (float)target_rate / (float)hardware_rate;
    unsigned int m = 7; // filter semi-length
    float as = 60.0f;   // stop-band attenuation [dB]

    if (upsampler) {
      resamp_crcf_destroy(upsampler);
    }
    // Narrow bandwidth to 0.45 / upsample_rate to suppress imaging artifacts
    upsampler =
        resamp_crcf_create(upsample_rate, m, 0.45f / upsample_rate, as, 32);
  } else {
    // success at native rate, clean up any old resampler
    if (upsampler) {
      resamp_crcf_destroy(upsampler);
      upsampler = nullptr;
    }
    upsample_rate = 1.0f;
  }

  // Initialize software mixer (Oscillator)
  // Signal is at +TUNAL_OFFSET in baseband, we need to shift by -TUNAL_OFFSET
  float phase_step = -2.0f * (float)M_PI * TUNAL_OFFSET / (float)hardware_rate;
  std::fprintf(stderr,
               "[DEBUG] Software mixer ENABLED. Phase step: %f rad/sample\n",
               phase_step);

  if (config.invert_iq) {
    invert_iq = true;
    // When IQ is swapped, we use std::conj() in the callback to restore
    // the signal's spectral orientation and position (from -Offset to +Offset).
    // Therefore, the mixer should CONTINUE to shift by -Offset to bring it to
    // DC. So we do NOT reverse the phase_step.
    std::fprintf(stderr, "IQ Inversion Enabled: Spectral correction active.\n");
  } else {
    invert_iq = false;
  }

  oscillator_step = std::polar(1.0f, phase_step);
  oscillator = {1.0f, 0.0f};

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
  rx_thread = std::thread([this]() {
    // Optimized buffer parameters for lower latency:
    // 12 buffers * 16384 samples = ~196k samples total
    int result =
        rtlsdr_read_async(device, rx_callback_wrapper, this, 12, 16384 * 2);
    if (result != 0) {
      std::fprintf(stderr, "rtlsdr_read_async failed with error %d\n", result);
    }
    streaming = false;
  });

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

  if (rx_thread.joinable()) {
    rx_thread.join();
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
      float i_val = (static_cast<float>(buf[2 * i]) - 128.0f) / 128.0f;
      float q_val = (static_cast<float>(buf[2 * i + 1]) - 128.0f) / 128.0f;

      // Apply IQ Inversion if requested
      std::complex<float> s(i_val, q_val);
      if (self->invert_iq) {
        s = std::conj(s);
      }

      // Apply software mixer to shift frequency back
      self->float_buffer[i] = s * self->oscillator;

      // Advance oscillator
      self->oscillator *= self->oscillator_step;
    }
    // Normalize oscillator periodically to prevent drift (every block)
    self->oscillator /= std::abs(self->oscillator);

    // Calculate output size (upsample_rate * sample_count + filter delay)
    uint32_t out_count = (uint32_t)(self->upsample_rate * sample_count) + 32;
    if (self->resample_tmp_buffer.size() < out_count) {
      self->resample_tmp_buffer.resize(out_count);
    }
    if (self->conversion_buffer.size() < out_count) {
      self->conversion_buffer.resize(out_count);
    }

    uint32_t num_written = 0;
    resamp_crcf_execute_block(self->upsampler, self->float_buffer.data(),
                              sample_count, self->resample_tmp_buffer.data(),
                              &num_written);

    // Convert resampled floats back to int8 with SATURATION
    // This prevents "wrapping" distortion on strong signals
    for (uint32_t i = 0; i < num_written; ++i) {
      float r = self->resample_tmp_buffer[i].real() * 127.0f;
      float im = self->resample_tmp_buffer[i].imag() * 127.0f;

      // Manually clamp/saturate to prevent int8 overflow
      if (r > 127.0f)
        r = 127.0f;
      else if (r < -128.0f)
        r = -128.0f;

      if (im > 127.0f)
        im = 127.0f;
      else if (im < -128.0f)
        im = -128.0f;

      self->conversion_buffer[i] =
          std::complex<int8_t>(static_cast<int8_t>(r), static_cast<int8_t>(im));
    }

    self->user_callback(self->conversion_buffer.data(), num_written);
  } else {
    // Direct path (unsigned -> signed)
    if (self->conversion_buffer.size() < sample_count) {
      self->conversion_buffer.resize(sample_count);
    }

    for (uint32_t i = 0; i < sample_count; ++i) {
      float i_val = (static_cast<float>(buf[2 * i]) - 128.0f) / 128.0f;
      float q_val = (static_cast<float>(buf[2 * i + 1]) - 128.0f) / 128.0f;

      // Apply IQ Inversion and Mixer
      std::complex<float> s(i_val, q_val);
      if (self->invert_iq) {
        s = std::conj(s);
      }
      s *= self->oscillator;

      // Advance oscillator
      self->oscillator *= self->oscillator_step;

      // Convert back to int8
      float r = s.real() * 127.0f;
      float im = s.imag() * 127.0f;
      // Manually clamp/saturate
      if (r > 127.0f)
        r = 127.0f;
      else if (r < -128.0f)
        r = -128.0f;
      if (im > 127.0f)
        im = 127.0f;
      else if (im < -128.0f)
        im = -128.0f;

      self->conversion_buffer[i] =
          std::complex<int8_t>(static_cast<int8_t>(r), static_cast<int8_t>(im));
    }
    // Normalize oscillator
    self->oscillator /= std::abs(self->oscillator);

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
