# Add music, buttons, earbuds (feature branch)

**Status:** builds for all three environments and passes the host tests. **Not flashed or
tested on hardware.** This branch builds on the (also unflashed) optimization branch;
see [OPTIMIZATION.md](OPTIMIZATION.md). Known-good hardware baseline: `8f03337`.

New **Add** tab (tabs are now Player / Files / Add / Info):

## Wi-Fi upload (iPhone or Mac)

```
Add → Wi-Fi ─► player becomes Wi-Fi "Addy's MP3-Player" (pw whyisadityasupercool)
              screen: QR 1 = join Wi-Fi   ──(phone joins)──►   QR 2 = http://192.168.4.1/
              Safari page: Choose MP3s → per-song progress → "Done – turn off Wi-Fi"
```

- Music pauses while Wi-Fi is on (Wi-Fi and audio share CPU core 0).
- Wi-Fi turns itself off after **5 min with no requests** (not counted while a song is
  arriving), when the page/Mac app presses Done, or on the player's Done button.
- Uploads are raw `PUT /api/upload` bodies (name in `X-File-Name`, URL-encoded), streamed
  through a 32 KB buffer to `/Music/.upload-*.tmp`, size-checked, then renamed. Existing
  files are never overwritten: same name + same size = "already on the player",
  different size = `Name (2).mp3`. Leftover temp files are deleted next time.
- Names are made FAT-safe identically on the player (`sanitizeUploadName`) and in the
  Mac app (`PlayerName.forSong`): printable ASCII, no `\/:*?"<>|`, max 100 chars.
- Endpoints: `GET /`, `GET /api/status`, `GET /api/files`, `PUT /api/upload`,
  `POST /api/done`. Anything else is 404 (unexpected bodies are discarded, not buffered).
- No captive-portal DNS on purpose: iOS's captive mini-browser handles file pickers
  badly. The phone will say "No Internet Connection"; that's expected.

## USB drive (Mac/PC)

```
Add → USB drive ─► audio closes its file ─► card exported as "Addy MP3 Player" drive
                   computer copies into /Music ─► Eject on computer ─► player restarts
```

- The board's USB port normally is the USB-Serial/JTAG port (flashing + serial). Drive
  mode starts TinyUSB, which takes the port over, so **the COM port disappears** until
  the player restarts. Flashing and `inspect_player.py` are unchanged otherwise.
- Exit is always a restart (eject, Cancel, or 5 min without a computer). Before the
  restart the USB PHY is handed back to USB-Serial/JTAG (that setting survives a
  software reset). If the COM port ever doesn't come back, unplug/replug or hold
  BOOT + tap RESET.
- Cancel while a computer has the drive open asks for a second tap ("eject first").
- Writes `/.addys-mp3-player` so the Mac app can recognise the card.
- Dot-files (`._Song.mp3` from macOS, temp files) are now skipped by the scanner and
  the file browser.
- Build without it: remove `-DPLAYER_USB_DRIVE=1` in `platformio.ini`.

## Physical buttons

GPIO43 (J8 pin 9) and GPIO44 (J8 pin 10), each to GND, internal pull-ups. These are UART0
TX/RX: the boot ROM prints on GPIO43, so use a ~1 kΩ series resistor.

The mapping is a placeholder until it's decided. Change only the six lines in
`include/PlayerConfig.h`:

| | Single | Double | Long (0.7 s) |
| --- | --- | --- | --- |
| Button A (GPIO43) | Play/pause | — | — |
| Button B (GPIO44) | Next | Previous | — |

Actions available: `PlayPause`, `Next`, `Previous`, `Restart`, `WakeScreen`, `None`.
A button with no double/long action reacts on release; otherwise a single click waits
out the 350 ms double-click window. Media buttons work with the screen off and don't
wake it. Debounce 25 ms. Ignored while Wi-Fi/USB mode is open.

## Earbuds build

`platformio.exe run -e earbuds`: stereo output (speaker build stays mono) and digital
gain `kEarbudGainQ6 = 2` (vs 5 for the speaker), with a compile-time ceiling of 5.
ESP8266Audio's `SetGain` (which wraps 4.0 to 0) is overridden to clamp too. **Start with
the knob at its lowest; the cap value needs a real listening/dummy-load check.**

## Serial commands added

`wifi-on`, `wifi-off`, `usb-drive`, `rescan`, `add`, `info` (`storage` still works);
`status` now reports `transfer=0/1/2`.

## Build results (2026-11-16, compile only)

| Env | Flash (of 3 MB app) | Static RAM |
| --- | --- | --- |
| waveshare-speaker-test | 1,306,961 B (41.5%) | 220,936 B (67.4%) |
| earbuds | 1,306,977 B | 220,936 B |
| battery-candidate (160 MHz) | 1,306,961 B | 220,936 B |

Host tests: all previous tests plus upload-name rules, Wi-Fi QR escaping, JSON
escaping, 5-min timeout (incl. `millis()` wrap) and button click/double/long/bounce.

## Needs hardware checks

1. Free heap with Wi-Fi on (`status`), QR readability on the 2.8" LCD, iPhone join + upload,
   upload speed, phone-lock interruptions, the 5-min timeout.
2. USB drive: Finder/Explorer mount, copy, eject → restart, COM port comes back.
3. Buttons: bounce, double-click feel, booting with a button held.
4. Earbud loudness at the cap; left/right order.
5. The Mac app: [mac-app/README.md](mac-app/README.md) (written on Windows, never compiled).
