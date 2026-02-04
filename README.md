# OpenStint Laptiming Decoder

<img src="docs/logo.svg" alt="OpenStint logo" width="200"/>

OpenStint is a software defined radio (SDR) based laptiming decoder, using either *HackRF One* or *RTL-SDR v4*. It works both with its own [transponder protocol](docs/transponder-protocol.md) and with AMB/RC3-based transponders. It can run on a Raspberry Pi 3 Model B+. Only a minimal electronics knowledge is required; touching a soldering iron is optional.

* :tada: Supports OpenStint transponder, as well as RC3/RC4Hybrid/MRT and other RC3-clones
* :wrench: Off-the-shelf components, no electronic skills are required (HackRF One, RTL-SDR v4).
* :checkered_flag: Tested with [ZRound](https://www.zround.com/index.php/download-mananger/) and [LiveTime Scoring](https://www.livetimescoring.com/)
* :chart_with_downwards_trend: Low resource requirements: runs even on a Rapsberry Pi 3 Model B+
* :brain: Adaptive filters enchance reception quality

**Download:** [Windows users](docs/setup-tutorial-windows.md) can download precompiled binaries; the rest of us have to compile from source.

Side-projects: [openstint transponder](https://github.com/zsellera/openstint-transponder) | [antenna preamplifier](https://github.com/zsellera/openstint-preamp)

Check out the [track setup tutorial](docs/setup-tutorial.md) to get started. To learn more how SDR works, watch [Andreas Spiess](https://www.youtube.com/watch?v=xQVm-YTKR9s) explaining it.

## Compile from source
You have compile it from source. Install its dependencies first:

```shell
sudo apt-get install hackrf libhackrf libhackrf-dev librtlsdr-dev libliquid libliquid-dev libzmq3-dev cppzmq-dev libfec0 libfec-dev
```

Then checkout this repo, and build with cmake/make (`Release` build enables `-O3` compiler flag, improves performance significantly):
```shell
cmake -DCMAKE_BUILD_TYPE=Release .
make
./src/openstint_hackrf   # or ./src/openstint_rtlsdr
```

Vehicle passings are printed to `stdout` and published with ZeroMQ at `:5556`. The easiest method for testing with real transponders is with a *near-field magnetic probe* (sub-$10 stuff, search on ebay/aliexpress or see [Dave Jones DIY one](https://youtu.be/2xy3Hm1_ZqI?si=vmh87UB20cV0W4xt)).

To use goodies in the `integrations/` directory, `sudo apt-get install python3 python3-zmq` as well.

Note on Mac: we can't `brew install libfec`, compile and install it [from source](https://github.com/fblomqvi/libfec).

If this is your first rodeo, `sudo apt-get install cmake build-essentials libtool autoconf` as well.

HackRF One users: there is a build flag `SAMPLES_PER_SYMBOL`, default to `4`, resulting in 5 MSPS sampling rate. Better reception is achievable by setting it to `8` (10 MSPS) at the cost of higher CPU utilization. On resource-constrained environment, lower it to `2` (2.5 MSPS: lower CPU-usage, shittier reception). RTL-SDR maxes out at the required minimum of 2.5 MSPS (`SAMPLES_PER_SYMBOL=2`), there is no way to fine-tune that.

## Integrations

The primary method of 3rd-party integration with OpenStint is via ZeroMQ. The `openstint` process listens by default on port `:5556`, and acts as a ZeroMQ PUBLISHER for arbitrary number of clients.

The [decoder protocol](docs/decoder-protocol.md) is documented under the `docs/` directory.

Find some built-in integrations in the `integrations/` directory. Two notable are `bridge-zround.py` and `bridge-p3.py`.

## Command line arguments

### HackRF

```
openstint_hackrf -h
Usage: openstint_hackrf [-d ser_nr] [-l <0..40>] [-v <0..62>] [-a] [-b] [-p tcp_port] [-m] [-t]
	-d ser_nr   default:first	serial number of the desired HackRF
	-l <0..40>  default:24  	LNA gain (rf signal amplifier; valid values: 0/8/16/24/32/40)
	-v <0..62>  default:24  	VGA gain (baseband signal amplifier, steps of 2)
	-a          default:off 	Enable preamp (+13 dB to input RF signal)
	-b          default:off 	Enable bias-tee (+3.3 V, 50 mA max)
	-p port     default:5556	ZeroMQ publisher port
	-m          default:off 	Enable monitor mode (print received frames to stdout)
	-t          default:off 	Use system clock as the timebase (beware of NTP jumps)
```

### RTL-SDR

```
openstint_rtlsdr -h
Usage: openstint_rtlsdr [-d ser_nr] [-g <gain_dB>] [-D] [-b] [-p tcp_port] [-m] [-t]
	-d ser_nr   default:first	serial number of the desired RTL-SDR
	-g <dB>     default:20  	tuner gain in dB
	-b          default:off 	Enable bias-tee (+4.5 V)
	-p port     default:5556	ZeroMQ publisher port
	-m          default:off 	Enable monitor mode (print received frames to stdout)
	-t          default:off 	Use system clock as the timebase (beware of NTP jumps)
```

## HackRF One or RTL-SDR?

Initial results show 2-3 dB better performace with a HackRF One when compared to an RTL-SDR v4. Given the ~40 dB effective dynamic range of these devices, this is not noticable in practice (if it is, re-think the antenna setup). A HackRF One clone costs 2x more as an RTL-SDR v4 dongle; an original from Great Scott Gadgets is 6-7x more expensive.

For permanent setups, prefer the HackRF One though. The RTL-SDR dongle heats up considerably. I would not put it into an enclosed electrical box, and I would not leave it exposed to sunshine neither. The HackRF board is much less dense, thermal management is not a problem there.

## Contribution

Submit PRs according to the project's core values:

* it should be and remain a solution for small-scale clubs and friendly gatherings
* it should run on constrained hardware (Raspberry Pi 3 Model B+)
* the interfaces should be simple and well documented (to promote 3rd-party integration)
* "does one thing and one thing well" phylosophy

Otherwise, I'll to adhere myself to the [C4 community process](https://hintjens.gitbooks.io/social-architecture/content/chapter4.html).
