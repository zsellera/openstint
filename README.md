# OpenStint Laptiming Decoder

<img src="docs/logo.svg" alt="OpenStint logo" width="200"/>

OpenStint is a software defined radio (SDR) based laptiming decoder, currently implemented with HackRF One. It works both with its own [transponder protocol](docs/transponder-protocol.md) and with AMB/RC3-based transponders. It can run on a Raspberry Pi 3 Model B+. Only a minimal electronics knowledge is required; touching a soldering iron is optional.

Side-projects: [openstint transponder](https://github.com/zsellera/openstint-transponder) | [antenna preamplifier](https://github.com/zsellera/openstint-preamp)

Check out the [track setup tutorial](docs/setup-tutorial) to get started. To learn more how SDR works, watch [Andreas Spiess](https://www.youtube.com/watch?v=xQVm-YTKR9s) explaining it.

## Quickstart (Ubuntu/RaspberryPi)

You have compile it from source. Install its dependencies first:

```shell
sudo apt-get install hackrf libhackrf libhackrf-dev libliquid libliquid-dev licppzmq cppzmq-dev libfec libfec-dev
```

Then checkout this repo, and build with cmake/make:
```shell
cmake .
make
./src/openstint
```
Vehicle passings are printed to `stdout` and published with ZeroMQ at `:5556`. The easiest method for testing with real transponders is with a *near-field magnetic probe* (sub-$10 stuff, search on ebay/aliexpress or see [Dave Jones DIY one](https://youtu.be/2xy3Hm1_ZqI?si=vmh87UB20cV0W4xt)).

To use goodies in the `integrations/` directory, `sudo apt-get install python3 python3-zmq` as well.

Note on Mac: we can't `brew install libfec`, compile and install it [from source](https://github.com/fblomqvi/libfec).

If this is your first rodeo, `sudo apt-get install cmake build-essentials libtool autoconf` as well.

## Integrations

The primary method of 3rd-party integration with OpenStint is via ZeroMQ. The `openstint` process listens by default on port `:5556`, and acts as a ZeroMQ PUBLISHER for arbitrary number of clients.

The [decoder protocol](docs/decoder-protocol.md) is documented under the `docs/` directory.

Find some built-in integrations in the `integrations/` directory. Two notable are `subscriber.py` and `laptimer.py`, both being ChatGPT-generared without any modifications.

## Command line arguments

```
openstint -h
Usage: openstint [-d ser_nr] [-p tcp_port] [-l <0..40>] [-v <0..62>] [-a] [-b] [-m]
	-d ser_nr   default:first   serial number of the desired HackRF
	-p port     default:5556	ZeroMQ publisher port
	-l <0..40>  default:24  	LNA gain (rf signal amplifier; valid values: 0/8/16/24/32/40)
	-v <0..62>  default:24  	VGA gain (baseband signal amplifier, steps of 2)
	-a          default:off 	Enable preamp (+13 dB to input RF signal)
	-b          default:off 	Enable bias-tee (+3.3 V, 50 mA max)
	-m          default:off 	Enable monitor mode (print received frames to stdout)
```

## Future plans (some sort of a roadmap)

* Increase compatibilty with existing software by implementing an OpenStint-P3 bridge
* RTL-SDR support (inexpensive software defined radio)
* Adaptive equalization to decrease EVM

**RC4 support is not pursued.** There are already multiple open-source transponder projects out there. One can order an assembled 2x10pcs OpenStint-compatible panel from JLCPCB for less than $100, including taxes and shipping. For a cost of a single brand-name transponder, a whole club can enjoy reliable laptiming. Then why bother decyphering a protocol which was designed to be and to remain closed?!?

## Contribution

Submit PRs according to the project's core values:

* it should be and remain a solution for small-scale clubs and friendly gatherings
* it should run on constrained hardware (Raspberry Pi 3 Model B+)
* the interfaces should be simple and well documented (to promote 3rd-party integration)
* "does one thing and one thing well" phylosophy

Otherwise, I'll to adhere myself to the [C4 community process](https://hintjens.gitbooks.io/social-architecture/content/chapter4.html).
