"""Restore the exact LVGL dependency used by the verified hardware build.

The 94 MB vendor archive and extracted library stay out of Git. A checksum
prevents a changed download from silently becoming part of this build.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import shutil
import urllib.request
import zipfile

URL = 'https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8/ESP32-S3-Touch-LCD-2.8-V2-Demo.zip'
SHA256 = 'b09e8cdfb47212ea94a06ba02a11568dfb70dcc1ea040142067d08e374b29737'
PREFIX = 'Arduino/libraries/lvgl/'
ROOT_FILES = {'lvgl.h', 'library.json', 'library.properties', 'LICENCE.txt'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, help='Use a previously downloaded vendor ZIP')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    archive = args.archive or root / 'vendor/waveshare-v2-demo.zip'
    if not archive.exists():
        if args.archive:
            raise FileNotFoundError(archive)
        archive.parent.mkdir(exist_ok=True)
        partial = archive.with_suffix('.download')
        print('Downloading the official Waveshare demo archive...')
        with urllib.request.urlopen(URL, timeout=60) as response, partial.open('wb') as out:
            shutil.copyfileobj(response, out)
        partial.replace(archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != SHA256:
        raise RuntimeError(f'Vendor archive checksum mismatch: {digest}; nothing extracted')
    target = root / 'lib/lvgl'
    existing_config = target / 'src/lv_conf.h'
    if existing_config.exists():
        raise RuntimeError('Remove the vendor src/lv_conf.h first; this build uses include/lv_conf.h')
    extracted = 0
    with zipfile.ZipFile(archive) as bundle:
        for member in bundle.infolist():
            if member.is_dir() or not member.filename.startswith(PREFIX):
                continue
            relative = PurePosixPath(member.filename[len(PREFIX):])
            if relative.is_absolute() or '..' in relative.parts:
                raise RuntimeError('Unsafe archive path')
            if str(relative) == 'src/lv_conf.h':
                continue
            if relative.parts[0] != 'src' and str(relative) not in ROOT_FILES:
                continue
            dest = target.joinpath(*relative.parts)
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(bundle.read(member))
            extracted += 1
    print(f'Verified archive SHA-256; restored {extracted} LVGL files to {target}')


if __name__ == '__main__':
    main()
