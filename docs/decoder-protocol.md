# OpenStint decoder protocol

This page describes how to get information out of the OpenStint decoder software.

The decoder protocol costsis of one-way *text-based* messages over a *ZeroMQ pub-sub* channel.
* [ZeroMQ](https://zguide.zeromq.org/) makes sure messages are consumed as a single entity, in a fault-tolearant manner. There is no need for custom frame detection (ie. P3's `0x8D` or Cano's `\n`), ZeroMQ does this for us ("consumed as a single entity"). There is also little to no need to worry about lost TCP connections, the ZeroMQ client reconnects when possible ("fault tolerant"). Note, the pub-sub structure does not buffer messages though, meaning unseen messages are lost.
* Messages are human-readable. Check out [example subscriber](../integrations/subscriber.py) for a quick demo.

## Protocol messages

The protocol defines 3 types of messages. Each message type is identified by the first character of the message. The message attributes (ie. `transponder_id`, `timestamp`, etc.) are space-separared. Attribute types are defined by their position in the stream. This makes processing as easy as:

```python
parts = msg.split()
if len(parts) < 5:
    continue  # malformed message

msg_type = parts[0] # "P" for passing, "T" for timesync, ...
if msg_type != "P":
    continue  # not interrested

try:
    timecode = int(parts[1])
    transponder_type = parts[2]
    transponder_id = int(parts[3])
    rssi = float(parts[4])
    hit_count = int(parts[5])
    pass_duration = int(part[6])
except ValueError:
    continue  # skip malformed data
```

For a working laptimer example, check out [laptimer.py](../integrations/laptimer.py).

Other message parameters are reserved for future use. As such, do not do something like this, as it might fail with future versions:

```python
msg_type, timecode, transponder_type, transponder_id, rssi, hit_count, pass_duration = msg.split()
```


### Passings ("P")

**BREAKING CHANGE:** Error Vector Magnitude is no longer reported with passings, but are visible per-frame in monitor mode (`-m` command line flag). The last parameter now is `pass_duration`.

Structure:
```
P <decoder_timestamp:uint64> <transponder_type:string> <transponder_id:uint32_t> <rssi:float> <hit_count:uint32_t> <pass_duration:uint32> [other future parameters]
```

Example:
```
P 1618706341 OPN 1615544 3.50 64 89
P 1618714251 OPN 1615544 3.08 40 94
P 1658197240 AMB 3616557 3.88 21 92
P 1658197696 AMB 3616557 4.24 30 89
```

* `decoder_timestamp` is a milliseconds-resolution [steady clock](https://en.cppreference.com/w/cpp/chrono/steady_clock.html) epoch, counting from the startup of the decoder process. As such, it is insensitive to updates to system time (NTP syncs). Treat it as a monotonic counter. When the decoder process restarts, the counter restarts as well.
* `transponder_type` defines if the passing is from an OpenStint (`OPN`) or legacy/RC3 transponder (`AMB`). The [OpenStint transponder protocol](transponder-protocol.md) is an error-corrected, highly sensitive, well-documented transponder protocol, and it is the preferred protocol of this project. The legacy/RC3 is there to provide backwards-compatibility with existing transponders (MyLaps/MRT/Vostok/etc.). RC4 support is not actively pursued.
* `transponder_id` is a non-negative number. Both OpenStint and AMB/RC3 defines it as "up to 7 digits", but future transponder options might increase it's width. With OpenStint transponders, even single-digit (ie. `0`) transponder ids are possible.
* `RSSI` is the maximum "**R**elative **S**ignal **S**trenght **I**ndicator. It is expressed in terms of power, in decibel scale. The reference point (0 dB) is the maximum power the radio can receive, and every measured value *should be* negative (high-power, clipped signals can present as positive values though). It is calculated from (an approximation of) RMS value. As such, the value `-3.0` means the full scale is used, larger values indicate clipping (decrease amplifier gains). Reliable reception is possible at 3 dB above noise floor (repored in status messages).
* `hit_count` tells about the number of successfully decoded tranponder messages during the passing. OpenStint transponders should transmit a message on average every 1.5 ms. RC4-hybrid transponders send at a similar rate, but only every ~4th is an RC3 message (which is the supported message format).
* `pass_duration` is an estimate of the transponder being spent inside the loop, in miliseconds. It is usable for speed detection: 90 ms inside a 30 cm wide loop means 0.3/0.09=3.33 m/s or 12 km/h. Pass duration estimate is only available when the transponder's coil is parallel to the pickup loop. If detection is not possible, `0` value is reported.


### Time Syncronization ("T")

Structure:
```
<decoder_timestamp:uint64> <transponder_type:string> <transponder_id:uint32_t> <transponder_timecode:uint32_t> 
```

Example:
```
T 1618721816 OPN 1615544 905370
```

Time syncronization messages are sent by OpenStint transponders with precise internal clock. These messages are meant to syncronize distinct decoder installations for **sector timing**.

You can read further info in the [transponder protocol](transponder-protocol.md) document.

The gist is though, if the laptimer software receive one-one timesync messages from two decoders, it can synchronize decoder_timestamp with a high precision.

Possible future extensions:

* Frequency offset between the transponders carrier and the radio's master clock


### Status Messages ("S")

Structure:
```
S <decoder_timestamp:uint64> <noise_power:float> <dc_offset_magnitude:float> <frames_received> <frames_processed> [other future parameters]
```

Example:
```
S 1792039754 -41.018744 5.08 0 0
S 1792040804 -41.2333267 5.08 77 52
S 1792041851 -40.9898376 5.22 184 135
S 1792042901 -41.0032545 5.08 0 0
```

* `decoder_timestamp` is the same monotoic clock as used in other messages.
* `noise_power` is the average received signal level when no transponder messages are received. It is expressed in dBFS (decibel full-scale), just like the RSSI values. As it is log-scale, you can get the signal-to-noise ratio as `SNR = frame_power-noise_power`.
* The `dc_offset_magnitude` is the absolute value of the DC-offset. It is radio-dependent error, and usually caused by phase-imbalance in the mixer stages. Post-mixer amplifiers (hackrf: VGA) amplifiy it. If the magnitude is larger than ~10.0, consider decreasing the VGA gain of the radio.
* `frames_received` and `frames_processed` count the total and successfully processed transponder transmissions in the given reporting period. A large difference indicates a bad signal-to-noise environment or high inter-symbol interfecence (caused by bad LC-tuning). If you're experimenting with your own transponders, this is a good metric to track while tuning the capacitors of the "antenna loop".

Possible future extensions:
* Low-bin (ie. 64) FFT on the received signal. It would help setting up preamps and amplifiers gains.

## Timebase, sector timing and timing accuracy (-t flag)

**MAIN TAKEAWAY:** use the default setting (monotonic cpu clock), and enable the `-t` flag (use system clock) only after the risks & benefits have been understood and assessed.

Every physical clock only approximates what mankind defined as "one second". As such, clocks inevitably drift from each other. The drift is measued in *ppm* (parts per million). A drift of *10 ppm* (typical to inexpensive crystal clocks) means 1 ms difference over 100 s when measured with by two different clocks. Unless you're after Formula-1 level of accuracies, this is most likely fine: the laptime is a difference of two timestamp close to each other, and inaccuracies do not add up over time.

However, the crystal clock in your host computer has to maintain a *system time*, where such small errors do add up. To compensate for these errors, the local time is synchronized regularly to a remote time standard (backed by a more accurate atomic clock), using NTP prococol. This can introduce:

* jumps in the system time (100+ ms)
* system time might travel backwards
* system time might artificially speed up or slow down (slewing) to catch up

All the factors above pose a problem in non-F1 environments as well. To avoid these, the openstint decoder uses the host CPUs tick counter (insensitive to wall clock problems).

When it comes to sector timing, we no longer measure an interval on the same clock though, but the difference of two, possibly free-running clocks. In this setup, drift errors do add up. As such, you might have to enable the `-t` flag, so the laptiming software sees a *syncronized time* across the various decoders (and leave the problem of time synchronization to the operating system).

To address the issues introduced by time syncronization, the errors must be decreased to an acceptable level. There are several ways to achieve this, but each of them is bigger than the scope of this document. Two notable solutions are though:

* a single host computer processess all sectors, and either the default monotonic clock is used or the NTP/timesync is turned off.
* GPS-referenced local timesource, cabled network, frequent NTP sync, slewing enabled
