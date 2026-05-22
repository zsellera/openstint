# Simple setup with RTL-SDR

This is the most simple setup imaginable. You need your laptop, and the following hardware:

<div align="center">

| Part | Approx. Cost |
|------|--------------|
| [RTL-SDR Blog v4](https://www.rtl-sdr.com/buy-rtl-sdr-dvb-t-dongles/) | $40 |
| [1:9 HF balun](https://www.aliexpress.com/item/1005005936422760.html) | $2 |
| [10m/30ft RG316 coax](https://www.aliexpress.com/item/1005005547021161.html) SMA Male To SMA Male | $15 |
| [10m 20AWG silicone-insulated wire](https://www.aliexpress.com/item/32905273182.html) | $5 |
| [1m USB extension cable](https://www.aliexpress.com/item/1005004371131664.html) | $2 |
| [390 Ohm resistor](https://www.aliexpress.com/item/1005006730561561.html) | $1 |
| **TOTAL** | $65 |

Your prices might vary (shipping and taxes).

</div>

You'll also need transponders:
* [openstint-transpoder](https://github.com/zsellera/openstint-transponder) is a DIY open-source alternative. Go to JLCPCB for larger quantities (40pcs/$200, 80pcs/$300). For smaller quantities, ask around on [rctech forum](https://rctech.net/forum/radio-electronics/1137693-openstint-laptiming-decoder.html), or contact me via zsellera@gmail.com email address.
* AMB RC3 (EOL), MyLaps RC4 ($100) and RC4-Hybrid ($150), as well as MRT mPTX ($70) and other RC3-clones are supported.
* [RCHourGlass](https://github.com/mv4wd/RCHourglass/tree/master/Firmware/Transponder) transponders are also supported.
 

## System diagram

```
+-------------------+
|                   |   +--------+          +---------+         +---------+
+---------+  Loop    \--| 1:9 HF |          | RTL-SDR |         | Windows |
| 390 Ohm |  Antenna /--| balun  |--[coax]--|   v4.   |--[usb]--| Laptop  |
+---------+         |   +--------+          +---------+         +---------+
|                   |
+-------------------+
```

Some explanation:
* Lay down the loop as parallel as possible. For outdoor, use silicone-insulated fine-stranded copper wire. For indoors, use thin (26 awg) PTFE/FEP insulated wires (thicker wires bulge the carpet causing havoc among 1/12 pancars). Silicone, teflon an FEP insulations last a long time. Avoid enamel-coated transformator-wires.
* The termination resistor is important, anything between 330-470 ohms work. No soldering is strictly required: just weave the resistor's leads into the wire (under the insulation, between the strands), and wrap around strongly with ductape. No judgement :D
* The balun converts the parallel wire transmission line into a coaxial one. The linked balun can not accept wider wires than ⌀1 mm (20 AWG).
* RG316 is an inexpensive, outdoor-proof, 50-ohm coax cable. Order with SMA male on both ends. Note, the male connector has the center protruding pin (as opposed to female connector, which has a socket to accept the pin) -- do not get confused by the threading of the connector. 
* The RTL-SDR is a bulky beast, won't fit directly into your laptop. Get an extension cable.

## Software

Before running anything, [install RTL-SDR driver with Zadig](https://www.rtl-sdr.com/rtl-sdr-quick-start-guide/).

For debugging, you can test your setup with SDRAngel, as described at [track setup tutorial](setup-tutorial.md)

Using OpenStint is this easy:
1. Download the Windows distibution from the [releases](https://github.com/zsellera/openstint/releases/tag/nightly-master).
2. You can double-click on `openstint_rtlsdr.exe` to start with default parameters, or right-click -> edit `start.bat` to fine-tune parameters.

### Lap counting and scorign

I develop [LapBeeps](https://lapbeeps.com), it support OpenStint natively.

There is a bridge for [ZRound](https://www.zround.com/index.php/download-mananger/) and [RCGTiming](https://rcgtiming.com/), translating OpenStint's protocol to their native ones. These are packaged with the project in its .zip file.
