# OpenStint Laptiming Decoder

<img src="docs/logo.svg" alt="OpenStint logo" width="200"/>

OpenSint is a software defined radio (SDR) based laptiming decoder, currently implemented with HackRF One. It works both with its own [transponder protocol](docs/transponder-protocol.md) and with AMB/RC3-based transponders. It can run on a Raspberry Pi 3b. Only a minimal electronics knowledge is required; touching a soldering iron is optional.

To learn more how SDR works, watch [Andreas Spiess](https://www.youtube.com/watch?v=xQVm-YTKR9s) explaining it.

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

Note on Mac: we can't `brew install libfec`, compile and install it [from source](https://github.com/fblomqvi/libfec).

If this is your first rodeo, `sudo apt-get install cmake build-essentials libtool autoconf` as well.

## Integrations

The primary method of 3rd-party integration with OpenStint is via ZeroMQ. The `openstint` process listens by default on port `:5556`, and acts as a ZeroMQ PUBLISHER for arbitrary number of clients.

The [decoder protocol](docs/decoder-protocol.md) is documented under the `docs/` directory.

Find some built-in integrations in the `integrations/` directory. Two notable are `subscriber.py` and `laptimer.py`, both being ChatGPT-generared without any modifications.

## Command line arguments

TODO

## Future plans (some sort of a roadmap)

* Increase compatibilty with existing software by implementing an OpenStint-P3 bridge
* RTL-SDR support (inexpensive software defined radio)
* Adaptive equalization to decrease EVM
* Command-and-control interface and tooling
