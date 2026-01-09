# Decoder Setup Tutorial

This writeup helps you getting started, even if you're not using the exact same setup as described here. The following architecture allows a permanent setup, where anyone with a windows-based laptop can connect to the decoder using [ZRound](https://www.zround.com/).

<img width="805" height="166" alt="image" src="https://github.com/user-attachments/assets/1e198886-e9ac-4f51-982c-c496ac5f4d33" />

## The pickup antenna

This is commonly refered to as "loop". Geometerically it's indeed a loop, but from electrical standpoint, it's a *parallel-wire transmission line*. As such, it has two ends: 
1. a remote end, which has to be terminated by a *termination resistor*, matching the line's characteristic impedance
2. a "business end", which should connect to an impedance transformer (balun)

The line's characteristic impedance is about 400 Ω, but depends on:
1. distance from the ground (closer means lower impedance)
2. wire separation (closer means lower impedance)
3. wire thickness (higher gauge means lower impedance)

For practical purposes,
* **indoor carper:** use thin wire (~ 0.1 mm<sup>2</sup> / 28 AWG) under the carpet and ~ 20..25 cm / 8..10 inch wire separation; terminate the wire with 330..400 Ω resistor.
* **outdoor overhead:** use 1.5..2 mm<sup>2</sup> (16..14 AWG) wire, 30 cm / 1 foot wire separation, and a 470 Ω termination resistor; consider using a preamplifier.
* **outdoor under the track:** use 1.5..2 mm<sup>2</sup> (16..14 AWG) wire, 30 cm / 1 foot wire separation, and a 330 Ω termination resistor.

PROTIP: To reduce common mode interference (from other legit sources like maritime radio), lay down the wires in parallel, with 90° bends.

If you do not terminate the line (leave open), or connect the wires together (short), the system will work, but the performance will be degraded. Also, do not tune the "loop" with capacitors and inductors, as it degrades the performance.

## Balun/preamp + filter

As there is an impledance mismatch between the antenna (ca. 400 Ω) and the coaxial input of the HackRF radio (50 Ω), some impedance transformation is a must.
* **passive** option is a ca. 1:8 impendance transformator. Search google/aliexpress/whatever for "1:9 HF balun" for a sub-$10 option.
* **active** solutions are more expensive, but provide some gain right after the antenna and the active circuit might offer ESD protection. Recommended solitions
  * [OpenStint Preamp](https://github.com/zsellera/openstint-preamp/), which has built-in impedance transformer, band-pass filter, ESD-protection and a low-noise single-stage +12 dB amplifier.
  * Some have success with MLA-30+ loop antenna preamplifiers.

If you do not install an impedance transformer (but solder the antenna wires directly to the coax inner conductor and shield), you'll face degraded performance, higher interference, higher noise, and you also risk the input of the radio.

PROTIP: for testing, purchase or [DIY](https://www.youtube.com/watch?v=2xy3Hm1_ZqI) a near-field magnetic probe. There are sub-$10 products on Aliexpress, any of them is much better than having the center conductor exposed to ESD for a longer period of time.

### Sidenote on filters

The way SDRs like the HackRF One operate, allow the downconversion of out-of-band signals to the baseband. In practical terms, the radio might see interfering radio at 15 MHz or FM transmission at 90 MHz, despite it is tuned to 5 MHz (the transponder frequency). To prevent this from happening, a band-pass filter is highly recommended, while an FM-rejection filter is the 2nd best thing to have.

## HackRF One

HackRF One is an open-source radio. The orignal is manufactured by [Great Scott Gadgets](https://greatscottgadgets.com/hackrf/one/), and costs $330. Clones around $130 exists. I have successfuly tested one from Opensourcesdr Labs and Circet. Tips:
* prefer the "clifford version", as it has extra input protection
* do not purchase a portapack, as it is useless for laptiming application
* bare PCBs (w/o housing) are cheaper options which might worth considering

## Coaxial cable

The 50 Ω cables are expensive. Good options are:
* indoor: RG58
* outdoor: RG316

Prefer pre-built cables, as soldering your own connectors requires skill, patience and practice. Prefer cables with cable relief. You need typically SMA *male* connectors on both ends. Note, on coaxial cables "male" refers to the inner conductor, and not the outer screw. Avoid RP-SMA and other blasphemies.

## Testing the system so far

On a laptop, install [SDRAngel](https://www.sdrangel.org/), an open-source radio software, and open the HackRF radio with it. Tune it to 5 MHz, 5 M Sample Rate (SR), apply the 1.75M low-pass filter. Depending on the balun/preamp, check the "Bias T" box.

<img width="359" height="273" alt="image" src="https://github.com/user-attachments/assets/b66f9ed1-715d-4acf-95f2-791a2df73de5" />

Make sure there is no active transponder around the loop. You might see in-band traffic on the waterfall. This is usually legit radio-traffic from neighbouring sources.

<img width="682" height="580" alt="image" src="https://github.com/user-attachments/assets/38fb2719-4fa1-4575-8980-7ee0d87d9739" />


Park a car with a well-placed, strong transponder over the loop. Set LNA gain to minimum, and VGA gain to 16. Then, start from low values, and increase the LNA gains until you see sidelobes on the spectroscope. It indicates clipping. If seen, back off by 8 dB. Then do the same with the VGA gains (2 dB steps).

<img width="867" height="336" alt="image" src="https://github.com/user-attachments/assets/8528073c-124f-4179-b1f4-c78871f55b7e" />

On our track, I ended up with a setup of LNA=+24dB, VGA=+22dB, and enabled bias-tee for the optional openstint-preamp. Note these values, as these will be used to initialize the openstint decoder with.

<img width="877" height="741" alt="image" src="https://github.com/user-attachments/assets/b57866e0-2c57-4bad-a0fb-3b606bee4828" />


With these settings, the highest signals used the full range of the 8-bit ADCs inside the radio, while weaker transponder placements utilized the lower ~4 bits only. The noise affected the ~1.5 least significant digits, yielding still a good SNR for less-than-ideal transponder placements as well.
