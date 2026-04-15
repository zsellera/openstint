#!/usr/bin/env python3
"""
OpenStint → RCGTiming bridge.

Streams decoder status and passing events to rcgtiming.com via HTTP POST.
API endpoints: /ambbox/status and /ambbox/event
API reference: https://rcgtiming.com/apidoc/index.html
"""

import sys
import zmq
import argparse
import urllib.request
import urllib.error
import urllib.parse
import time


CONNECT_TIMEOUT = 5
RESPONSE_TIMEOUT = 5


def post(url, params, token):
    """POST form-urlencoded data with X-Auth-Token header. Returns True on success."""
    query = urllib.parse.urlencode(params)
    full_url = f"{url}?{query}"
    try:
        req = urllib.request.Request(
            full_url,
            data=b"",
            headers={"X-Auth-Token": token},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=RESPONSE_TIMEOUT) as resp:
            resp.read()
        return True
    except (urllib.error.URLError, urllib.error.HTTPError, OSError) as e:
        print(f"[rcgtiming] POST {url} failed: {e}")
        return False


def post_status(base_url, token, online):
    """Post decoder status to /ambbox/status."""
    url = base_url.rstrip("/") + "/ambbox/status"
    post(url, {"status": 1 if online else 0}, token)


def post_event(base_url, token, transponder, timestamp, localts, signal=None, noise=None):
    """Post a passing event to /ambbox/event."""
    url = base_url.rstrip("/") + "/ambbox/event"
    params = {
        "transponder": transponder,
        "timestamp": timestamp,
        "localts": localts,
    }
    if signal is not None:
        params["signal"] = signal
    if noise is not None:
        params["noise"] = noise
    post(url, params, token)


def parse_status_message(msg):
    """Parse an OpenStint 'S' status message. Returns dict or None.
    Format: S <decoder_timestamp:uint64> <noise_power:float> <dc_offset_magnitude:float> <frames_received> <frames_processed>
    """
    parts = msg.split()
    if len(parts) < 6:
        return None
    try:
        return {
            "decoder_time": int(parts[1]),
            "noise_power": float(parts[2]),
            "dc_offset": float(parts[3]),
            "frames_received": int(parts[4]),
            "frames_processed": int(parts[5]),
        }
    except (ValueError, IndexError):
        return None


def parse_passing_message(msg):
    """Parse an OpenStint 'P' passing message. Returns dict or None."""
    parts = msg.split()
    if len(parts) < 6:
        return None
    try:
        return {
            "timecode": int(parts[1]),
            "transponder_type": parts[2],
            "transponder_id": int(parts[3]),
            "rssi": float(parts[4]),
            "hit_count": int(parts[5]),
        }
    except (ValueError, IndexError):
        return None


def main():
    parser = argparse.ArgumentParser(
        description="Read OpenStint decoder and stream results to RCGTiming"
    )
    parser.add_argument(
        "--host", help="OpenStint host", default="127.0.0.1"
    )
    parser.add_argument(
        "--port", help="OpenStint port", default="5556"
    )
    parser.add_argument(
        "--url",
        help="RCGTiming API base URL",
        default="https://rcgtiming.com/API/",
    )
    parser.add_argument(
        "--token",
        help="RCGTiming API token (prompted if not provided)",
        default=None,
    )
    args = parser.parse_args()

    token = args.token
    if not token:
        try:
            token = input("Enter RCGTiming API token: ").strip()
        except EOFError:
            print("No token provided, exiting.")
            sys.exit(1)
    if not token:
        print("No token provided, exiting.")
        sys.exit(1)

    # ZeroMQ connection
    address = f"tcp://{args.host}:{args.port}"
    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    socket.setsockopt_string(zmq.SUBSCRIBE, "")
    socket.setsockopt(zmq.RCVTIMEO, 5000)  # 5s receive timeout for disconnect detection
    socket.connect(address)
    print(f"[openstint] connected to {address}")

    last_status = None
    connected = False

    try:
        while True:
            try:
                msg = socket.recv_string().strip()
            except zmq.Again:
                # No message within timeout — decoder is offline
                if connected:
                    print("[openstint] no messages received, posting offline status")
                    post_status(args.url, token, online=False)
                    connected = False
                continue

            if not connected:
                connected = True
                print("[openstint] receiving messages")

            print(f"[openstint] {msg}")

            if msg.startswith("S "):
                status = parse_status_message(msg)
                if status:
                    last_status = status
                    post_status(args.url, token, online=True)

            elif msg.startswith("P "):
                passing = parse_passing_message(msg)
                if passing:
                    # timestamp: decoder timecode in seconds (float)
                    timestamp = passing["timecode"] / 1000.0
                    # localts: current UNIX timestamp
                    localts = int(time.time())
                    # signal: RSSI value from the passing
                    signal = int(passing["rssi"])
                    # noise: from last known status, if available
                    noise = int(last_status["noise_power"]) if last_status else None

                    post_event(
                        args.url, token,
                        transponder=passing["transponder_id"],
                        timestamp=timestamp,
                        localts=localts,
                        signal=signal,
                        noise=noise,
                    )

    except KeyboardInterrupt:
        print("\n[bridge] interrupted, posting offline status...")
        post_status(args.url, token, online=False)
    finally:
        socket.close()
        context.term()
        print("[bridge] shutdown complete")


if __name__ == "__main__":
    main()
