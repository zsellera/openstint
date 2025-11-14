#!/usr/bin/env python3
import argparse
import time
import zmq
import websocket


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
    print(f"connected to {address}. Waiting for messages...")

    ws = websocket.create_connection(args.url)
    print(f"connected to {args.url}")

    while True:
        try:
            # Reconnect WebSocket if needed
            if not ws or not ws.connected:
                print("reconnecting to WebSocket...")
                ws = websocket.create_connection(args.url)
                print(f"reconnected to {args.url}")

            # Receive message from ZeroMQ
            msg = socket.recv_string()
            print(msg)

            # Send to WebSocket
            ws.send(msg)

        except (websocket.WebSocketConnectionClosedException, ConnectionRefusedError, BrokenPipeError) as e:
            print(f"WebSocket connection failed: {e}. Retrying in 2 seconds...")
            if ws:
                ws.close()
                ws = None
            time.sleep(2)

        except KeyboardInterrupt:
            print("Interrupted, shutting down...")
            break

        except Exception as e:
            import traceback
            print(f"Unexpected error: {e}")
            print(traceback.format_exc())
            time.sleep(2)

    # Cleanup
    if ws:
        ws.close()
    socket.close()
    context.term()

if __name__ == "__main__":
    main()
