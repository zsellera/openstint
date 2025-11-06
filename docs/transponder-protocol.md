# OpenStint transponder protocol

There are no "personal transponders"; every transponder should have a **random** transponder id. The transponder id must be between 0 and 9999999 (under 7 decimal digits). As such, collisions are possible, but rare. On the other hand, there is no need for any centralized institution or committee to issue and maintain such a transponder database.

To assess the probability of having the same transponder ids in a group, check out the [Birthday paradox](https://en.wikipedia.org/wiki/Birthday_problem). The collision probability
* in a group of 12 is less than 10<sup>-5</sup> (you have to deal with this problem on every 10000th race).
* in a group of 120 is less than 10<sup>3</sup> (out of 1000 race events, you might see two people with the same transponder id, not neccessary in the same race category).
* in a group of ~3700 is about 1/2.

The proposed way is to generate from the MCU's serial number, if possible:
```c
uint32_t *serial = UID_BASE;
uint32_t transponder_id = crc32(serial, 12) % 10000000U;
```

## Radio

The frames are encoded and transmitted with BPSK modulation with a 1.25 MHz symbol rate aligned to an 5 MHz carrier. The transponder id should be transmitted *on average* every 1.5 ms. The transmission period should have ±750 us random jitter added. There must be at least 750 us silence between subsequent frames.

The frames are typically 104 bits of length. As such, the transmission duration is 83.2 us (104/1.25meg). This yields a channel usage of ~5.5%, allowing other transponders to co-exist.

Assuming a 30 cm wide detection window, a car at v=10 m/s spends 30 ms in the detection window. On average 20 transponder transmissions are happening during this timeframe, which is more than enough for a passing detection.

The frames must align to the carrier wave (symbols should be 0/180 degrees to the carrier wave).

Note: OpenStint protocol does not employ differential-encoding before transmission. While it makes bit syncronization harder, differential-encoding can cause double bit errors on the receiver side, making successful error correction less likely.

## Framing

The frame's structure, in baseband, is defined as:
```
<-- init -->| <------------------ preamble -------------------> | <------------- message -------------> | <-- tail -->
            | 13-bit Barker code                     |   pad    | 80-bit message                        | any seq
-1 -1 -1 -1 | +1 +1 +1 +1 +1 -1 -1 +1 +1 -1 +1 -1 +1 | -1 -1 -1 | <convolutional-coder encoded message> | +1 -1 +1 -1
```

* The *init sequence* can trigger energy-based frame detectors (start processing the baseband). It must consists of at least 4, at most 8 `-1 + 0i` symbols.
* The *preamble* is of 16 bits, starting with a 13-bit Barker code followed `-1 -1 -1` sequence (to pad it to 16 bits). This allows correlation-based detectors to indicate "start of frame" while also aid initializing the symbol syncronizer.
* The *message* is encoded by a convolutional encoder of K=9, rate=1/2, polynoms of `0x1af` and `0x11d`. This is libfec's `viterbi29` implementation btw.
* Digital filters have delay. An optional *tail sequence* can help getting all symbols out from various filter pipelines while still processing similar-magnitude data stream. Maximum of 8 symbols.

The preamble is not part of the convolutional encoded data stream.

## Transponder messages

The transponder message is as follows:
```
<transponder_id[23:16]> <transponder_id[15:8]> <transponder_id[7:0]> <crc8> 0x00
```

Details:
 * CRC8 is the [liquid-dsp](https://liquidsdr.org/) implementation. Transponder's bytes are processed in big-endian byte order.
 * The tailing `0x00` 8 bits are required by the viterbi trellis to decode the message.

After the K=9 convolutional encoder, this structure has the following error characteristic:

| SNR | valid frame rate | crc false positives |
|-----|------------------|---------------------|
| 12 dB | 100.0 %        | 0.00 %              |
| 9 dB  | 100.0 %        | 0.00 %              |
| 6 dB  | 99.96 %        | <0.001 %            |
| 3 dB  | 87.8 %         | <0.02 %             |
| 0 dB  | ~13 %          | <0.3 %              |

Note: SNR is measured on the baseband signal (1 sample per symbol). Oversampling might further improve the characteristics.

## Time syncing messages

To measure sector times, the decoder clocks have to be in-sync (the timing software has to have a very good estimate of the clock differences). The transponders can aid this timesync.

Transponders without working crystal oscillators must prevent transmitting timesync messages!
Transponders with XO should transmit timesync messages at a rate of 1 Hz (average).

As transponders must consists of at most 8 decimal digits, the maximum transponder id is 9999999, or `0b100110001001011001111111`. As such, we can use some of the remaining space for time syncing.

The time sync message is as follows:
```
1010 <timecode[19:0]> <crc8> 0x00
```

The `timecode` is 20 LSB of a continuosly running 10 kHz timer, meaning it's incrementing once in every 100 us.

The decoders can associate the timesync messages with transponders, and report to the timing software. The timing software has now a chance to calulate time differences between decoders.

### Note on time syncing

See the example here:

| decoder | decoder's timestamp (ms) | transponder id | transponder time | timing software's steady clock (ms) |
|---------|--------------------------|--------------- | ---------------- | ----------------------------------- |
| A       | 888881000.0              | 1234567        | 500000           | 1117770000.0                        |
| B       | 999991000.0              | 1234567        | 10000            | 1118770000.0                        |

There is `1118770000.0 ms - 1117770000.0 ms = 1000 s` difference between reception time of the decoder messages. This difference is affected by network and other latencies though.

On the other hand, we know that:
```
transpoder_time_delta ~= timing_software_time_delta
```

Substition:
```
1000 ms - 50000.0 ms + N*(2^21)/10 ms ~= 1000 s
```
where N is a positive integer (times the transponder timer has overflown). The closest solution is `N=5`, where the residual error is `-424 ms`.

As such, the transponder saw a time difference between the two detection of
```
1000.0 ms - 50000.0 ms + 5*(2^21)/10 ms = 999576.0 ms
```

From this, we can calulate the clock difference between the two independent decoders:
```
999991000.0 ms - 999576.0 ms - 888881000.0 ms = 110110424 ms
```

It means if we have an event on decoder_B at `Tb`, the same event happend at `Ta = Tb - 110110424` in decoder_A's timeframe.

## Notes on forward error correction

By sending the transponder id only (no FEC, no checksums), the TX duration can be significantly decreased, but each receive error results in a fake passing. It's obviously not acceptable.

Sending transponder + crc8 (32 bits in total), yields the following table:

| SNR | valid frame rate | crc false positives | crc false negatives |
|-----|------------------|---------------------| ------------------- |
| 12 dB | 100.0 %        | 0.00 %              | 0.00 %              |
| 9 dB  | 99.51 %        | 0.00 %              | 0.14 %              |
| 6 dB  | 79.5 %         | <0.01 %             | 4.8 %               |
| 3 dB  | 19.8 %         | <0.1 %              | 10 %                |
| 0 dB  | <1 %           | <0.39 %             | <2.1 %              |

With `viterbi27` decoder (K=7, r=1/2), we see the following results:

| SNR | valid frame rate | crc false positives |
|-----|------------------|---------------------|
| 12 dB | 100.0 %        | 0.00 %              |
| 9 dB  | 100.0 %        | 0.00 %              |
| 6 dB  | 99.91 %        | <0.001 %            |
| 3 dB  | 83.6 %         | <0.04 %             |
| 0 dB  | ~11.2 %        | <0.32 %             |

**Conclusion:** 
* The +3dB improvement the convolutional coding provides is huge. It means we need 40% more signal for the same reception quality. Given all the bad transponder placements I've seen during race weekends, this is an improvement worth the cost.
* The difference between K=7 and K=9 is marginal though. The compute cost of K=9 is 4x of K=7. **Final decision needs further testing.**
* Sending a shorter message more frequent (20+32 bits vs 20+80 bits) won't give the performace boost as forward error correction does.