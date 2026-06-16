# Receiver architecture

The challenges are:
* Short burst of data (~100 symbols).
* AMB/MyLaps transponders use crystal resonator (!=oscillator), which can be 20-30 kHz off after some aging.
* Transmitters use a tuned LC-tank for transmit, Q>4 causes inter-symbol interference.
* Multiple manufacturers, different hardware characteristics. Some have strange timing or jitter.
* Large loops have their own distorsion characteristics.

```
                  +-----------+
                  | Preamble  |    init (phase / freq. offset / taps)
            +---->| Detector  |------+
            |     +-----+-----+      |
            |           |            |
            |           v            v
            |     +--------+   +-----------+   +-----------+   +-----------+
  radio ----+---->| Costas |-->| Fractnl.  |-->| Slicer    |-->| Decoding  |
                  | Loop   |   | Spaced Eq.|   | (BPSK)    |   +-----------+
                  +--------+   +-----------+   +-----------+
                      ^             |  ^             |
                      |             |  |             |
                      |             |  +-------------+
                      |             |   slicer decisions (decision-directed)
                      |             |
                      +-------------+
                        FSE output (carrier recovery)
```

The preamble detector initializes the Costas loop and the FSE using a known preamble (learns the frequency offset, initial phase of symbols and the filter taps).

The FSE filter length is 3x`samples_per_symbol`. While learning its taps, the filter figures out the "optimal sampling point" intrinsically. As such, no symbol synchronizer / clock recovery is used.

After training on the preamble, decision-directed ("blind") feedback loops take over. This functions as further clock recovery as well.
