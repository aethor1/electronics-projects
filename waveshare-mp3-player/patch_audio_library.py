"""Disable the unused I2S MCLK so GPIO0 stays available for the BOOT button."""
from pathlib import Path

source = Path(__file__).parent / '.pio/libdeps/waveshare-speaker-test/ESP8266Audio/src/AudioOutputI2S.cpp'
text = source.read_text(encoding='utf-8')
before = '.mck_io_num = 0, // Unused'
after = '.mck_io_num = I2S_PIN_NO_CHANGE, // PCM5101 uses BCLK; leave BOOT GPIO0 alone'
if before in text:
    assert text.count(before) == 1
    source.write_text(text.replace(before, after), encoding='utf-8')
    print('Patched local ESP8266Audio dependency: unused MCLK disabled.')
elif after in text:
    print('Local ESP8266Audio dependency already patched.')
else:
    raise RuntimeError('Unexpected AudioOutputI2S source; inspect before building.')
