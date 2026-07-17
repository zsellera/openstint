# Raspberry Pi Setup Tutorial

## Prerequisites

- Raspberry Pi 3 Model B+ (or newer)
- MicroSD card (16GB or larger)
- HackRF One SDR

## 1. Prepare SD Card

Flash Raspberry Pi OS (64-bit Lite recommended) to your SD card. See [official Raspberry Pi imaging guide](https://www.raspberrypi.com/documentation/computers/getting-started.html#raspberry-pi-imager).

Enable SSH during imaging for headless setup. Set up Wifi before imaging if possible

**Note: call your user `pi` to use the provided systemd files without modification**

## 2. Initial Setup

Boot the Pi, connect via SSH, and update the system:

```bash
sudo apt-get update
sudo apt-get upgrade -y
```

## 3. Install Dependencies & Compile

Tools to compile (ubuntu/raspbian/etc.):
```shell
sudo apt-get install build-essential cmake pkg-config ninja-build git
```

Install its dependencies:
```shell
sudo apt-get install hackrf libhackrf-dev librtlsdr-dev libliquid-dev libzmq3-dev cppzmq-dev libfec0 libfec-dev
```

Then checkout this repo, and build with cmake/make (`Release` build enables `-O3` compiler flag, improves performance significantly):
```shell
git clone https://github.com/zsellera/openstint.git
cd openstint
cmake -DCMAKE_BUILD_TYPE=Release .
make
./src/openstint_rtlsdr -g 20  # or ./src/openstint_hackrf -l 20 -v 20
```

## 4. Install Services

Install `bridge-zround` dependencies:
```bash
sudo apt-get install python3 python3-zmq
```

Copy the systemd service files:

```bash
sudo cp systemd/openstint.service /etc/systemd/system/
sudo cp systemd/bridge-zround.service /etc/systemd/system/
sudo systemctl daemon-reload
```

Enable auto-start on boot (enable the bridges you need):

```bash
sudo systemctl enable openstint.service
sudo systemctl enable bridge-zround.service
```

Start the services:

```bash
sudo systemctl start openstint.service
sudo systemctl start bridge-zround.service
```

## 5. Verify

Check service status:

```bash
sudo systemctl status openstint.service
sudo systemctl status bridge-zround.service
```

View logs:

```bash
sudo journalctl -u openstint.service -f
sudo journalctl -u bridge-zround.service -f
```

## Configuration

Edit service files to customize openstint arguments (gain, bias-tee, etc.):

```bash
sudo nano /etc/systemd/system/openstint.service
sudo systemctl daemon-reload
sudo systemctl restart openstint.service
```

See [README.md](https://github.com/zsellera/openstint/blob/master/README.md) for available command-line arguments.
