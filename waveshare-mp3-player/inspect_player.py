"""Bounded serial diagnostics for the SD/display test; does not reset or flash."""
import argparse
import pathlib
import struct
import time
import zlib
import serial


def png(path, frame):
    pixels = bytearray()
    for row in range(320):
        pixels.append(0)
        for col in range(240):
            value = struct.unpack_from('>H', frame, (row * 240 + col) * 2)[0]
            pixels.extend((((value >> 11) & 31) * 255 // 31,
                           ((value >> 5) & 63) * 255 // 63,
                           (value & 31) * 255 // 31))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' +
                     chunk(b'IHDR', struct.pack('>2I5B', 240, 320, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))


parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM15')
parser.add_argument('--command', action='append', default=[])
parser.add_argument('--seconds', type=float, default=8)
parser.add_argument('--screenshot', type=pathlib.Path)
parser.add_argument('--log', type=pathlib.Path, default=pathlib.Path('logs/player-serial.txt'))
args = parser.parse_args()
port = serial.Serial()
port.port, port.baudrate, port.timeout = args.port, 115200, 0.3
port.dtr = port.rts = False
port.open()
captured = bytearray()
try:
    for command in args.command:
        port.write((command + '\n').encode())
        end = time.monotonic() + 0.9
        while time.monotonic() < end:
            captured.extend(port.read(port.in_waiting or 1))
    if args.screenshot:
        port.write(b'shot\n')
        end = time.monotonic() + 15
        data = bytearray()
        marker = b'FRAME_BEGIN 240 320 RGB565BE\r\n'
        while time.monotonic() < end:
            data.extend(port.read(port.in_waiting or 1))
            offset = data.find(marker)
            if offset >= 0 and len(data) >= offset + len(marker) + 153600:
                captured.extend(data[:offset])
                start = offset + len(marker)
                args.screenshot.parent.mkdir(parents=True, exist_ok=True)
                png(args.screenshot, data[start:start+153600])
                captured.extend(data[start+153600:])
                print('SCREENSHOT', args.screenshot)
                break
        else:
            raise RuntimeError(f'Incomplete screen capture: {len(data)} bytes')
    end = time.monotonic() + args.seconds
    while time.monotonic() < end:
        captured.extend(port.read(port.in_waiting or 1))
finally:
    port.close()
    args.log.parent.mkdir(parents=True, exist_ok=True)
    args.log.write_bytes(captured)
    print(captured.decode('utf-8', errors='replace'))
