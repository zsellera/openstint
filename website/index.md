---
# https://vitepress.dev/reference/default-theme-home-page
layout: home

hero:
  name: "OpenStint"
  text: "HackRF & RTL-SDR Powered RC Lap Timing Decoder"
  tagline: "OpenStint is an open-source project reading AMB-style near-field transponders using inexpensive software-defined radios (SDR)."
  image:
    src: /openstint-decoder.png
    alt: Simple setup with Raspbery Pi Model 3 B+
  actions:
    - theme: brand
      text: Quickstart
      link: /docs/setup-simple-rtlsdr
    - theme: alt
      text: Windows downloads
      link: https://github.com/zsellera/openstint/releases
    - theme: alt
      text: OpenStint Transponders
      link: https://github.com/zsellera/openstint-transpoder

features:
  - icon: 🎉
    title: Multi-protocol support
    details: Natively decodes the OpenStint transponder, plus RC3, RC4Hybrid, MRT and other RC3-clones.
  - icon: 🎓
    title: RC4 with learning
    details: Supports 3-wire RC4 transponders, including a learning feature to register them.
  - icon: 🔧
    title: Off-the-shelf hardware
    details: No soldering or electronics skills required — works with HackRF One and RTL-SDR v3 & v4.
  - icon: 🏁
    title: Works with your timing software
    details: Tested with LapBeeps, RCGTiming and ZRound.
  - icon: 📉
    title: Runs on modest hardware
    details: Low resource requirements — runs even on a Raspberry Pi 3 Model B+.
  - icon: ⏱️
    title: Precise passing detection
    details: Accurate passing-time detection based on signal strength.
  - icon: 🚗
    title: Passing speed detection
    details: Estimates the vehicle's passing speed from signal strength.
  - icon: 🧠
    title: Adaptive filtering
    details: Adaptive filters enhance reception quality.
---

