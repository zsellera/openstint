#include "sdr_device.hpp"
#include <iostream>

#ifdef HAVE_HACKRF
#include "sdr_hackrf.hpp"
#endif

#ifdef HAVE_RTLSDR
#include "sdr_rtlsdr.hpp"
#endif

std::unique_ptr<SdrDevice> create_sdr_device(SdrBackend backend) {
    switch (backend) {
        case SdrBackend::HackRF:
#ifdef HAVE_HACKRF
            return std::make_unique<SdrHackRF>();
#else
            std::cerr << "Error: HackRF support not compiled in. Rebuild with -DUSE_HACKRF=ON\n";
            return nullptr;
#endif

        case SdrBackend::RTL_SDR:
#ifdef HAVE_RTLSDR
            return std::make_unique<SdrRTLSDR>();
#else
            std::cerr << "Error: RTL-SDR support not compiled in. Rebuild with -DUSE_RTLSDR=ON\n";
            return nullptr;
#endif

        default:
            std::cerr << "Error: Unknown SDR backend\n";
            return nullptr;
    }
}
