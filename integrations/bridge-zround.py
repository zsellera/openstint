#!/usr/bin/env python3
"""
Async OpenStint → ZRound bridge.
"""

import asyncio
import zmq
import zmq.asyncio
import argparse
import time


class Bridge:
    def __init__(self):
        self.zround_writer = None
        self.race_running = False
        self.base_decoder_ts = None
        self.base_race_time = 0

    async def handle_zround(self, reader, writer):
        """Handle one ZRound TCP connection."""
        self.zround_writer = writer
        peer = writer.get_extra_info("peername")
        print(f"[zround] connected from {peer}")
        try:
            while True:
                data = await reader.readuntil(b"&")
                msg = data.decode().strip()
                print(f"[zround] RX {msg}")

                if msg.startswith("%C"):
                    await self.send("%A&")  # ACK
                elif msg.startswith("%I"):
                    self.race_running = True
                    self.base_decoder_ts = None
                    self.base_race_time = time.monotonic()
                    print("[bridge] race started")
                elif msg.startswith("%F"):
                    self.race_running = False
                    print("[bridge] race stopped")
        except asyncio.IncompleteReadError:
            pass
        except Exception as e:
            print(f"[zround] connection error: {e}")
        finally:
            print("[zround] disconnected")
            self.zround_writer = None

    async def send(self, msg: str):
        """Send a ZRound message if connected."""
        if self.zround_writer:
            self.zround_writer.write(msg.encode())
            await self.zround_writer.drain()
            print(f"[zround] TX {msg.strip()}")

    async def handle_openstint(self, zmq_url):
        """Subscribe to OpenStint PUB socket."""
        ctx = zmq.asyncio.Context()
        sock = ctx.socket(zmq.SUB)
        sock.setsockopt_string(zmq.SUBSCRIBE, "")
        sock.connect(zmq_url)
        print(f"[openstint] subscribed to {zmq_url}")

        while True:
            msg = await sock.recv_string()
            if msg.startswith('P '):
                parts = msg.split(' ')
                if len(parts) < 5:
                    continue
                
                decoder_time = int(parts[1])
                transponder_id = int(parts[3])
                await self.translate_openstint_passing(decoder_time, transponder_id)
            if msg.startswith('S '):
                parts = msg.split(' ')
                if len(parts) < 6:
                    continue
                decoder_time = int(parts[1])
                await self.translate_openstint_status(decoder_time)

    async def translate_openstint_passing(self, decoder_time: int, transponder_id: int):
        if not self.race_running:
            return

        decoder_time_seconds = float(decoder_time)/1000.0
        if self.base_decoder_ts is None:
            time_passed_since_race_start = time.monotonic() - self.base_race_time
            self.base_decoder_ts = decoder_time_seconds - time_passed_since_race_start
            print(f"[bridge] set base_decoder_ts={self.base_decoder_ts}")

        race_time = int(1000*(decoder_time_seconds - self.base_decoder_ts))
        tx_hex = format(transponder_id, "X")
        time_hex = format(race_time, "X")

        # Send lap message
        await self.send(f"%L{tx_hex},{time_hex}&")
    
    async def translate_openstint_status(self, decoder_time: int):
        if not self.race_running:
            return
         
        if self.base_decoder_ts is None:
            return

        decoder_time_seconds = float(decoder_time)/1000.0
        race_time = int(1000*(decoder_time_seconds - self.base_decoder_ts))
        time_hex = format(race_time, "X")

        # Send lap message and optional clock
        await self.send(f"%T{time_hex}&")

async def main():
    parser = argparse.ArgumentParser(description='Read OpenStint decoder and stream results to a ZRound')
    parser.add_argument('--host', help='OpenStint host', default='127.0.0.1')
    parser.add_argument('--port', help='OpenStint port', default='5556')
    parser.add_argument('--listen', help='ZRound protocol listen port', default='5001')
    args = parser.parse_args()

    bridge = Bridge()

    # TCP server for ZRound
    server = await asyncio.start_server(bridge.handle_zround, "0.0.0.0", args.listen)
    print(f"[bridge] listening for ZRound on port {args.listen}")

    # ZMQ address
    zmq_sub = f"tcp://{args.host}:{args.port}"

    # Run both OpenStint subscriber and TCP server concurrently
    async with server:
        await asyncio.gather(
            bridge.handle_openstint(zmq_sub),
            server.serve_forever()
        )

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("bye")
