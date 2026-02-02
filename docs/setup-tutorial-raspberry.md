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

Follow the compilation instructions in [README.md](../README.md):

```bash
sudo apt-get install -y cmake build-essential libtool autoconf \
    hackrf libhackrf-dev libliquid-dev cppzmq-dev libfec-dev \
    python3 python3-zmq git

cd ~
git clone https://github.com/zsellera/openstint.git
cd openstint
cmake .
make
```

## 4. Install Services

Copy the systemd service files:

```bash
sudo cp systemd/openstint.service /etc/systemd/system/
sudo cp systemd/bridge-zround.service /etc/systemd/system/
sudo cp systemd/bridge-p3.service /etc/systemd/system/
sudo systemctl daemon-reload
```

Enable auto-start on boot (enable the bridges you need):

```bash
sudo systemctl enable openstint.service
sudo systemctl enable bridge-zround.service
sudo systemctl enable bridge-p3.service
```

Start the services:

```bash
sudo systemctl start openstint.service
sudo systemctl start bridge-zround.service
sudo systemctl start bridge-p3.service
```

## 5. Verify

Check service status:

```bash
sudo systemctl status openstint.service
sudo systemctl status bridge-zround.service
sudo systemctl status bridge-p3.service
```

View logs:

```bash
sudo journalctl -u openstint.service -f
sudo journalctl -u bridge-zround.service -f
sudo journalctl -u bridge-p3.service -f
```

## Configuration

Edit service files to customize openstint arguments (gain, bias-tee, etc.):

```bash
sudo nano /etc/systemd/system/openstint.service
sudo systemctl daemon-reload
sudo systemctl restart openstint.service
```

See [README.md](../README.md) for available command-line arguments.
