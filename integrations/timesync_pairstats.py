#!/usr/bin/env python3
"""
Live timesync error statistics for OpenStint transponders (pairwise method).

Connects to a decoder's ZeroMQ pub channel, reads
(decoder_timestamp, transponder_timecode) tuples from one transponder, and as
each tuple arrives pairs it with the past tuple(s) ~p seconds back (for each
power-of-two gap p). The elapsed time is measured on both clocks and the
disagreement (error_ms) is accumulated per gap bucket. Every 5 seconds the
accumulated errors are summarized.

For a pair (a, b) with a earlier than b:

    decoder_delta_ms  = decoder_ts_b - decoder_ts_a              # ms
    rollover_count    = int(decoder_delta_ms * 10 / 2**20)       # timecode wraps
    transponder_delta = (timecode_b + rollover_count * 2**20)    # 100 us ticks
                        - timecode_a
    error_ms          = decoder_delta_ms - transponder_delta / 10

error_ms is "how much more time the decoder clock measured than the transponder
clock" over that interval. Pairs are bucketed by round(decoder_delta_ms / 1000)
when that rounds to a power of two (1, 2, 4, ... s), and each bucket reports
count / average / std dev / median / min / max / 90% spread of error_ms.

How to read it: the AVERAGE error in a bucket grows linearly with the gap and
reveals the clock's frequency offset (avg_ms / gap_s = -ppm/1000); the STD DEV
and the 90% spread show the timing jitter at that gap.

Usage:
    timesync_pairstats.py [host] [port] [transponder_id]
    (host defaults to 127.0.0.1, port defaults to 5556)

Notes:
  * The 20-bit, 100 us transponder timecode wraps every 2**20 ticks = 104.8576 s;
    the rollover count derived from the decoder clock unwraps it per pair.
  * Errors are computed incrementally as each message arrives: a new tuple is
    paired only with the past tuple(s) ~p seconds back, for each bucket p. That
    is O(buckets) per message instead of O(N^2) over the whole buffer, so the
    accumulated error history is unbounded and the 5 s loop only summarizes it.
"""

import os
import sys
import time
import math
import bisect
from collections import defaultdict

import numpy as np
import zmq


# --- transponder timecode characteristics ---------------------------------
TIMECODE_BITS = 20
TIMECODE_WRAP = 1 << TIMECODE_BITS   # 1048576 ticks; 100 us each -> 104.8576 s

# --- analysis parameters --------------------------------------------------
# Buckets: pairs whose gap rounds to one of these whole seconds.
BUCKETS = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096]
MAX_BUCKET = max(BUCKETS)       # only need lookback tuples up to this gap
REPORT_INTERVAL = 5.0          # seconds between printouts

# ANSI terminal control (only used when stdout is a TTY)
ALT_SCREEN_ON = "\033[?1049h"
ALT_SCREEN_OFF = "\033[?1049l"
CURSOR_HIDE = "\033[?25l"
CURSOR_SHOW = "\033[?25h"
CURSOR_HOME = "\033[H"
CLEAR_BELOW = "\033[J"


def pair_error_ms(dd_ms, raw_ticks):
    """error_ms for one pair, unwrapping the timecode via the decoder clock.

    Choose the rollover count that puts the transponder-measured elapsed closest
    to the decoder-measured one. (A plain floor(decoder_ticks / 2**20) is off by
    one wrap for any pair straddling a counter wrap, injecting 104.9 s errors.)
    """
    expected_ticks = dd_ms * 10.0
    rollover = round((expected_ticks - raw_ticks) / TIMECODE_WRAP)
    td_ticks = raw_ticks + rollover * TIMECODE_WRAP
    return dd_ms - td_ticks / 10.0


def update_buckets(new_ms, new_tc, recent_ms, recent_tc, bucket_errors):
    """Pair an arriving tuple with past tuples ~p seconds back, per bucket.

    recent_ms is kept sorted ascending (decoder time is monotonic). For each
    bucket p we bisect out the past tuples whose gap rounds to p and append one
    error_ms per such pair, reproducing exactly the pairs the O(N^2) sweep would
    have found, but at O(buckets) cost per message.
    """
    for p in BUCKETS:
        # gap rounds to p  <=>  past timestamp in [new - (p+.5)s, new - (p-.5)s)
        lo = new_ms - (p + 0.5) * 1000.0
        hi = new_ms - (p - 0.5) * 1000.0
        i0 = bisect.bisect_left(recent_ms, lo)
        i1 = bisect.bisect_left(recent_ms, hi)
        for i in range(i0, i1):
            dd_ms = new_ms - recent_ms[i]
            raw_ticks = new_tc - recent_tc[i]
            bucket_errors[p].append(pair_error_ms(dd_ms, raw_ticks))

    recent_ms.append(new_ms)
    recent_tc.append(new_tc)
    # drop lookback tuples older than the largest bucket gap
    cutoff = new_ms - (MAX_BUCKET + 1) * 1000.0
    drop = bisect.bisect_left(recent_ms, cutoff)
    if drop:
        del recent_ms[:drop]
        del recent_tc[:drop]


def bucket_stats(errors):
    """count / mean / std / median / min / max / 90% spread of an error list."""
    errors = np.asarray(errors, dtype=np.float64)
    n = len(errors)
    # single-sided 90% spread: half the central P5..P95 width, reported as +/-.
    spread90 = ((np.percentile(errors, 95) - np.percentile(errors, 5)) / 2.0
                if n >= 2 else 0.0)
    return {
        "count": n,
        "mean": float(np.mean(errors)),
        "std": float(np.std(errors, ddof=1)) if n >= 2 else 0.0,
        "median": float(np.median(errors)),
        "min": float(np.min(errors)),
        "max": float(np.max(errors)),
        "spread90": float(spread90),
    }


def format_report(locked_id, messages_count, bucket_errors, status=None):
    lines = []
    bar = "=" * 86
    lines.append(bar)
    lines.append(
        f"timesync pair-stats  transponder {locked_id}  |  "
        f"{messages_count} messages processed"
    )
    if status:
        lines.append(f"  {status}")
    if not bucket_errors:
        lines.append("  (not enough data yet)")
        lines.append(bar)
        return "\n".join(lines)

    header = (f"  {'gap[s]':>6} {'count':>7} {'mean':>9} {'stdev':>9} "
              f"{'median':>9} {'min':>9} {'max':>9} {'90%spread':>11}")
    lines.append(header)
    lines.append("  " + "-" * (len(header) - 2))
    for p in BUCKETS:
        if p not in bucket_errors:
            continue
        s = bucket_stats(bucket_errors[p])
        flag = "  (low n)" if s["count"] < 10 else ""
        spread = f"±{s['spread90']:.3f}"
        lines.append(
            f"  {p:>6} {s['count']:>7} {s['mean']:>9.3f} {s['std']:>9.3f} "
            f"{s['median']:>9.3f} {s['min']:>9.3f} {s['max']:>9.3f} "
            f"{spread:>11}{flag}"
        )
    lines.append("  (all error values in ms; error = decoder_elapsed - "
                 "transponder_elapsed)")
    lines.append(bar)
    return "\n".join(lines)


def save_adev_chart(locked_id, bucket_errors):
    """On exit, plot an ADEV-like log-log curve from the per-bucket stddev.

    Allan-deviation analogue per bucket: the timing-error stddev (a time, in s)
    divided by the gap (a time, in s) is the dimensionless fractional-frequency
    deviation -- adev ~ (stddev_ms / 1000) / gap_s. Falls as 1/tau while jitter-
    limited, bottoms at the stability floor, rises again under drift.
    """
    points = []
    for p in BUCKETS:
        errs = bucket_errors.get(p)
        if errs and len(errs) >= 2:
            std_ms = float(np.std(np.asarray(errs, dtype=np.float64), ddof=1))
            adev = (std_ms / 1000.0) / p
            points.append((p, adev, std_ms, len(errs)))
    if len(points) < 2:
        print("Not enough buckets to plot an ADEV chart.")
        return

    tau = [p for p, _, _, _ in points]
    adev = [a for _, a, _, _ in points]

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not available; ADEV-like data (tau[s], adev, "
              "stddev[ms], count):")
        for p, a, s, n in points:
            print(f"  {p:>6}  {a:.3e}  {s:9.3f}  {n}")
        return

    fig, ax = plt.subplots(figsize=(8, 6))
    ax.loglog(tau, adev, "o-", color="#1f77b4", lw=1.8, ms=7,
              label=f"stddev/tau (transponder {locked_id})")

    # tau^-1 reference (phase noise / jitter), anchored to the first point
    import numpy as _np
    tref = _np.array([tau[0], tau[min(len(tau) - 1, 5)]], dtype=float)
    ax.loglog(tref, adev[0] * (tref / tau[0]) ** -1.0, "--", color="gray",
              lw=1, label=r"$\tau^{-1}$ (jitter)")

    # mark the floor (minimum)
    imin = int(_np.argmin(adev))
    ax.scatter([tau[imin]], [adev[imin]], s=140, facecolors="none",
               edgecolors="green", lw=2, zorder=5,
               label=f"floor {adev[imin]:.2e} @ {tau[imin]} s")

    ax.set_xlabel("gap  tau  [s]")
    ax.set_ylabel("ADEV-like  =  stddev / tau  (dimensionless)")
    ax.set_title(f"timesync stability - transponder {locked_id} (log-log)")
    ax.grid(True, which="both", ls=":", alpha=0.5)
    ax.legend(fontsize=9)
    fig.tight_layout()

    fname = f"adev_{locked_id}_{int(time.time())}.png"
    path = os.path.abspath(fname)
    fig.savefig(path, dpi=130)
    plt.close(fig)
    print(f"ADEV chart saved to {path}")


def main():
    args = sys.argv[1:]
    if len(args) > 3:
        print(f"Usage: {sys.argv[0]} [host] [port] [transponder_id]")
        print("  host defaults to 127.0.0.1, port defaults to 5556")
        sys.exit(1)

    host = args[0] if len(args) >= 1 else "127.0.0.1"
    port = args[1] if len(args) >= 2 else "5556"
    locked_id = int(args[2]) if len(args) >= 3 else None
    address = f"tcp://{host}:{port}"

    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    socket.connect(address)
    socket.setsockopt_string(zmq.SUBSCRIBE, "T")  # only timesync messages

    poller = zmq.Poller()
    poller.register(socket, zmq.POLLIN)

    print(f"Connected to {address}. Collecting timesync messages...")
    if locked_id is not None:
        print(f"Filtering on transponder {locked_id}.")

    use_tty = sys.stdout.isatty()
    if use_tty:
        sys.stdout.write(ALT_SCREEN_ON + CURSOR_HIDE)
        sys.stdout.flush()

    recent_ms = []      # lookback decoder timestamps (ms), kept sorted
    recent_tc = []      # matching transponder timecodes (100 us ticks)
    bucket_errors = defaultdict(list)   # bucket seconds -> accumulated error_ms
    n_messages = 0
    last_decoder_ms = None
    status = None
    next_report = time.monotonic() + REPORT_INTERVAL

    try:
        while True:
            timeout_ms = max(0, (next_report - time.monotonic()) * 1000.0)
            events = dict(poller.poll(timeout_ms))

            if socket in events:
                try:
                    msg = socket.recv_string(zmq.NOBLOCK).strip()
                except zmq.Again:
                    msg = None
                if msg:
                    parts = msg.split()
                    # T <decoder_ts_ms> <type> <id> <timecode_100us>
                    if len(parts) >= 5 and parts[0] == "T" and parts[2] == "OPN":
                        try:
                            decoder_ms = int(parts[1])
                            tid = int(parts[3])
                            raw_ticks = int(parts[4]) % TIMECODE_WRAP
                        except ValueError:
                            tid = None
                        if tid is not None:
                            if locked_id is None:
                                locked_id = tid
                                status = f"locked onto transponder {locked_id}"
                                if not use_tty:
                                    print(f"Locked onto transponder {locked_id}.")
                            if tid == locked_id:
                                # decoder restart -> timestamps jump back; only
                                # the lookback buffer is invalid (cross-restart
                                # pairs), accumulated error stats stay valid.
                                if (last_decoder_ms is not None and
                                        decoder_ms < last_decoder_ms):
                                    status = ("decoder timestamp went backwards; "
                                              "lookback reset")
                                    if not use_tty:
                                        print("Decoder restarted; resetting "
                                              "lookback buffer.")
                                    recent_ms.clear()
                                    recent_tc.clear()
                                last_decoder_ms = decoder_ms
                                n_messages += 1
                                update_buckets(float(decoder_ms),
                                               float(raw_ticks),
                                               recent_ms, recent_tc,
                                               bucket_errors)

            if time.monotonic() >= next_report:
                next_report += REPORT_INTERVAL
                report = format_report(locked_id, n_messages, bucket_errors,
                                       status)
                if use_tty:
                    sys.stdout.write(CURSOR_HOME + CLEAR_BELOW + report + "\n")
                    sys.stdout.flush()
                else:
                    print(report)

    except KeyboardInterrupt:
        pass
    finally:
        if use_tty:
            sys.stdout.write(CURSOR_SHOW + ALT_SCREEN_OFF)
            sys.stdout.flush()
            report = format_report(locked_id, n_messages, bucket_errors, status)
            print(report)
        socket.close()
        context.term()
        print("Stopped.")
        if locked_id is not None:
            save_adev_chart(locked_id, bucket_errors)


if __name__ == "__main__":
    main()
