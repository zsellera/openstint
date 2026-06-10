# Supported SDR Hardware

OpenStint works best with [HackRF One](https://greatscottgadgets.com/hackrf/one/) and [RTL-SDR v4](https://www.rtl-sdr.com/buy-rtl-sdr-dvb-t-dongles/). RTL-SDR v3 is also supported, but not recommended.

I recommend **RTL-SDR v4** due to its price/performance ratio.

To learn more how SDR works, watch [Andreas Spiess](https://www.youtube.com/watch?v=xQVm-YTKR9s) explaining it.

## HackRF One vs RTL-SDR?

The HackRF One has lower noise, better thermal performance, similar dynamic range and a 3x-10x pricetag. Pick the RTL-SDR v4, unless you're a radio nerd with some money to spend.

If you build a track with a permament loop and decoder, consider the HackRF One due to it's beter thermal management (more likely survives an installation in an enclosed electrical box).

## HackRF One

HackRF One is an open-source radio. The orignal is manufactured by [Great Scott Gadgets](https://greatscottgadgets.com/hackrf/one/), and costs $330. Clones around $130 exists. I have successfuly tested one from Opensourcesdr Labs and Circet. Tips:
* prefer the "clifford version", as it has extra input protection
* do not purchase a portapack, as it is useless for laptiming application
* bare PCBs (w/o housing) are cheaper options which might worth considering

## RTL-SDR v3

These devices do not have internal amplifiers for the required frequency range, you have to also buy external amplifiers/attenuators.