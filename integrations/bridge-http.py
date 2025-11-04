#!/usr/bin/env python3
import sys
import zmq
import json
import argparse
import urllib.request

def post_json(url, data):
    try:
        req = urllib.request.Request(
            url,
            data=json.dumps(data).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST"
        )
        with urllib.request.urlopen(req, timeout=1) as resp:
            resp.read()  # discard response
    except Exception:
        print("POST FAILED\n")

def main():
    parser = argparse.ArgumentParser(description='Read OpenStint decoder and stream results to a HTTP endpoint')
    parser.add_argument('--host', help='OpenStint host', default='127.0.0.1')
    parser.add_argument('--port', help='OpenStint port', default='5556')
    parser.add_argument('url', help='destination url')
    args = parser.parse_args()

    # ZeroMQ connection
    address = f"tcp://{args.host}:{args.port}"
    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    socket.connect(address)
    socket.setsockopt_string(zmq.SUBSCRIBE, "")
    print(f"Connected to {address}. Waiting for messages...")

    # Start streaming data
    try:
        while True:
            msg = socket.recv_string().strip()
            print(msg)
            if not msg.startswith("P "):
                continue  # ignore non-"P" messages

            parts = msg.split()
            if len(parts) < 6:
                continue  # malformed message

            try:
                timecode = int(parts[1])
                transponder_type = parts[2]
                transponder_id = int(parts[3])
                rssi = float(parts[4])
                hit_count = int(parts[5])
            except ValueError:
                continue  # skip malformed data

            post_json(args.url, {
                "timecode": timecode,
                "transponder_type": transponder_type,
                "transponder_id": transponder_id,
                "rssi": rssi,
                "hit_count": hit_count,
            })
    except KeyboardInterrupt:
        print("\nInterrupted by user.")
    finally:
        socket.close()
        context.term()

if __name__ == "__main__":
    main()
