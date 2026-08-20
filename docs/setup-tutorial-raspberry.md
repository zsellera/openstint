# Raspberry Pi Setup Tutorial

## Prerequisites

- Raspberry Pi 3 Model B+ (or newer)
- MicroSD card (16GB or larger)
- RTL-SDR or HackRF One SDR

## 1. Prepare SD Card

Flash Raspberry Pi OS (64-bit Lite recommended) to your SD card. See [official Raspberry Pi imaging guide](https://www.raspberrypi.com/documentation/computers/getting-started.html#raspberry-pi-imager).

Enable SSH during imaging for headless setup. Set up Wifi before imaging if possible.

**64-bit Trixie (Debian 13) or newer.** 

On any other platform, see [compile from source](#compile-from-source-alternative) below.

## 2. Initial Setup

Boot the Pi, connect via SSH, and update the system:

```bash
sudo apt-get update
sudo apt-get upgrade -y
```

## 3. Install OpenStint

Add the package repository and install:

```bash
echo 'deb [trusted=yes arch=arm64] https://repo.lapbeeps.com/apt/ /' | sudo tee /etc/apt/sources.list.d/openstint.list
sudo apt update
sudo apt install openstint
```

That is the whole install. It pulls in the SDR libraries and Python
dependencies, then enables and starts `openstint.service`.

It's using the RTL-SDR implementation by default, so if it's plugged it, this shows it already working:

```bash
systemctl status openstint
```

## 4. Configuration

Arguments live in `/etc/openstint.conf`, **not in the unit file**:

```bash
sudo vim /etc/openstint.conf
sudo systemctl restart openstint.service
```

```ini
OPENSTINT_ARGS="-g 20 -s /var/lib/openstint"
```

To use a HackRF instead of an RTL-SDR, switch the `ExecStart` line — the unit
ships with the HackRF variant commented out — and set the HackRF gain flags in
`/etc/openstint.conf` (`-l`/`-v`, not `-g`):

```bash
sudo systemctl edit --full openstint.service
```

## 5. zRound bridge

The bridges in `/usr/share/openstint/integrations/` are all installed, but
**`bridge-zround.service` ships disabled**: which bridge you want depends on
your timing software. Enable the one you need:

```bash
sudo systemctl enable --now bridge-zround.service
```

See [ZRound](scoring-zround.md), [RCGTiming](scoring-rcgtiming.md) and
[LapBeeps](scoring-lapbeeps.md) for the per-back-end notes.

## 6. Verify

```bash
sudo systemctl status openstint.service
sudo journalctl -u openstint.service -f
```

## Learning mode .rc4 files

Learning-mode `*.rc4` files are saved to `/var/lib/openstint`. This library is user-writable (no need for `sudo` to modify it).

## Compile from source (alternative)

For development, for platforms other than 64-bit Raspberry Pi OS, or to change
build flags such as `SAMPLES_PER_SYMBOL`.

Tools to compile (ubuntu/raspbian/etc.):
```shell
sudo apt-get install build-essential cmake pkg-config ninja-build git
```

Install its dependencies:
```shell
sudo apt-get install libusb-1.0-0-dev libzmq3-dev cppzmq-dev libliquid-dev libfec-dev
```

CMake fetches rtl-sdr and hackrf itself, each at a pinned revision, and links
them statically. That is deliberate: hardware support lives inside the driver,
so the version Debian ships decides which dongles work. The libraries above are
used as packaged.

Install these to have the udev rules and device-specific CLI:
```shell
sudo apt-get install rtl-sdr hackrf
```

Then checkout this repo, and build with cmake/ninja (`Release` build enables `-O3` compiler flag, improves performance significantly). The first build also compiles the two vendored libraries, so give it a few minutes on a Pi:
```shell
git clone https://github.com/zsellera/openstint.git
cd openstint
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -B build .
ninja -C build
./build/src/openstint_rtlsdr -g 20  # or ./build/src/openstint_hackrf -l 20 -v 20
```

### Services, from source

Install `bridge-zround` dependencies:
```bash
sudo apt-get install python3 python3-zmq
```

The unit files in `systemd/` are the build-from-source variants: they run as
`User=pi` out of `/home/pi/openstint`, so name your user `pi` to use them
unmodified. (The packaged units in `packaging/` are the ones the `.deb`
installs; they differ deliberately.)

```bash
sudo cp systemd/openstint.service /etc/systemd/system/
sudo cp systemd/bridge-zround.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now openstint.service
sudo systemctl enable --now bridge-zround.service
```

With this path, openstint arguments are edited in the unit file itself:

```bash
sudo nano /etc/systemd/system/openstint.service
sudo systemctl daemon-reload
sudo systemctl restart openstint.service
```

See [README.md](https://github.com/zsellera/openstint/blob/master/README.md) for available command-line arguments.
