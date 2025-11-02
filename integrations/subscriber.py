#!/usr/bin/env python3
import sys
import zmq

"""
This tool was generated with ChatGPT, using the following prompt:

Write a python3 program that connects to a host and port specified
in program argument as a ZeroMQ subscriber, and prints every received
string message to console.
"""

def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <host> <port>")
        sys.exit(1)

    host = sys.argv[1]
    port = sys.argv[2]

    # Create ZeroMQ context and subscriber socket
    context = zmq.Context()
    socket = context.socket(zmq.SUB)

    # Connect to the publisher
    address = f"tcp://{host}:{port}"
    print(f"Connecting to {address} ...")
    socket.connect(address)

    # Subscribe to all messages (empty filter = all topics)
    socket.setsockopt_string(zmq.SUBSCRIBE, "")

    print("Subscribed. Waiting for messages...")
    try:
        while True:
            message = socket.recv_string()
            print(message)
    except KeyboardInterrupt:
        print("\nInterrupted by user.")
    finally:
        socket.close()
        context.term()

if __name__ == "__main__":
    main()
