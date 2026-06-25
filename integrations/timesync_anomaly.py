#!/usr/bin/env python3
"""
Timesync anomaly detector for OpenStint transponders.

Connects to a decoder's ZeroMQ pub channel and watches the timesync ("T")
messages of one transponder. Between two consecutive messages, three independent
clocks should each advance by the SAME amount:

    * local time       -- monotonic clock of this machine when the message arrives
    * decoder time     -- decoder_timestamp (ms)
    * transponder time -- transponder timecode (100 us ticks, unwrapped)

If the three per-step increments disagree by more than ANOMALY_MS (default
200 ms), the step is flagged as an anomaly: the script prints the previous 10
timesync messages for context, keeps printing the next 10, and beeps.

Because the three increments are compared against EACH OTHER (not against an
expected 1 s), a dropped message just makes all three ~2 s and still agrees --
only a genuine stall/jump on one clock trips the detector.

Usage:
    timesync_anomaly.py [host] [port] [transponder_id]
    (host defaults to 127.0.0.1, port defaults to 5556)
"""

import sys
import time
import shutil
import subprocess
from collections import deque

import zmq


# 20-bit, 100 us transponder timecode -> wraps every 2**20 ticks = 104.8576 s
TIMECODE_WRAP = 1 << 20

ANOMALY_MS = 200.0      # flag when the 3 increments disagree by more than this
CONTEXT_BEFORE = 10     # messages printed before an anomaly
CONTEXT_AFTER = 10      # messages printed after an anomaly
HEARTBEAT_S = 60.0      # "still alive" line cadence when all is quiet


def beep(times=3):
    """Audible alert: ASCII bell, plus a system sound on macOS if available."""
    for _ in range(times):
        sys.stdout.write("\a")
        sys.stdout.flush()
        time.sleep(0.15)
    if sys.platform == "darwin" and shutil.which("afplay"):
        try:
            subprocess.Popen(
                ["afplay", "/System/Library/Sounds/Sosumi.aiff"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            pass


def unwrap_tc_delta(raw_delta, decoder_delta_ms):
    """Timecode increment (ms), picking the rollover closest to the decoder."""
    expected_ticks = decoder_delta_ms * 10.0
    rollover = round((expected_ticks - raw_delta) / TIMECODE_WRAP)
    return (raw_delta + rollover * TIMECODE_WRAP) / 10.0


def format_record(rec):
    line = f"  #{rec['idx']:<7} {rec['raw']}"
    if rec["d_dec"] is None:
        return line + "    (first message - no delta)"
    mark = "   <<< ANOMALY" if rec["anomaly"] else ""
    return (line + "\n"
            f"          dlocal={rec['d_loc']:9.1f}  ddecoder={rec['d_dec']:9.1f}  "
            f"dtransponder={rec['d_tc']:9.1f}  spread={rec['spread']:8.1f} ms"
            + mark)


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

    print(f"Connected to {address}. Watching for timesync anomalies "
          f"(>{ANOMALY_MS:.0f} ms).")
    if locked_id is not None:
        print(f"Filtering on transponder {locked_id}.")

    history = deque(maxlen=CONTEXT_BEFORE + 1)  # recent records incl. current
    idx = 0                 # running message index
    prev = None             # previous (local_ms, decoder_ms, tc_ticks)
    last_printed_idx = -1   # highest idx already printed
    print_until_idx = -1    # keep printing through this idx (next-N window)
    n_anomalies = 0
    next_heartbeat = time.monotonic() + HEARTBEAT_S

    try:
        while True:
            # poll so the heartbeat can fire even if messages stall
            if socket.poll(1000) == 0:
                if time.monotonic() >= next_heartbeat:
                    next_heartbeat += HEARTBEAT_S
                    print(f"  ... {idx} messages, {n_anomalies} anomalies, "
                          f"monitoring", flush=True)
                continue

            msg = socket.recv_string().strip()
            local_ms = time.monotonic() * 1000.0
            parts = msg.split()
            # T <decoder_ts_ms> <type> <id> <timecode_100us>
            if len(parts) < 5 or parts[0] != "T" or parts[2] != "OPN":
                continue
            try:
                decoder_ms = float(parts[1])
                tid = int(parts[3])
                tc_ticks = int(parts[4]) % TIMECODE_WRAP
            except ValueError:
                continue

            if locked_id is None:
                locked_id = tid
                print(f"Locked onto transponder {locked_id}.", flush=True)
            if tid != locked_id:
                continue

            rec = {"idx": idx, "raw": msg, "d_loc": None, "d_dec": None,
                   "d_tc": None, "spread": None, "anomaly": False}

            if prev is not None:
                d_loc = local_ms - prev[0]
                d_dec = decoder_ms - prev[1]
                if d_dec < 0:
                    # decoder restarted; deltas meaningless, skip this step
                    print(f"  #{idx}: decoder timestamp went backwards "
                          f"(restart) - resetting reference.", flush=True)
                    prev = (local_ms, decoder_ms, tc_ticks)
                    history.append(rec)
                    idx += 1
                    continue
                d_tc = unwrap_tc_delta(tc_ticks - prev[2], d_dec)
                spread = abs(d_dec - d_tc)
                rec.update(d_loc=d_loc, d_dec=d_dec, d_tc=d_tc, spread=spread,
                           anomaly=spread > ANOMALY_MS)

            history.append(rec)

            if rec["anomaly"]:
                n_anomalies += 1
                fresh = idx > print_until_idx
                if fresh:
                    # opening a new context window: print the preceding records
                    print("\n" + "!" * 72)
                    print(f"ANOMALY #{n_anomalies} at message #{idx}: "
                          f"clocks disagree by {rec['spread']:.1f} ms "
                          f"(local {rec['d_loc']:.1f} / decoder {rec['d_dec']:.1f} "
                          f"/ transponder {rec['d_tc']:.1f} ms)")
                    print("!" * 72)
                    start = max(last_printed_idx + 1, idx - CONTEXT_BEFORE)
                    for r in history:
                        if start <= r["idx"] <= idx:
                            print(format_record(r))
                    last_printed_idx = idx
                else:
                    # already inside a window: just print this line + re-beep
                    print(format_record(rec))
                    last_printed_idx = idx
                print_until_idx = idx + CONTEXT_AFTER
                beep()
            elif idx <= print_until_idx:
                # inside the next-N window after an anomaly
                print(format_record(rec))
                last_printed_idx = idx
                if idx == print_until_idx:
                    print("-" * 72 + "  (end of context)\n", flush=True)

            prev = (local_ms, decoder_ms, tc_ticks)
            idx += 1

    except KeyboardInterrupt:
        pass
    finally:
        socket.close()
        context.term()
        print(f"\nStopped. {idx} messages, {n_anomalies} anomalies.")


if __name__ == "__main__":
    main()
