#!/usr/bin/env python3
"""
OpenStint decoder simulator.

Simulates decoder behavior by publishing status messages and transponder passings
over ZMQ. Useful for testing laptimer software without hardware.

Usage:
    python simulator.py 30:3 40:5

    This starts the simulator with two transponders:
    - Transponder 1: never faster than 30 s, median lap 33 s
    - Transponder 2: never faster than 40 s, median lap 45 s

Each positional argument describes one transponder as `period:median_gap`,
both in seconds. Lap times are drawn from a shifted lognormal distribution
rather than a normal one, because real lap times are skewed: `period` is the
fastest possible lap and `median_gap` is the median time lost on top of it,
so half the laps land below `period + median_gap` and half above, with a long
tail. See `Simulator.passing_loop` for the details.
"""

import argparse
import random
import sys
import threading
import time
from math import exp, floor, log

import zmq


class Simulator:
    def __init__(self, port: int = 5556):
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.PUB)
        self.socket.bind(f"tcp://*:{port}")
        self.lock = threading.Lock()
        self.running = True
        self.start_time = time.monotonic()
        self.hits = 0
        print(f"[simulator] publishing on port {port}")

    def get_timecode(self) -> int:
        """Get current timecode in milliseconds since start."""
        return int((time.monotonic() - self.start_time) * 1000)

    def publish(self, message: str):
        """Thread-safe message publishing."""
        with self.lock:
            self.socket.send_string(message)
            print(f"[tx] {message}")

    def status_loop(self):
        """Publish status messages every second."""
        while self.running:
            timecode = self.get_timecode()
            noise = -40 + random.gauss(0, 2)
            msg = f"S {timecode} {noise:.2f} 5 {self.hits} {floor(self.hits*random.randint(50, 99)/100)}"
            self.hits = 0
            self.publish(msg)
            time.sleep(1)

    def passing_loop(self, transponder_id: int, period: float, median_gap: float):
        """Generate passings at lognormally distributed intervals.

        Real lap times are not normally distributed. There is a hard floor set
        by car, driver and track, and a long tail above it caused by traffic,
        mistakes and off-track excursions: a driver can never go much faster
        than their best lap, but can always be a lot slower. Of the generators
        tried against recorded sessions, a shifted lognormal reproduced that
        skew best, so the interval is

            interval = period + median_gap * exp(gauss(0, sigma))

        which is a lognormal of median `median_gap` shifted up by `period`.

        period (seconds)
            Hard lower bound on the interval, i.e. the "perfect lap". No
            generated interval is ever shorter than this.

        median_gap (seconds)
            Median time lost on top of `period`, so the median interval is
            exactly `period + median_gap`: half the laps come in under it,
            half over, with the slow half stretching much further from the
            median than the fast half. Set it to 0 for a metronome that
            repeats `period` exactly.

        The spread is not a separate knob. It is derived as

            sigma = log(1 + median_gap) / 10

        so a driver who loses more time per lap is also less consistent, which
        is what the recorded sessions show. Typical values, for `period` 32 s:

            median_gap | median lap | 5th-95th percentile |
            -----------+------------+---------------------+-------------------
                     0 |     32.0 s | exact               | metronome
                     1 |     33.0 s | 32.9 ..  33.1 s     | very consistent
                     3 |     35.0 s | 34.4 ..  35.8 s     | typical club racer
                     5 |     37.0 s | 35.7 ..  38.7 s     | inconsistent
                    10 |     42.0 s | 38.7 ..  46.8 s     | wet / heavy traffic
                    20 |     52.0 s | 44.1 ..  65.0 s     | practice, out laps

        Note that `median_gap` is time *lost*, not the lap time itself: a kart
        session with 32 s laps is `30:2`, not `30:32`.
        """
        # exp(gauss(0, sigma)) rather than lognormvariate(log(median_gap), ...)
        # so that median_gap=0 degenerates to a fixed period instead of log(0).
        sigma = log(1.0 + median_gap) / 10.0

        while self.running:
            interval = period + median_gap * exp(random.gauss(0.0, sigma))
            time.sleep(interval)

            timecode = self.get_timecode()
            rssi = random.gauss(-10, 3)
            hit_count = random.randint(20, 80)
            pass_duration = random.randint(80000, 110000)
            self.hits += hit_count

            passing_msg = f"P {timecode} OPN {transponder_id} {rssi:.2f} {hit_count} {pass_duration}"
            self.publish(passing_msg)

            # 1/5 chance to generate a timesync message
            if random.random() < 0.2:
                transponder_timecode = random.randint(100000, 999999)
                timesync_msg = f"T {timecode} OPN {transponder_id} {transponder_timecode}"
                self.publish(timesync_msg)

    def stop(self):
        """Stop all loops."""
        self.running = False


def parse_period_gap(arg: str) -> tuple[float, float]:
    """Parse 'period:median_gap' argument, both in seconds.

    `period` is the fastest possible lap, `median_gap` the median time lost on
    top of it. See `Simulator.passing_loop`.
    """
    parts = arg.split(":")
    if len(parts) != 2:
        raise ValueError(f"Invalid format: {arg}. Expected 'period:median_gap'")
    period, median_gap = float(parts[0]), float(parts[1])
    if period <= 0:
        raise ValueError(f"Invalid period in {arg}: must be positive")
    if median_gap < 0:
        raise ValueError(f"Invalid median_gap in {arg}: must be zero or positive")
    return period, median_gap


def main():
    parser = argparse.ArgumentParser(
        description="OpenStint decoder simulator",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Example: %(prog)s 30:3 40:5  (two transponders with different pace)\n"
            "\n"
            "Both fields are seconds. 'period' is the fastest possible lap, and\n"
            "'median_gap' is the median time lost on top of it, so the median lap\n"
            "is period + median_gap. Laps are lognormally scattered around that\n"
            "median: never below period, with a long slow tail. Use 0 for a\n"
            "metronome. Note median_gap is time lost, not the lap time itself --\n"
            "a 32s kart lap is '30:2', not '30:32'."
        ),
    )
    parser.add_argument(
        "--port",
        type=int,
        default=5556,
        help="ZMQ publisher port (default: 5556)",
    )
    parser.add_argument(
        "transponders",
        nargs="*",
        metavar="period:median_gap",
        help=(
            "Transponder timing in seconds; e.g. '30:3' means laps never faster "
            "than 30s, with a median of 33s. See the notes below"
        ),
    )
    args = parser.parse_args()

    sim = Simulator(port=args.port)

    threads = []

    # Start status thread
    status_thread = threading.Thread(target=sim.status_loop, daemon=True)
    status_thread.start()
    threads.append(status_thread)

    # Start transponder threads
    for i, spec in enumerate(args.transponders, start=1):
        try:
            period, median_gap = parse_period_gap(spec)
        except ValueError as e:
            print(f"Error: {e}", file=sys.stderr)
            sys.exit(1)

        transponder_id = 1000000 + i
        print(
            f"[simulator] transponder {transponder_id}: period={period}s, "
            f"median_gap={median_gap}s (median lap {period + median_gap:.1f}s)"
        )

        t = threading.Thread(
            target=sim.passing_loop,
            args=(transponder_id, period, median_gap),
            daemon=True,
        )
        t.start()
        threads.append(t)

    print("[simulator] running (Ctrl+C to stop)")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n[simulator] stopping...")
        sim.stop()


if __name__ == "__main__":
    main()
