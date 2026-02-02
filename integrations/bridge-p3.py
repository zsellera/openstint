#!/usr/bin/env python3

import asyncio
import zmq
import zmq.asyncio
import argparse
import time
import struct
import traceback
import sys

if sys.platform == 'win32':
    asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())

FORMAT_CHARS = {1: 'B', 2: 'H', 4: 'L', 8: 'Q'}
OPENSTINT_RSSI_FULL_SCALE = 1.76 + 8 * 6.02

def signal_strenght_converter(openstint_signal_level):
    return max(0, int((openstint_signal_level + OPENSTINT_RSSI_FULL_SCALE) * 10.0))

class Bridge:
    def __init__(self, decoder_id):
        # basic init:
        self.last_decoder_timestamp = (0, 0)
        self.p3_writer = None
        # convert decoder_id to int:
        decoder_id_bytes = decoder_id.encode('ascii').ljust(4, b'\x00')
        self.decoder_id = struct.unpack('<L', decoder_id_bytes)[0]
        # keep them in memory to replay, see CLEAR_PASSINGS and RESEND:
        self.passings = []

    async def read_loop(self, reader, writer):
        """Handle one AMB P3 TCP connection."""
        self.p3_writer = writer
        peer = writer.get_extra_info("peername")
        print(f"[p3] connected from {peer}")
        while self.last_decoder_timestamp[0] == 0:
                print('[p3] waiting for OpenStint decoder to send an update...')
                await asyncio.sleep(1)
        try:
            while True:
                # Read until end-of-frame marker (0x8F)
                data = await reader.readuntil(b'\x8f')
                if data:
                    tor = struct.unpack_from('<H', data, 8)[0]
                    print(f'[p3][rx] {tor} {data.hex(sep=":")}')
                    print('[p3][rx][dbg]', p3frame_parse(data))
                    if tor == P3Frame.TOR_VERSION:
                        frame_raw = self.version_response()
                        await self.send_message(frame_raw)
                    if tor == P3Frame.TOR_GET_TIME:
                        frame_raw = self.get_time_response()
                        await self.send_message(frame_raw)
        except asyncio.IncompleteReadError:
            pass
        except Exception as e:
            print(f"[p3] connection error: {e}")
            traceback.print_exc()
        finally:
            print("[p3] disconnected")
            self.p3_writer = None

    async def send_message(self, msg: bytearray):
        if self.p3_writer:
            print('[p3][tx]', msg.hex(sep=":"))
            print('[p3][tx][dbg]', p3frame_parse(msg))
            self.p3_writer.write(msg)
            await self.p3_writer.drain()

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
                decoder_time = int(parts[1])
                transponder_id = int(parts[3])
                rssi = float(parts[4])
                hit_count = int(parts[5])

                # update last decoder timestamo
                self.last_decoder_timestamp = (time.time(), decoder_time)
                # save passing
                self.passings.append((decoder_time, transponder_id, rssi, hit_count))
                
                frame_raw = self.translate_openstint_passing(decoder_time, transponder_id, rssi, hit_count)
                await self.send_message(frame_raw)
            if msg.startswith('S '):
                parts = msg.split(' ')
                decoder_time = int(parts[1])
                noise = float(parts[2])

                # update last decoder timestamo
                self.last_decoder_timestamp = (time.time(), decoder_time)

                frame_raw = self.translate_openstint_status(noise)
                await self.send_message(frame_raw)

    def translate_openstint_passing(self, decoder_time: int, transponder_id: int, rssi: float, hit_count: int):
        p3_frame = P3Frame(P3Frame.TOR_PASSING, [
            P3KeyVal(P3Frame.TOF_PASSING_NUMBER, len(self.passings)),
            P3KeyVal(P3Frame.TOF_TRANSPONDER, transponder_id),
            P3KeyVal(P3Frame.TOF_RTC_TIME, decoder_time*1000),
            P3KeyVal(P3Frame.TOF_STRENGTH, signal_strenght_converter(rssi)),
            P3KeyVal(P3Frame.TOF_HITS, hit_count),
            P3KeyVal(P3Frame.TOF_FLAGS, 0),
            P3KeyVal(P3Frame.TOF_DECODER_ID, self.decoder_id),
        ])
        return p3_frame.prepare_buffer()
    
    def version_response(self):
        p3_frame = P3Frame(P3Frame.TOR_VERSION, [
            P3KeyVal(P3Frame.TOF_DECODER_TYPE, 0x10),
            P3KeyValVariadic(P3Frame.TOF_DESCRIPTION, bytearray('OpenStint', 'ascii')),
            P3KeyValVariadic(P3Frame.TOF_VERSION, bytearray('4.4', 'ascii')),
            P3KeyVal(P3Frame.TOF_RELEASE, 0),
            P3KeyVal(P3Frame.TOF_REGISTRATION, 0),
            P3KeyVal(P3Frame.TOF_BUILD_NUMBER, 0),
            P3KeyVal(P3Frame.TOF_DECODER_ID, self.decoder_id),
        ])
        return p3_frame.prepare_buffer()

    def get_time_response(self):
        dt = (time.time() - self.last_decoder_timestamp[0])*1000 + self.last_decoder_timestamp[1]
        p3_frame = P3Frame(P3Frame.TOR_GET_TIME, [
            P3KeyVal(P3Frame.TOF_GETTIME_RTC, int(dt*1000)),
            P3KeyVal(P3Frame.TOF_UNKNOWN_2, 0),
            P3KeyVal(P3Frame.TOF_UNKNOWN_3, int(dt*1000)),
            P3KeyVal(P3Frame.TOF_DECODER_ID, self.decoder_id),
        ])
        return p3_frame.prepare_buffer()
    
    def translate_openstint_status(self, noise_level: float):
        p3_frame = P3Frame(P3Frame.TOR_STATUS, [
            P3KeyVal(P3Frame.TOF_NOISE, signal_strenght_converter(noise_level)),
            P3KeyVal(P3Frame.TOF_GPS, 0),
            P3KeyVal(P3Frame.TOF_TEMPERATURE, 0),
            P3KeyVal(P3Frame.TOF_INPUT_VOLTAGE, 50),
            P3KeyVal(P3Frame.TOF_DECODER_ID, self.decoder_id),
        ])
        return p3_frame.prepare_buffer()
        

class P3KeyVal:
    """ Key-value pair parameters of a P3 message """
    def __init__(self, tor, value):
        self.tor = tor[0]
        self.size = tor[1]
        self.format_char = FORMAT_CHARS[self.size]
        self.value = value
    
    def encode(self, target_buffer, target_offset):
        target_buffer.extend(b'\x00' * (self.size + 2))
        struct.pack_into('<BB%s' % self.format_char, target_buffer, target_offset, self.tor, self.size, self.value)

class P3KeyValVariadic:
    """ Vairadic-length values (ie. strings) """
    def __init__(self, tor: int, value: bytearray):
        self.tor = tor
        self.size = len(value)
        self.value = value
    
    def encode(self, target_buffer, target_offset):
        target_buffer.extend(b'\x00' * (self.size + 2))
        struct.pack_into('<BB', target_buffer, target_offset, self.tor, self.size)
        for i in range(self.size):
            target_buffer[target_offset+2+i] = self.value[i]

class P3Frame:
    # https://www.hobbytalk.com/threads/amb-protocol.73738/page-2
    DEFAULT_VERSION = 0x02

    # Message types (type of record)
    TOR_PASSING = 0x01
    TOR_STATUS = 0x02
    TOR_VERSION = 0x03
    TOR_RESEND = 0x04
    TOR_CLEAR_PASSING = 0x05
    TOR_GET_TIME = 0x24

    # Generic fields
    TOF_DECODER_ID = (0x81, 4)
    TOF_CONTROLLER_ID = (0x83, 4)
    TOF_REQUEST_ID = (0x85, 8)

    # PASSING fields
    TOF_PASSING_NUMBER = (0x01, 4)
    TOF_TRANSPONDER = (0x03, 4)
    TOF_RTC_ID = (0x13, 4)
    TOF_RTC_TIME = (0x04, 8)
    TOF_UTC_TIME = (0x10, 8)
    TOF_STRENGTH = (0x05, 2)
    TOF_HITS = (0x06, 2)
    TOF_FLAGS = (0x08, 2)
    TOF_TRAN_CODE = (0x0a, 1)
    TOF_USER_FLAG = (0x0e, 4)
    TOF_DRIVER_ID = (0x0f, 1)
    TOF_SPORT = (0x14, 1)
    TOF_VOLTAGE = (0x30, 1)     # V = (float)voltage/10
    TOF_TEMPERATURE = (0x31, 1) # T = temperature - 100

    # STATUS fields
    TOF_NOISE = (0x01, 2)
    TOF_GPS = (0x06, 1)         # (0=false, 1 =true)
    TOF_TEMPERATURE = (0x07, 2)
    TOF_SATINUSE = (0x0a, 1)
    TOF_LOOP_TRIGGERS = (0x0b, 1)
    TOF_INPUT_VOLTAGE = (0x0c, 1) # input_voltage*10

    # VERSION fields
    TOF_DECODER_TYPE = (0x01, 1)
    TOF_DESCRIPTION = 0x02      # variadic length
    TOF_VERSION = 0x03          # variadic length
    TOF_RELEASE = (0x04, 4)
    TOF_REGISTRATION = (0x08, 8)
    TOF_BUILD_NUMBER = (0x0A, 2)
    TOF_OPTIONS = (0x0C, 4)

    # TOR_RESEND fields
    TOF_FROM = (0x01, 4)
    TOF_UNTIL = (0x02, 4)

    # GET_TIME fields
    TOF_GETTIME_RTC = (0x01, 8)
    TOF_UNKNOWN_2 = (0x04, 2)
    TOF_UNKNOWN_3 = (0x05, 8)

    # CRC16 lookup table (initialized once at class level)
    CRC16_TABLE = None

    @classmethod
    def _init_crc16_table(cls):
        cls.CRC16_TABLE = [0] * 256
        for i in range(256):
            crc = i << 8
            for j in range(8):
                if crc & 0x8000:
                    crc = (crc << 1) ^ 0x1021
                else:
                    crc = crc << 1
                crc &= 0xFFFF  # Keep it 16-bit
            cls.CRC16_TABLE[i] = crc

    def __init__(self, tor: int, fields: list[P3KeyVal]):
        self.version = P3Frame.DEFAULT_VERSION
        self.tor = tor
        self.fields = fields
        # Initialize CRC table on first instance creation
        P3Frame._init_crc16_table()

    def prepare_buffer(self):
        # CONSTRUCT BUFFER
        buffer = bytearray(10)
        buffer[0] = 0x8e
        # header: version(1) length(2) crc(2) flags(2) tor(2)
        struct.pack_into('<BHHHH', buffer, 1, self.version, 0, 0, 0, self.tor)
        # payload
        for field in self.fields:
            field.encode(buffer, len(buffer))
        buffer += b'\x8f'

        # UPDATE FRAME HEADERS
        struct.pack_into('<H', buffer, 2, len(buffer))
        struct.pack_into('<H', buffer, 4, self.crc16(buffer))

        return escape_buffer(buffer)
    
    @classmethod
    def crc16(cls, buffer):
        """Calculate CRC16 for the buffer (excluding first and last bytes)."""
        crc = 0xFFFF
        for i in range(len(buffer)):
            byte = buffer[i]
            crc = cls.CRC16_TABLE[((crc >> 8) & 0xFF)] ^ (crc << 8) ^ byte
            crc &= 0xFFFF  # Keep it 16-bit
        return crc



def escape_buffer(buffer):
    """Escape bytes in range 0x8A-0x8F (except first and last byte).

    Escaped bytes are prefixed with 0x8D and the byte value is increased by 0x20.
    These escape sequences are not counted in the message length.
    """
    result = bytearray()
    result.append(buffer[0])  # First byte unchanged

    # Process middle bytes
    for i in range(1, len(buffer) - 1):
        byte = buffer[i]
        if 0x8A <= byte <= 0x8F:
            result.append(0x8D)  # Escape prefix
            result.append(byte + 0x20)  # Escaped value
        else:
            result.append(byte)

    result.append(buffer[-1])  # Last byte unchanged
    return result


def deescape_buffer(buffer):
    """Reverse the escaping done by _escape_buffer().

    When 0x8D is encountered in the middle bytes, the next byte has 0x20
    subtracted to restore the original value.
    """
    result = bytearray()
    result.append(buffer[0])  # First byte unchanged

    # Process middle bytes
    i = 1
    while i < len(buffer) - 1:
        byte = buffer[i]
        if byte == 0x8D and i + 1 < len(buffer) - 1:
            result.append(buffer[i + 1] - 0x20)  # Restore original value
            i += 2
        else:
            result.append(byte)
            i += 1

    result.append(buffer[-1])  # Last byte unchanged
    return result


def p3frame_parse(buffer):
    deescaped = deescape_buffer(buffer)
    version, length, crc, flags, tor = struct.unpack_from('<BHHHH', deescaped, 1)
    recs = {}

    def read_buffer(buffer, offset, size):
        format_char = FORMAT_CHARS.get(size)
        if format_char:
            data = struct.unpack_from('<%s' % format_char, buffer, offset)
            return data[0]
        else:
            return buffer[offset:offset+size].hex(sep=":")

    i = 10
    while i<len(buffer)-1:
        rec = buffer[i]
        size = buffer[i+1]
        data = read_buffer(buffer, i+2, size) if size>0 else None
        recs[rec] = data
        i+=(size+2)
    return {
        'version': version,
        'length': length,
        'crc': crc,
        'flags': flags,
        'tor': tor,
        'recs': recs
    }

async def main():
    parser = argparse.ArgumentParser(description='Read OpenStint decoder and stream results as an AMB/P3 decoder')
    parser.add_argument('--host', help='OpenStint host', default='127.0.0.1')
    parser.add_argument('--port', help='OpenStint port', default='5556')
    parser.add_argument('--listen', help='P3 protocol listen port', default='5403')
    parser.add_argument('--id', help='Decoder ID (max 4 ascii characters)', default='OPNS')
    args = parser.parse_args()

    bridge = Bridge(args.id)
    bridge.version_response()

    # TCP server for AMB/P3
    server = await asyncio.start_server(bridge.read_loop, "0.0.0.0", args.listen)
    print(f"[bridge] listening as P3 on port {args.listen}")

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
