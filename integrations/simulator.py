#!/usr/bin/env python3
"""
OpenStint decoder simulator.

Simulates decoder behavior by publishing status messages and transponder passings
over ZMQ. Useful for testing laptimer software without hardware.

Usage:
    python simulator.py 10:1 15:2

    This starts the simulator with two transponders:
    - Transponder 1: passes every ~10 seconds (jitter variance 1)
    - Transponder 2: passes every ~15 seconds (jitter variance 2)
"""

import argparse
import random
import sys
import threading
import time

import zmq


class Simulator:
    def __init__(self, port: int = 5556):
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.PUB)
        self.socket.bind(f"tcp://*:{port}")
        self.lock = threading.Lock()
        self.running = True
        self.start_time = time.monotonic()
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
        """Publish status messages every 5 seconds."""
        while self.running:
            timecode = self.get_timecode()
            noise = -40 + random.gauss(0, 2)
            msg = f"S {timecode} {noise:.2f} 5 0 0"
            self.publish(msg)
            time.sleep(5)

    def passing_loop(self, transponder_id: int, period: float, jitter: float):
        """Generate passings at intervals following normal distribution."""
        while self.running:
            interval = max(0.1, random.gauss(period, jitter))
            time.sleep(interval)

            timecode = self.get_timecode()
            rssi = random.gauss(-10, 3)
            hit_count = random.randint(20, 80)
            pass_duration = random.randint(80, 110)

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


def parse_period_jitter(arg: str) -> tuple[float, float]:
    """Parse 'period:jitter' argument."""
    parts = arg.split(":")
    if len(parts) != 2:
        raise ValueError(f"Invalid format: {arg}. Expected 'period:jitter'")
    return float(parts[0]), float(parts[1])


def main():
    parser = argparse.ArgumentParser(
        description="OpenStint decoder simulator",
        epilog="Example: %(prog)s 10:1 15:2  (two transponders with different periods)",
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
        metavar="period:jitter",
        help="Transponder timing as 'period:jitter' (e.g., '10:1' for 10s mean, 1s variance)",
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
            period, jitter = parse_period_jitter(spec)
        except ValueError as e:
            print(f"Error: {e}", file=sys.stderr)
            sys.exit(1)

        transponder_id = 1000000 + i
        print(f"[simulator] transponder {transponder_id}: period={period}s, jitter={jitter}s")

        t = threading.Thread(
            target=sim.passing_loop,
            args=(transponder_id, period, jitter),
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
