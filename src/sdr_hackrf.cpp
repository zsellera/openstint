#include "sdr_hackrf.hpp"
#include <cstdio>
#include <format>

SdrHackRF::SdrHackRF()
    : device(nullptr)
    , streaming(false)
{
}

SdrHackRF::~SdrHackRF() {
    if (device) {
        close();
    }
}

bool SdrHackRF::initialize() {
    int result = hackrf_init();
    if (result != HACKRF_SUCCESS) {
        last_error = std::format("hackrf_init() failed: {} ({})",
            hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        return false;
    }
    return true;
}

bool SdrHackRF::open(const char* serial) {
    int result = hackrf_open_by_serial(serial, &device);
    if (result != HACKRF_SUCCESS || device == nullptr) {
        last_error = std::format("hackrf_open() failed: {} ({})",
            hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        return false;
    }

    // Read device serial number for info
    read_partid_serialno_t serno;
    result = hackrf_board_partid_serialno_read(device, &serno);
    if (result == HACKRF_SUCCESS) {
        device_info = std::format("HackRF One SerNo.: {:08x}{:08x}{:08x}{:08x}",
            serno.serial_no[0], serno.serial_no[1], serno.serial_no[2], serno.serial_no[3]);
    } else {
        device_info = "HackRF One (serial read failed)";
    }

    return true;
}

bool SdrHackRF::configure(const SdrConfig& config) {
    int result;

    // Set center frequency
    result = hackrf_set_freq(device, config.center_freq_hz);
    if (result != HACKRF_SUCCESS) {
        last_error = std::format("hackrf_set_freq() failed: {} ({})",
            hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        return false;
    }

    // Set sample rate
    result = hackrf_set_sample_rate(device, config.sample_rate);
    if (result != HACKRF_SUCCESS) {
        last_error = std::format("hackrf_set_sample_rate() failed: {} ({})",
            hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        return false;
    }

    // Set baseband filter bandwidth
    result = hackrf_set_baseband_filter_bandwidth(device, config.baseband_filter_bw);
    if (result != HACKRF_SUCCESS) {
        last_error = std::format("hackrf_set_baseband_filter_bandwidth() failed: {} ({})",
            hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        return false;
    }

    // Set LNA gain
    result = hackrf_set_lna_gain(device, config.lna_gain);
    if (result != HACKRF_SUCCESS) {
        std::fprintf(stderr, "Warning: hackrf_set_lna_gain() failed: %s (%d)\n",
            hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
    }

    // Set VGA gain
    result = hackrf_set_vga_gain(device, config.vga_gain);
    if (result != HACKRF_SUCCESS) {
        std::fprintf(stderr, "Warning: hackrf_set_vga_gain() failed: %s (%d)\n",
            hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
    }

    // Enable/disable amplifier
    result = hackrf_set_amp_enable(device, config.amp_enable ? 1 : 0);
    if (result != HACKRF_SUCCESS) {
        std::fprintf(stderr, "Warning: hackrf_set_amp_enable() failed: %s (%d)\n",
            hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
    }

    // Enable/disable bias-tee
    result = hackrf_set_antenna_enable(device, config.bias_tee ? 1 : 0);
    if (result != HACKRF_SUCCESS) {
        std::fprintf(stderr, "Warning: hackrf_set_antenna_enable() failed: %s (%d)\n",
            hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
    }

    return true;
}

bool SdrHackRF::start_rx(SdrCallback callback) {
    user_callback = callback;

    int result = hackrf_start_rx(device, rx_callback_wrapper, this);
    if (result != HACKRF_SUCCESS) {
        last_error = std::format("hackrf_start_rx() failed: {} ({})",
            hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        return false;
    }

    streaming = true;
    return true;
}

bool SdrHackRF::stop_rx() {
    if (!device || !streaming) {
        return true;
    }

    int result = hackrf_stop_rx(device);
    if (result != HACKRF_SUCCESS) {
        last_error = std::format("hackrf_stop_rx() failed: {} ({})",
            hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        return false;
    }

    streaming = false;
    return true;
}

bool SdrHackRF::close() {
    if (!device) {
        return true;
    }

    stop_rx();

    int result = hackrf_close(device);
    if (result != HACKRF_SUCCESS) {
        last_error = std::format("hackrf_close() failed: {} ({})",
            hackrf_error_name(static_cast<enum hackrf_error>(result)), result);
        device = nullptr;
        return false;
    }

    device = nullptr;
    hackrf_exit();
    return true;
}

bool SdrHackRF::is_streaming() const {
    if (!device) {
        return false;
    }
    return hackrf_is_streaming(device) == HACKRF_TRUE;
}

std::string SdrHackRF::get_device_info() const {
    return device_info;
}

// Static callback wrapper for HackRF C API
int SdrHackRF::rx_callback_wrapper(hackrf_transfer* transfer) {
    auto* self = static_cast<SdrHackRF*>(transfer->rx_ctx);

    if (!self || !self->user_callback) {
        return 0;
    }

    // HackRF provides signed int8 IQ samples (no conversion needed)
    uint32_t sample_count = transfer->valid_length / 2;
    const std::complex<int8_t>* samples = reinterpret_cast<const std::complex<int8_t>*>(transfer->buffer);

    // Invoke user callback
    self->user_callback(samples, sample_count);

    return 0; // Continue streaming
}
