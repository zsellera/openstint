#!/usr/bin/env python3
"""Frequency-shift (tune) an interleaved 8-bit IQ capture.

Reads an IQ file produced by hackrf_transfer (complex signed int8) or
rtl_sdr (complex unsigned int8), multiplies the samples by a complex
exponential to translate the spectrum by a given frequency offset, and
writes the result back in the same 8-bit format.

All input captures are assumed to be centered at fc = 5 MHz. Applying an
offset of +O retunes the capture so that the content originally at
fc + O ends up at the new center (DC), i.e. it tunes the receiver up by O.

Usage:
    tune_capture.py --unsigned --rate=2.5M --offset=50k capture.iq capture490.iq
    tune_capture.py --signed   --rate=20M  --offset=-1.2M in.iq out.iq

Options:
    --signed      input/output samples are signed int8   (hackrf_transfer)
    --unsigned    input/output samples are unsigned int8  (rtl_sdr)
    --rate=R      sample rate in samples/second (suffixes k, M supported)
    --offset=O    frequency offset to apply (suffixes k, M; may be negative)
"""

import argparse
import sys

import numpy as np

# Process in chunks so arbitrarily large captures stream through a small
# amount of memory. One I/Q sample = 2 bytes.
CHUNK_SAMPLES = 1 << 20  # ~2 MB of raw bytes per chunk


def parse_si(value):
    """Parse a number with an optional k/M/G SI suffix into a float."""
    value = value.strip()
    suffixes = {"k": 1e3, "K": 1e3, "M": 1e6, "G": 1e9}
    if value and value[-1] in suffixes:
        return float(value[:-1]) * suffixes[value[-1]]
    return float(value)


def main():
    parser = argparse.ArgumentParser(
        description="Frequency-shift an 8-bit interleaved IQ capture.")
    fmt = parser.add_mutually_exclusive_group(required=True)
    fmt.add_argument("--signed", action="store_true",
                     help="signed int8 samples (hackrf_transfer)")
    fmt.add_argument("--unsigned", action="store_true",
                     help="unsigned int8 samples (rtl_sdr)")
    parser.add_argument("--rate", required=True, type=parse_si,
                        help="sample rate in S/s (k/M suffixes ok)")
    parser.add_argument("--offset", required=True, type=parse_si,
                        help="frequency offset to apply (k/M suffixes, may be negative)")
    parser.add_argument("infile", help="input IQ file")
    parser.add_argument("outfile", help="output IQ file")
    args = parser.parse_args()

    in_dtype = np.int8 if args.signed else np.uint8
    # rtl_sdr unsigned samples are centered at 127.5; hackrf signed at 0.
    dc_offset = 127.5 if args.unsigned else 0.0

    # Angular increment per sample for the tuning exponential. Negative sign so
    # that a positive --offset brings content at fc+offset down to DC.
    w = -2.0 * np.pi * args.offset / args.rate

    n0 = 0  # running sample index, keeps phase continuous across chunks
    with open(args.infile, "rb") as fin, open(args.outfile, "wb") as fout:
        while True:
            raw = np.fromfile(fin, dtype=in_dtype, count=2 * CHUNK_SAMPLES)
            if raw.size == 0:
                break
            # Drop a trailing odd byte (incomplete I/Q pair) if present.
            if raw.size % 2:
                raw = raw[:-1]

            iq = raw.astype(np.float32).reshape(-1, 2)
            n = iq.shape[0]
            sig = (iq[:, 0] - dc_offset) + 1j * (iq[:, 1] - dc_offset)

            idx = np.arange(n0, n0 + n)
            sig *= np.exp(1j * w * idx)
            n0 += n

            out = np.empty((n, 2), dtype=np.float32)
            out[:, 0] = sig.real + dc_offset
            out[:, 1] = sig.imag + dc_offset

            if args.unsigned:
                out = np.rint(out).clip(0, 255).astype(np.uint8)
            else:
                out = np.rint(out).clip(-128, 127).astype(np.int8)
            out.tofile(fout)

    print(f"tuned {args.infile} -> {args.outfile} "
          f"(offset {args.offset/1e3:+.1f} kHz @ {args.rate/1e6:.3f} MS/s)",
          file=sys.stderr)


if __name__ == "__main__":
    main()
