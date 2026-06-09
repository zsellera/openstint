# Replay captured IQ samples

Both HackRF and RTL-SDR have command line tools to capture IQ samples. OpenStint can replay these samples. This is useful for testing and debugging.

When doing the capture, take care:
* `openstint_rtlsdr` processes 2 samples per symbol.
* `openstint_hackrf` processes 4 samples per symbol by default; but it's settable to 2, 4 or 8.

## rtl_sdr

```
rtl_sdr -f 5000000 -s 2500000 -g 20 -n 12500000 capture.iq
openstint_rtlsdr -c capture.iq
```

* `-f 5000000`: center frequency 5 MHz
* `-s 2500000`: sample rate 2.5 MSPS
* `-g 20`: built-in amplifier gains
* `-n 12500000`: record 5 seconds worth of data (`5 s * 2.5 MSPS = 12500000 samples`)

## hackrf_transfer

```
hackrf_transfer -r capture.iq -f 5000000 -s 5000000 -l 20 -g 20 -n 25000000
openstint_hackrf -c capture.iq
```

* `-f 5000000`: center frequency 5 MHz
* `-s 5000000`: sample rate 5 MSPS
* `-l 20`, `-g 20`: built-in LNA and VGA gains
* `-n 25000000`: record 5 seconds worth of data (`5 s * 5 MSPS = 25000000 samples`)

Note: 
* for `SAMPLES_PER_SYMBOL=2`, use `-s 2500000 -n 12500000`
* for `SAMPLES_PER_SYMBOL=4`, use `-s 5000000 -n 25000000`
* for `SAMPLES_PER_SYMBOL=8`, use `-s 10000000 -n 50000000`
