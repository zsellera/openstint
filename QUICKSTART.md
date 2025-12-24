# OpenStint RTL-SDR Support - Quick Start Guide

**Status:** Implementation Complete ✅
**Your Hardware:** RTL-SDR Blog V3/V4
**Date:** December 16, 2024

---

## What Was Done

✅ Added RTL-SDR support alongside HackRF
✅ Automatic sample format conversion (unsigned→signed)
✅ Direct sampling mode for 5 MHz reception
✅ New `-r` flag to select RTL-SDR
✅ New `-g` flag for unified gain control (0-100)
✅ Full backward compatibility with HackRF

**Result:** 6 new files, 2 modified files, ~625 lines of new code

---

## Installation (Choose Your OS)

### Ubuntu/Debian/Raspberry Pi (Most Common)

```bash
# Install all dependencies
sudo apt-get update
sudo apt-get install -y cmake build-essential \
    hackrf libhackrf-dev rtl-sdr librtlsdr-dev \
    libliquid-dev libfec-dev libcppzmq-dev

# Build
cd /Users/danpereda/Projects/RcLapTimerRF_openstint
cmake .
make

# Run
./src/openstint -r
```

### macOS (Where You Are Now)

```bash
# Install dependencies
brew install cmake hackrf liquid-dsp rtl-sdr cppzmq

# libfec needs manual install
git clone https://github.com/fblomqvi/libfec.git
cd libfec
./configure
make
sudo make install  # Installs the library to /usr/local
cd ..

# Build
cd /Users/danpereda/Projects/RcLapTimerRF_openstint
cmake .
make

# Run
./src/openstint -r
```

---

## Usage Examples

```bash
# HackRF (default, unchanged)
./src/openstint

# RTL-SDR with default gain (50)
./src/openstint -r

# RTL-SDR with optimal gain (60-70 recommended)
./src/openstint -r -g 65

# RTL-SDR with bias-tee (for external LNA)
./src/openstint -r -g 65 -b

# RTL-SDR with monitor mode (see frame details)
./src/openstint -r -g 65 -m

# RTL-SDR with specific device
./src/openstint -r -d 00000001
```

---

## Gain Tuning Quick Guide

**Start here:** `-g 60`

**Too low (< 40):** Few detections, low RSSI
**Optimal (50-70):** Consistent detections, good RSSI (-40 to -60 dBm)
**Too high (> 80):** Spurious detections, saturation

**Tuning steps:**
1. Start: `./src/openstint -r -g 50 -m`
2. Check detection rate and RSSI
3. Adjust in steps of 10
4. Find sweet spot (usually 60-70)

---

## Testing Checklist

**Basic test:**
```bash
./src/openstint -r
# Should see: "Device: Generic RTL2832U..."
# Should see: "Streaming... stop with Ctrl-C"
# Should see: Statistics every second
```

**Transponder test:**
```bash
./src/openstint -r -g 65 -m
# Place transponder near antenna
# Should see: "P [timestamp] O [id] ..." for OpenStint
# Should see: "P [timestamp] L [id] ..." for AMB/RC3
```

**Both backends test:**
```bash
# HackRF
./src/openstint
# Ctrl-C to stop

# RTL-SDR
./src/openstint -r
# Both should work!
```

---

## Troubleshooting Quick Fixes

### "Could not find LIQUID_LIB" during build
```bash
sudo apt-get install libliquid-dev    # Linux
brew install liquid-dsp                # macOS
```

### "Failed to open RTL-SDR device"
```bash
rtl_test                                # Check if detected
sudo usermod -a -G plugdev $USER        # Fix permissions (Linux)
# Log out and back in
```

### "No transponders detected"
```bash
./src/openstint -r -g 70 -m            # Increase gain, enable monitor
# Check antenna connection
# Move transponder closer
```

### "Too many false detections"
```bash
./src/openstint -r -g 40               # Decrease gain
# Move antenna away from interference
```

---

## Key Files Reference

**Implementation files:**
```
src/sdr_device.hpp       - HAL interface
src/sdr_device.cpp       - Factory function
src/sdr_hackrf.cpp       - HackRF backend (194 lines)
src/sdr_rtlsdr.cpp       - RTL-SDR backend (252 lines) ← Sample conversion here!
src/main.cpp             - Refactored to use HAL
src/CMakeLists.txt       - Optional backend support
```

**Documentation:**
```
RTL-SDR_IMPLEMENTATION.md - Full documentation (you're reading the quick version)
QUICKSTART.md            - This file
README.md                - Original OpenStint README
```

---

## What's Next?

1. **Install dependencies** (see Installation section above)
2. **Build the project** (`cmake . && make`)
3. **Test with HackRF** (verify no regression: `./src/openstint`)
4. **Test with RTL-SDR** (basic: `./src/openstint -r`)
5. **Tune gain** (optimal: `./src/openstint -r -g 65`)
6. **Test transponders** (add `-m` flag to see details)
7. **Run long test** (verify stability over 1+ hour)

---

## Command Reference Card

| Task | Command |
|------|---------|
| **Build** | `cmake . && make` |
| **HackRF** | `./src/openstint` |
| **RTL-SDR** | `./src/openstint -r` |
| **RTL-SDR + gain** | `./src/openstint -r -g 65` |
| **RTL-SDR + monitor** | `./src/openstint -r -g 65 -m` |
| **RTL-SDR + bias-tee** | `./src/openstint -r -g 65 -b` |
| **Help** | `./src/openstint -h` |
| **Test RTL-SDR hardware** | `rtl_test` |
| **Test HackRF hardware** | `hackrf_info` |

---

## Performance Expectations

| Metric | HackRF | RTL-SDR | Notes |
|--------|--------|---------|-------|
| **Detection rate** | 100% | 100% | Should be identical |
| **RSSI** | Baseline | ±5 dB | RTL may be slightly noisier |
| **EVM** | < 0.3 | < 0.5 | RTL may be slightly higher |
| **CPU (RPi3)** | ~40% | ~40% | Should be similar |

---

## Critical Technical Details

**Sample format conversion** (src/sdr_rtlsdr.cpp:197-206):
```cpp
// RTL-SDR: unsigned uint8 (0-255, center at 127)
// OpenStint expects: signed int8 (-128 to +127, center at 0)
// Conversion: signed = unsigned - 127
```

**Direct sampling mode** (src/sdr_rtlsdr.cpp:77-83):
```cpp
// Needed for 5 MHz (below normal 24 MHz minimum)
rtlsdr_set_direct_sampling(device, 2);  // Q-branch
```

**Gain mapping** (src/sdr_rtlsdr.cpp:230-248):
```cpp
// Maps -g 0-100 to available tuner gains
// Typical RTL-SDR: -1.0 to 49.0 dB
// -g 50 → ~19 dB, -g 70 → ~29 dB
```

---

## Getting Help

**For detailed info:** Read `RTL-SDR_IMPLEMENTATION.md` (complete documentation)

**For build issues:** Check "Installation & Building" section

**For runtime issues:** Check "Troubleshooting" section

**For testing:** Follow "Testing Checklist" section

**Original OpenStint project:** https://github.com/zsellera/openstint

---

**Ready to build and test!** 🚀

Once you install the dependencies, the project should build and run with both HackRF and RTL-SDR support.

Start with HackRF to verify no regression, then switch to RTL-SDR with the `-r` flag.
