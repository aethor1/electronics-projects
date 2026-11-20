"""Build the firmware's UI (src/PlayerUi.cpp + LVGL) for the PC and save screenshots.

    python tools/ui_preview/run.py [--out docs/ui]

Needs gcc/g++ and the LVGL copy restored by prepare_vendor.py. Writes one PNG per screen
at 2x (pixel-doubled, so 1 LCD pixel = 2x2) plus overview.png with all of them.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import struct
import subprocess
import zlib

root = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, default=root / 'docs/ui')
args = parser.parse_args()

build = root / 'logs/ui_preview'
objects = build / 'obj'
objects.mkdir(parents=True, exist_ok=True)
lvgl = root / 'lib/lvgl'
if not (lvgl / 'lvgl.h').exists():
    raise SystemExit('LVGL missing: run python prepare_vendor.py first')
# 32-bit like the ESP32, so LVGL heap use matches the board (needs g++-multilib on Linux).
probe = subprocess.run(['gcc', '-m32', '-x', 'c', '-', '-o', str(build / 'probe')], input='int main(){return 0;}',
                       text=True, capture_output=True)
bits = ['-m32'] if probe.returncode == 0 else []
if not bits:
    print('note: no -m32 toolchain; 64-bit build overstates LVGL heap use about 2x')
flags = [*bits, '-O1', '-DLV_CONF_INCLUDE_SIMPLE', f'-I{here / "stubs"}', f'-I{root / "include"}',
         f'-I{root / "src"}', f'-I{lvgl}']
config = root / 'include/lv_conf.h'


def compile_c(source):
    out = objects / (str(source.relative_to(lvgl)).replace('/', '_') + '.o')
    if not out.exists() or out.stat().st_mtime < max(source.stat().st_mtime, config.stat().st_mtime):
        subprocess.run(['gcc', '-c', '-w', *flags, str(source), '-o', str(out)], check=True)
    return out


with ThreadPoolExecutor() as pool:
    lib = list(pool.map(compile_c, sorted((lvgl / 'src').rglob('*.c'))))
binary = build / 'preview'
subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', *flags, str(here / 'preview.cpp'),
                str(root / 'src/PlayerUi.cpp'), *map(str, lib), '-o', str(binary)], check=True)

args.out.mkdir(parents=True, exist_ok=True)
raw = build / 'frames'
raw.mkdir(exist_ok=True)
print(subprocess.run([str(binary), str(raw)], check=True, capture_output=True, text=True).stdout, end='')


def write_png(path, width, height, rows):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    body = b''.join(b'\0' + row for row in rows)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', width, height, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(body, 9)) + chunk(b'IEND', b''))


def doubled(frame):
    rows = []
    for y in range(320):
        line = frame[y * 720:(y + 1) * 720]
        wide = b''.join(line[x:x + 3] * 2 for x in range(0, 720, 3))
        rows += [wide, wide]
    return rows


frames = sorted(raw.glob('*.rgb'))
gap, pad, background = 24, 24, bytes((0x2A, 0x2E, 0x2C))
sheet_w = pad * 2 + len(frames) * 480 + (len(frames) - 1) * gap
sheet = [bytearray(background * sheet_w) for _ in range(640 + pad * 2)]
for i, path in enumerate(frames):
    rows = doubled(path.read_bytes())
    write_png(args.out / (path.stem + '.png'), 480, 640, rows)
    x = (pad + i * (480 + gap)) * 3
    for y, row in enumerate(rows):
        sheet[pad + y][x:x + len(row)] = row
write_png(args.out / 'overview.png', sheet_w, len(sheet), [bytes(r) for r in sheet])
print(f'{len(frames)} screens -> {args.out}')
