I'm proud to announce OpenStint, an open-source, RC3-compatible laptiming decoder project. Instead of custom hardware builds, it relies on inexpensive, ready-made software-defined radios. The primairly audience are small clubs and friendly gatherings, who either like thinkering or have no budget for a ready-made solution. It is also useful for tracks where RC3 compatibility is required, but compatible decoders are not available any more.

PROJECT URL: github.com /zsellera/openstint

The project is accompanied by a reference design of a transponder, and a low-noise balun & preamplifier side-project, custom-designed for RC timing loops.

HIGHLIGHTS
Off the shelf components only, no soldering is required. The heart of the decoder is a HackRF One, a software defined radio, available from $90 at opensourcesdrlab.com.
Supports RC3 protocol, with error-check and a makeshift error correction (little-to-no "ghost"/"shadow" transponder ids).
Defines (and decodes) an open transponder protocol. Known, easy-to-understand algorithms allowing 3 dB signal-to-noise ratio improvements (see docs/).
Runs on a Raspberry Pi Model 3 B+ (sub-$40 single board computer). A sub-$200 permanent installation is possible.
To watch it in action, here is a quick video: youtube.com /watch?v=YDW0eA1Szk4

To start using it, the necessary steps are detailed in the project's README. In short, you'll need:
HackRF One; RTL-SDR support is planned, but not available now.
A 1:9 or 1:8 HF balun (search for "noelec 1:9 balun" it's a $3 stuff) or a magnetic field probe.
Some 50-ohm coaxial cable (RG58 and RG316 are the inexpensive options)
An optional (but highly recommended) termination resistor: 330 ohm for surface installations and 470 ohm for above-the-track installations (see TDR measurements)
Note however, the project is in-the-making. See project roadmap in the aforementioned README file.

PICKUP ANTENNA
The detector antenna uses a similar design as other commercial solutions: it is a parallel wire transmission line, with a balun on one end, and a 330/470 ohm termination on the other end. The wires should be ideally 2 mm^2, and separated by 25 cm from each other. Use the 330 ohm termination for in-the-ground installations, and the 470 ohm termination for overhead wires. The inexpensive NoElec 1:9 HF balun gives a very good impedance match for the overhead installations, and is also usable for in-the-ground setups.

Note: I use the term "antenna" and not "loop". From a geometrical standpoint, it is a loop for sure. From electrical perspective, this is a parallel-wire transmission line, an unshielded arrangement to pick up external disturbances. There is no need for "resonance matching" or other similar magic.

TRANSPONDERS
While the project can decode RC3-compatible transponders, the preferred protocol is the OpenStint transponder protocol. The protocol is well documented, it makes sense (no deliberate obfuscation) and has pretty good SNR characteristics. A reference implementation is available (schematics, pcb and firmware). At club level, one can manufacture 40 pcs of these transponders for $130 (a cost less than a single RC4 hybrid).

The reference design is available at:
github.com /zsellera/openstint-transponder/

The reference design produce comparable signal levels to the commercial offerings when running from 7.2 V (servo port of the receiver). It requires 4.5 V minimum to function properly, and testing was done up to 8.5 V (2s). The output level depends on the input voltage.
Note: this is a "v2" design shared, I just submitted an order at JLCPCB for it. I'll update this thread when I could properly test them.

PREAMPLIFIER
The project comes with an optional, low noise preamp + filter, that does the differential to single-ended conversion as well. It fits into a Hammond 1590L die-cast aluminium case. It is useful at overhead antenna installations and in noisy environments. The design offers a good +13 dB power gain. The onboard filters get rid of out-of-band interferring radio signals, which may get downconverted to baseband due to non-idealities in the inexpensive radio receivers.

Project url:
github.com /zsellera/openstint-preamp

WHAT'S NEXT?
On the short run, I'd like to focus on:
lapcounter software: application for practice sessions, maybe clubraces later on.
STL-SDR v4 support: the cost of a 3rd-party HackRF One is ca. ~$90, while the cost of an original RTL-SDR is ~$50. On the other hand, RTL-SDR implementation is likely consists of a fractional resampler, which is resource-intensive (might need Raspberry Pi 4 or even 5).
sector timing: while this is primarily a lapcounter software feature, the clock-syncing required for it is a "first-class citizen" of the OpenStint project. See the "timesync messages" in the transponder protocol documentation.
I've spent about 2 months of a sabbatical on this project. While I think it was the best possible way to spend this time, I'd like to return to the world of 9-to-5 soon. As such, there are no promises on future features or project timelines.

HOW CAN YOU HELP OR CONTRIBUTE?
First off, this project converts a hardware problem into a software one, which is usually more likely to get solved.
This project severely needs integration with laptiming software. If you develop one, and you're interested in an integration, check out the decoder protocol documentation.
The project contain some basic integrations in the integrations folder, including a very basic laptiming software (for educational purposes). I'd like to work on a P3 bridge soon. If you have any documentation on the P3 protocol, please share it with me.
While reading the specs of the reference transponder, one might notice the gap between this and the RC4 transponder. Someone shared an X-ray image of an RC4-hybrid on this forum, and a "V7" text is visible on the copper layer. The design I shared is "V2" as of now. On the other hand, I unfortunately out of ideas on how to push it any further.
If you have developed your own transponder, let's talk. A reference implementation of the transponder protocol is available in github, and I can help making sense of it.
Test it out, preferably at your local track. I'll do the same. Initial tests are promising btw.
Also, if you like this project, please star them on Github.







Yesterday we could test the system at our club. It ran the whole day, and counted 3170 laps in total. Out of these, 801 were made with OpenStint transponders, and the rest with RC3/RC4Hybrid/clone ones. I received no complaints about missed laps from people participating in the test.

As it was an indoor event and the pickup antenna was placed under the carpet. I used a lightly coated 26 AWG (0.14 mm2) wire. The wire separation was 30 cm, and a 330 Ohm termination resistor was soldered onto the remote end. At the business end, I added an openstint-preamp (see github for shematics), which was connected to the HackRF One. The HackRF was connected to a raspberry pi, which streamed the laptimes to a makeshift dashboard people could access with their phone.

I set the amplifier gains of the HackRF with the help of SDRAngel. It's a great tool to do all sort of RF magic with SDRs. To figure out the gains, we only need its' spectroscope though. Open the HackRF device with it, tune to 5 MHZ, set sample rate to 5 MSPS and IF filter bandwidth to 1.75 MHz. Park a car with a well-placed, strong transponder over the loop. Then, start from low values, and increase the LNA gains until you see sidelobes on the spectroscope. It indicates clipping. If seen, back off by 8 dB. Then do the same with the VGA gains (2 dB steps). I ended up starting the decoder with the following parameters:

Quote:
./openstint -l 24 -v 22 -b
As such, gains are LNA=+24dB, VGA=+22dB, and enabled bias-tee for the optional openstint-preamp. With these settings, the highest signals used the full range of the 8-bit ADCs inside the radio, while weaker transponder placements utilized the lower ~4 bits only. The noise affected the ~1.5 least significant digits, yielding still a good SNR for less-than-ideal transponder placements as well.

The antenna was placed to a slow section of the track. During passings, a typical RC4-hybid registered 55-60 hits, while an OpenStint transponder did 170-180. This is in-line with the expectations, as the openstint transponders produce ~3x more decodable messages as an RC4-hybrid does.

Unfortunately, the decoder also reported (probably) RC3 status messages as passings. I added a patch, so next time (hopefully) this will be not a problem. Unfortunately I'm just guessing though. The real solution would be a monitor mode, similar to what RCHourGlass has, to aid solving these mysteries.