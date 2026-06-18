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
count / average / std dev / median / min / max / +-68/95/99.7% spread of
error_ms.

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
    """count / mean / std / median / min / max / ±68/95/99.7% spread."""
    errors = np.asarray(errors, dtype=np.float64)
    n = len(errors)

    def half_spread(pct):
        # single-sided half-width of the central pct interval, reported as +/-.
        if n < 2:
            return 0.0
        lo = (100.0 - pct) / 2.0
        hi = 100.0 - lo
        return (np.percentile(errors, hi) - np.percentile(errors, lo)) / 2.0

    return {
        "count": n,
        "mean": float(np.mean(errors)),
        "std": float(np.std(errors, ddof=1)) if n >= 2 else 0.0,
        "median": float(np.median(errors)),
        "min": float(np.min(errors)),
        "max": float(np.max(errors)),
        "spread68": float(half_spread(68.0)),
        "spread95": float(half_spread(95.0)),
        "spread997": float(half_spread(99.7)),
    }


def format_report(locked_id, messages_count, bucket_errors, status=None):
    lines = []
    bar = "=" * 100
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

    header = (f"  {'gap[s]':>6} {'count':>7} {'mean':>9} {'stdev':>8} "
              f"{'median':>8} {'min':>8} {'max':>8} "
              f"{'±68%':>9} {'±95%':>9} {'±99.7%':>9}")
    lines.append(header)
    lines.append("  " + "-" * (len(header) - 2))
    for p in BUCKETS:
        if p not in bucket_errors:
            continue
        s = bucket_stats(bucket_errors[p])
        flag = "  (low n)" if s["count"] < 10 else ""
        lines.append(
            f"  {p:>6} {s['count']:>7} {s['mean']:>9.3f} {s['std']:>8.3f} "
            f"{s['median']:>8.3f} {s['min']:>8.3f} {s['max']:>8.3f} "
            f"{('±' + format(s['spread68'], '.3f')):>9} "
            f"{('±' + format(s['spread95'], '.3f')):>9} "
            f"{('±' + format(s['spread997'], '.3f')):>9}{flag}"
        )
    lines.append("  (error values in ms; error = decoder_elapsed - transponder_elapsed)")
    lines.append(bar)
    return "\n".join(lines)


def save_stability_chart(locked_id, bucket_errors):
    """On exit, plot per-bucket frequency-stability curves (log-log).

    All three are dimensionless fractional-frequency quantities sharing one axis
    (x 1e6 = ppm):
      * stddev/tau  -- the random part. Falls as 1/tau while jitter-limited,
        bottoms out, then rises again under noise/drift.
      * |mean|/tau  -- the systematic frequency offset. Flat = constant offset;
        a rising slope means the offset itself drifts over the gap.
      * RMS fractional frequency = sqrt(mean^2 + stddev^2)/tau -- the total
        per-sync error budget (random and systematic combined in quadrature).
    Where |mean|/tau overtakes stddev/tau, systematic drift dominates -- that gap
    is your practical re-sync horizon. NB: none of these is the Allan deviation
    (that needs the neighbour-difference estimator), so the file is not named so.
    """
    points = []
    for p in BUCKETS:
        errs = bucket_errors.get(p)
        if errs and len(errs) >= 2:
            arr = np.asarray(errs, dtype=np.float64)
            std_ms = float(np.std(arr, ddof=1))
            mean_ms = float(np.mean(arr))
            rand = (std_ms / 1000.0) / p                 # random, fractional
            offset = abs(mean_ms / 1000.0) / p           # systematic, fractional
            rms = (math.hypot(mean_ms, std_ms) / 1000.0) / p   # total, fractional
            points.append((p, rand, offset, rms, std_ms, mean_ms, len(errs)))
    if len(points) < 2:
        print("Not enough buckets to plot a stability chart.")
        return

    tau = [p for p, *_ in points]
    rand = [r for _, r, _, _, _, _, _ in points]
    offset = [o for _, _, o, _, _, _, _ in points]
    rms = [q for _, _, _, q, _, _, _ in points]
    # sign of the offset (clock fast -> error negative -> clock runs ahead),
    # taken from the longest-gap bucket where it is most reliable.
    sign = "fast" if points[-1][5] < 0 else "slow"
    off_ppm = offset[-1] * 1e6

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not available; data (tau[s], stddev/tau, |mean|/tau, "
              "RMS/tau, stddev[ms], mean[ms], count):")
        for p, r, o, q, s, m, n in points:
            print(f"  {p:>6}  {r:.3e}  {o:.3e}  {q:.3e}  {s:9.3f}  {m:9.3f}  {n}")
        return

    fig, ax = plt.subplots(figsize=(8, 6))
    ax.loglog(tau, rms, "^-", color="#2ca02c", lw=2.0, ms=7,
              label=r"RMS fractional frequency:  $\sqrt{mean^2+stddev^2}/\tau$")
    ax.loglog(tau, rand, "o-", color="#1f77b4", lw=1.8, ms=7,
              label="random:  stddev / tau")
    ax.loglog(tau, offset, "s-", color="#d62728", lw=1.8, ms=7,
              label=f"systematic:  |mean| / tau  (clock {sign})")

    # tau^-1 reference (phase noise / jitter), anchored to the first point
    tref = np.array([tau[0], tau[min(len(tau) - 1, 5)]], dtype=float)
    ax.loglog(tref, rand[0] * (tref / tau[0]) ** -1.0, "--", color="gray",
              lw=1, label=r"$\tau^{-1}$ (jitter)")

    # right axis in ppm (same quantity x 1e6)
    secax = ax.secondary_yaxis(
        "right", functions=(lambda y: y * 1e6, lambda y: y / 1e6))
    secax.set_ylabel("ppm")

    ax.set_xlabel("gap  tau  [s]")
    ax.set_ylabel("fractional frequency deviation")
    ax.set_title(f"timesync stability - transponder {locked_id} "
                 f"(offset {off_ppm:.1f} ppm {sign}, log-log)")
    ax.grid(True, which="both", ls=":", alpha=0.5)
    ax.legend(fontsize=9)
    fig.tight_layout()

    fname = f"freq_stability_{locked_id}_{int(time.time())}.png"
    path = os.path.abspath(fname)
    fig.savefig(path, dpi=130)
    plt.close(fig)
    print(f"stability chart saved to {path}")


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
            save_stability_chart(locked_id, bucket_errors)


if __name__ == "__main__":
    main()
