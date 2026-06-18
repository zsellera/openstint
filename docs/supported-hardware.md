# Supported SDR Hardware

OpenStint works best with [HackRF One](https://greatscottgadgets.com/hackrf/one/) and [RTL-SDR v4](https://www.rtl-sdr.com/buy-rtl-sdr-dvb-t-dongles/). RTL-SDR v3 is also supported, but not recommended.

I recommend **RTL-SDR v4** due to its price/performance ratio.

To learn more how SDR works, watch [Andreas Spiess](https://www.youtube.com/watch?v=xQVm-YTKR9s) explaining it.

## HackRF One vs RTL-SDR?

The HackRF One has lower noise, better thermal performance, similar dynamic range and a 3x-10x pricetag. Pick the RTL-SDR v4, unless you're a radio nerd with some money to spend.

If you build a track with a permament loop and decoder, consider the HackRF One due to it's better thermal management (more likely survives an installation in an enclosed electrical box).

## HackRF One

HackRF One is an open-source radio. The orignal is manufactured by [Great Scott Gadgets](https://greatscottgadgets.com/hackrf/one/), and costs $330. Clones around $130 exists. I have successfuly tested one from Opensourcesdr Labs and Circet. Tips:
* prefer the "clifford version", as it has extra input protection
* do not purchase a portapack, as it is useless for laptiming application
* bare PCBs (w/o housing) are cheaper options which might worth considering

## RTL-SDR v4

Prefer the "original" [RTL-SDR Blog v4](https://www.rtl-sdr.com/buy-rtl-sdr-dvb-t-dongles/). Thermal management is a known painpoint of these devices, and not every manufacturer is up to this challenge.

Right now, the V4 is [out of stock](https://www.rtl-sdr.com/rtl-sdr-blog-v4-end-of-line/). If you can, wait for V4L, it's expected 2026 summer. If you buy an off-brand, and it's overheating (it's not a neccessity, many manufacturer can indeed use the thermal paste and aluminium blocks), just attach a fan. Being hot is the normal operation, you have to just take some of that heat away occasionally.

## RTL-SDR v3

These devices do not have internal amplifiers for the required frequency range, you have to also buy external amplifiers/attenuators.

In direct sampling mode,
* it's ca. 3 dB more sensitive than the v4. If you use V4 it with "-g 20" settings, you need a 3 dB attenuator to get the same results.
* to use with an NE592-based "loop antenna amplifier", you need 20..30 dB attenuator with gain turned to minimum to get the same results.
* the [openstint-preamp](https://github.com/zsellera/openstint-preamp) is not usable, it detects the transponder from like 2 m away the loop; as it's bias-tee powered, you can not add attenuators
