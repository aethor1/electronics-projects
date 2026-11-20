# Local optimization results

Scope: preserve the stock USB-powered speaker path and the known-good LCD
pixel order, reduce avoidable work, and prepare a candidate for hardware tests.
The board was disconnected. No device was flashed and nothing was pushed.

## Changes

| Area | Baseline | Local candidate |
| --- | --- | --- |
| PCM output | One driver write per sample frame | 256-frame batches, with correct handling of short writes |
| Audio controls | Polling every 1 ms while playing / 5 ms while idle | Bounded 512-frame decode slices; block on commands while paused/stopped |
| I2S lifecycle | Driver allocation on track changes; clocks continue during pause | Reuse driver; stop clocks on pause/EOF; restart on resume |
| Decoder memory | Repeated large heap allocations per song | One reusable decoder workspace and 16 KiB read-ahead block |
| SD reads | Tag parsing can read one byte at a time | Seek over validated ID3 tags; refill in 16 KiB blocks |
| LCD transfers | Twelve SPI transactions per dirty rectangle | One transaction; validated command/data order and pixel bytes |
| UI | Reset all labels every 300 ms; GUI loop every 3 ms | Update changed text/position only, skip hidden player tab, wait on timers/interrupts |
| Touch | I2C polling every 15 ms | Interrupt-driven reads; 250 ms fallback; 15 ms while a finger is down |
| Screen | Backlight always 50% | 50% active, 12% after 15 s, LCD/backlight sleep after 30 s |
| Wake | No idle-screen handling | First gesture wakes only; a second gesture can activate controls |
| Screenshot | Permanent 153,600-byte PSRAM buffer and copies on every flush | Temporary allocation/copy only during capture |
| Startup | Fixed 1.5 s USB wait, hash/copy checks on every boot | UI before card work; no USB wait; existing SD tracks bypass import/hash work |
| Directory scans | Unbounded traversal time | Prioritize `/Music`; bound traversal to 2 s / 4,096 entries / 128 tracks |
| Diagnostics | Periodic USB logging | Explicit `status` / `metrics` requests |

The scan time limit is checked between filesystem calls, not a hard deadline
for a stalled card. File browsing can still open tracks outside the initial
playlist. Imports retain SHA-256 verification, use temporary files, preserve
existing content, and never format a card on mount failure.

The decoder workspace now appears in static RAM rather than being allocated
when a song starts. This increases the static-RAM figure; it is a deliberate
trade for predictable allocation, not a claim that every memory metric shrank.
The original 96 KiB LVGL heap and safe 20 MHz display clock are retained.

## Verification performed without the board

The host tests compile **production** `PlayerCore.h`, `BatchedI2S.cpp`, and the
LCD driver against deterministic I/O fakes. They check:

- 300,000 PCM cases against the original gain, mono mixing, saturation, and
  stereo word packing; no intended audio-quality change.
- 200,003 frames under random short writes and backpressure, compared byte for
  byte, including slice boundaries and partial tails.
- 12,000 randomized read/seek cases, EOF bounds, and logical file positions.
- ID3v2.2/2.3/2.4 sizes, footer handling, truncation, and invalid headers.
- Driver reuse, pause/resume clock calls, and injected I2S write errors.
- Actual LCD address-window bytes, one transaction per rectangle, retained
  RAMCTRL `0xE0`, invalid rectangles, PWM endpoints, and wake delay.
- Screen deadlines, millis rollover, and suppression of a wake gesture.

Measured in those host tests:

| Workload | Baseline calls | Candidate calls |
| --- | ---: | ---: |
| 44,100 PCM frames, full-write fake sink | 44,100 | 173 I2S writes |
| One dirty LCD rectangle | 12 | 1 SPI transaction |
| 100,003 one-byte reads through the new reader | 100,003 without buffering | 7 source reads |

These are call-count and byte-correctness results. They do **not** measure mA,
battery hours, actual SD latency, codec CPU load, speaker sound, or physical
touch/LCD behavior. Full hardware audio playback is still required.

Run the host tests after installing the PlatformIO dependency:

```powershell
python tests/run_host_tests.py --compiler path/to/clang++.exe
# g++, or a Zig executable, can also be supplied.
```

Build the regular 240 MHz candidate:

```powershell
pio run -e waveshare-speaker-test
```

An optional **160 MHz** candidate is separate and must be compared on hardware
before choosing it for a battery build:

```powershell
pio run -e battery-candidate
```

The installed Arduino SDK has `CONFIG_PM_ENABLE` disabled. This change does not
claim automatic CPU light sleep. RTOS waits allow idle time; LCD sleep and
paused I2S clocks are separate operations. Wi-Fi/Bluetooth are not initialized.

## Next hardware checks before pushing

1. Keep the known-good `8f03337` firmware/backup available. Use the USB-only,
   unmodified speaker setup; battery/headphone circuitry remains unvalidated.
2. Flash the 240 MHz build first. Check playback from SD, pause/resume, repeated
   next/previous, end of song, missing files/card, and file-browser navigation.
3. Confirm dimming at 15 seconds and sleep at 30 seconds. Confirm that the first
   touch wakes without skipping/pausing, and the next touch works normally.
4. Check `metrics` before/after intervals for file progress, I2S write count,
   read refills, heap stability, and zero LCD redraws while the screen is off.
   Check `shot` still captures a complete image and releases its memory.
5. Compare the 160 MHz candidate only if the regular build is stable. Test
   44.1/48 kHz tracks and sustained card browsing; listen for gaps or distortion.
6. Measure current at the battery-side supply under matched conditions: playing
   with screen active/off, paused with screen active/off, and power removed.
   Compare against the baseline with the same volume, files, and hardware.
   Battery runtime remains unknown until that measurement and a discharge test.

References:
- [ESP-IDF power management](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32s3/api-reference/system/power_management.html)
- [ESP-IDF I2S API](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32s3/api-reference/peripherals/i2s.html)
- [ID3v2.4 structure](https://id3.org/id3v2.4.0-structure)

## Final local build result

Both the regular 240 MHz and optional 160 MHz environments pass the final
PlatformIO build. Static RAM is 182,452 bytes versus the baseline 140,548 bytes
(the reusable audio buffers account for the increase). Host tests pass.
Full build output and machine-readable evidence remain locally in `logs/`.
