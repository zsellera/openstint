#!/usr/bin/env python3
import sys
import zmq

"""
This tool was generated with ChatGPT, using the following prompt:

Write a python3 program that connects to a host and port specified
in program argument as a ZeroMQ subscriber. It will receive string
messages. Out of these messages, some start with the letter "P".
These lines represent vehicle passings on a racetrack.

It has the following format:
P <uint64_t timecode> <string transponder_type> <uint32_t transponder_id> <float rssi> <int hit_count> [other optional arguments]

Collect "transponder_id"-"timecode" in a dict to remember the time of
the last passing. If a transpoder_id had already been seen, print the
difference of the current timecode and last seen timecode to console
(the laptime). The timecode is milliseconds resolution, but the printed 
text should be seconds resolution with a single decimal digits. If the
laptime is longer than 2 minutes, do not print anything.
"""

def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <host> <port>")
        sys.exit(1)

    host = sys.argv[1]
    port = sys.argv[2]
    address = f"tcp://{host}:{port}"

    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    socket.connect(address)
    socket.setsockopt_string(zmq.SUBSCRIBE, "")

    print(f"Connected to {address}. Waiting for messages...")

    last_seen = {}  # transponder_id -> last timecode (ms)

    try:
        while True:
            msg = socket.recv_string().strip()
            if not msg.startswith("P "):
                continue  # ignore non-"P" messages

            parts = msg.split()
            if len(parts) < 6:
                continue  # malformed message

            try:
                timecode = int(parts[1])
                # transponder_type = parts[2]
                transponder_id = int(parts[3])
                # rssi = float(parts[3])
                # hit_count = int(parts[4])
            except ValueError:
                continue  # skip malformed data

            if transponder_id in last_seen:
                diff_ms = timecode - last_seen[transponder_id]
                if 0 < diff_ms <= 120_000:  # ignore laps > 2 minutes
                    diff_s = diff_ms / 1000.0
                    print(f"Transponder {transponder_id}: {diff_s:.1f} s")

            # Update last seen time
            last_seen[transponder_id] = timecode

    except KeyboardInterrupt:
        print("\nInterrupted by user.")
    finally:
        socket.close()
        context.term()

if __name__ == "__main__":
    main()