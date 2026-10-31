"""Compile and run production core/LCD/I2S code against deterministic host fakes.

Use --compiler clang++, g++, or a Zig executable. ESP8266Audio must have been
installed with PlatformIO first. No attached device is accessed.
"""
from pathlib import Path
import argparse
import shutil
import subprocess

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler',default=shutil.which('clang++') or shutil.which('g++'))
args=parser.parse_args()
if not args.compiler:
    parser.error('Provide a C++17 compiler via --compiler')
compiler=Path(args.compiler)
command=[str(compiler)]
if compiler.stem=='zig': command.append('c++')
out=root/'logs/host-tests.exe'
out.parent.mkdir(exist_ok=True)
command+=['-std=c++17','-O1','-g','-Wall','-Wextra',
    '-I'+str(root/'tests/stubs'),'-I'+str(root/'include'),'-I'+str(root/'src'),
    '-I'+str(root/'.pio/libdeps/waveshare-speaker-test/ESP8266Audio/src'),
    str(root/'tests/test_core.cpp'),str(root/'src/BatchedI2S.cpp'),
    str(root/'src/vendor/Display_ST7789.cpp'),'-o',str(out)]
subprocess.run(command,check=True,cwd=root)
result=subprocess.run([str(out)],check=True,cwd=root,capture_output=True,text=True)
print(result.stdout,end='')
(root/'logs/host-tests.txt').write_text(result.stdout,encoding='utf-8')
