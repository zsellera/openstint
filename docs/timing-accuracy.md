# Timing Accuracy

The time is measured on the host computer when the radio's buffer is processed. Unfortunately, there is no way to measure it using the radio (where it is actually received). The system can drop buffers at various levels of the stack (radio/usb/driver), and individual buffers have no serial number to track lost ones. We are at the mercy of the operating system's scheduler, giving an accuracy in the **410us ~ 10ms range**, depending on your configuration.

Typical error values over a 32 second lap (aka choosing hardware):

| Platform           | HackRF One | RTL-SDR |
| ------------------ | ---------- | ------- |
| MacOS (M1)         | 0.8 ms     | 1.4 ms  |
| Windows 10 (Intel) | 10.9 ms    | 2.7 ms  |
| Linux (RPi3+)      | 3 ms       | 0.5 ms  |

(stdev in ms over a 32 second lap)

## Measuring timing accuracy

Funfact: you can compare two clocks, but there is no such as "absolute clock".

[Openstint-transponders](purchase-transponder.md) send a [timesync timecode](transponder-protocol.md#time-syncing-messages) every ca. 1000 ms. As the transponders are equipped with a good-enough crystal oscillator, so we can compare the timecode and the receive timestamp, building statistics over time.

Park a car off-track but still in the loop with an openstint-transponder inside. Use the tool `timesync_pairstats` to build statistics. So long the car is not moved away from the loop, its' passing is not registered. Other cars can still cross the loop and get registered.

![frequency deviation of RTL-SDR one on M1 Macbook Pro](adev.jpg)

## Sources of error

* NTP syncronization by slewing (Linux problem): handles small time gaps by slowing down or speedig up the local timer. This propagates down to the application. The error is in the 10-40 ppm range though.
* USB selective suspend / power management / Power plan throttling (Windows problem)
* App nap - MacOS problem; run with `caffeinate -dimsu openstint_rtlsdr ...`
* Operating system's scheduler: the buffers are timestamped when they are processed, and this time is up to the operating system (and the system load). This creates a "flat noise" accross every time intervals, no matter if you measure 1 second laps or 1000 second laps.
* Drift (clocks not running at the same rate): 10 ppm difference is 1 ms over a 100 s window.
* Wandering: no clock is stable, the most likely cause of "wandering" is temperature change. In short term, the clock seem stable (low stddev), but at the scale of the "wander" (hours), the stddev increase.

Note, some errors are systematic, and it affects every racer the same way (clock drifts)

## Parsing timesync_pairstats output

The following measurement was made with an RTL-SDR and a Windows 10 laptop. The SDR dongle was the only USB defices plugged in. The `openstint_rtlsdr` was started in normal priority.

```
====================================================================================================
timesync pair-stats  transponder 4394188  |  996 messages processed
  locked onto transponder 4394188
  gap[s]   count      mean    stdev   median      min      max      ±68%      ±95%    ±99.7%
  ------------------------------------------------------------------------------------------
       1     985    -0.026    2.715    0.000   -6.100    6.500    ±3.000    ±5.170    ±5.931
       2     984    -0.080    2.717    0.000   -6.100    6.200    ±3.000    ±5.221    ±6.026
       4     982    -0.156    2.657   -0.150   -6.300    5.900    ±2.750    ±5.200    ±5.926
       8     979    -0.308    2.812   -0.300   -6.600    5.900    ±3.126    ±5.350    ±6.053
      16     971    -0.638    2.645   -0.700   -6.700    5.700    ±2.800    ±5.088    ±6.032
      32     955    -1.262    2.712   -1.300   -7.500    4.900    ±2.918    ±5.165    ±6.100
      64     923    -2.536    2.791   -2.600   -8.700    3.700    ±3.050    ±5.200    ±6.062
     128     861    -5.053    2.613   -5.200  -11.400    1.400    ±2.650    ±5.250    ±6.021
     256     736   -10.114    2.698  -10.100  -16.100   -3.700    ±2.800    ±5.181    ±5.990
     512     483   -20.192    2.758  -20.200  -26.400  -14.300    ±3.088    ±5.297    ±5.978
  (error values in ms; error = decoder_elapsed - transponder_elapsed)
====================================================================================================
```

**Note:** "error" is the decoder's timestamp compared to the transponder's timecode! The text below assumes the clock inside the transponder is much more precise than of the host computer. At

The way to read it:
* On a typical, 16 second lap, the `stdev=2.645`. The jitter is a long-tail distribution, the 68-96-99.7% rule is not applicable.
* The calculation above is re-assured by actual measurements: no measured 16-second gap had larger error than (`max - min = 5.7 + 6.7 = 12.4 ms`); 95% of all such possible laps are within ±5.088 of each other.
* The *mean* and *median* doubles every octave: this is frequency drift. The clock of the host computer is ca. `20ms/512s = 39 ppm` slower than the transponder's clock. This is both normal and typical. Note: everyone's laptime is slewed.
* the `stddev` remains stable over the frequency ranges: the two clocks (transponder and host computer) are stable relative to each other. If one of them were changing differently relative to the other (ie the crystal heats up on the transponder), it would be visible on the timescale of the drifting.

## Windows tips-and-tricks

I'm seeing the least reliable measurements with a Windows 10 laptop (2013 Macbook Air, 1.7 GHz dual-core Intel CPU, the radio is the only USB device plugged in). The exact reasons are still being investigated.

* Power plan → High Performance (or Ultimate Performance).
* Disable USB selective suspend: Power Options → plan settings → Advanced → USB settings → "USB selective suspend setting" → Disabled. And Device Manager → your SDR + each USB Root Hub → Power Management → uncheck "Allow the computer to turn off this device to save power."
* Exclude the decoder process from Windows Defender real-time scanning.
* Start using `start /high openstint_rtlsdr.exe ...` in high-priority mode.
