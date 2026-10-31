"""Read a bounded sample of the speaker-test diagnostic output."""
import argparse
from pathlib import Path
import time
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM15')
parser.add_argument('--seconds', type=float, default=15)
args = parser.parse_args()
port = serial.Serial()
port.port = args.port
port.baudrate = 115200
port.timeout = 0.5
port.dtr = False
port.rts = False
port.open()
chunks = []
try:
    until = time.monotonic() + args.seconds
    while time.monotonic() < until:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            chunks.append(chunk)
finally:
    port.close()
decoded = b''.join(chunks).decode('utf-8', errors='replace')
logs = Path(__file__).parent / 'logs'
logs.mkdir(exist_ok=True)
(logs / 'latest-serial.txt').write_text(decoded, encoding='utf-8')
print(decoded if decoded else '(No serial text captured.)')
