# Send to MP3 Player (Mac app)

Small SwiftUI app for the recipient's Mac: paste a YouTube link → 128 kb/s MP3 saved in
`~/Music/Addy's MP3 Player` → **Send it to MP3 player**.

> **Status: written on Windows, never compiled or run.** Expect a few compile fixes on the
> first build on a Mac.

## Build (on a Mac)

Needs Xcode or the Command Line Tools (`xcode-select --install`), macOS 13+.

```bash
cd mac-app
chmod +x build-app.sh
./build-app.sh
open build
```

Drag `Send to MP3 Player.app` to Applications. It's ad-hoc signed (no $99 developer
account), so on another Mac: right-click → **Open** the first time, or System Settings →
Privacy & Security → **Open Anyway**.

## How it works

| Step | What happens |
| --- | --- |
| Get song | Runs `yt-dlp -x --audio-format mp3 --audio-quality 128K` into the library folder. Uses the yt-dlp/ffmpeg MacYTDL already installed in `/usr/local/bin` (or Homebrew's); otherwise downloads private copies (plus `deno`, which yt-dlp needs for YouTube) to `~/Library/Application Support/SendToMP3/bin`. Menu → **Update Song Downloader** runs `yt-dlp -U` when YouTube breaks it. |
| Send (USB) | Looks for a mounted volume with `/.addys-mp3-player` (the player writes it in USB drive mode). Copies missing songs to `/Music` as plain bytes (no `._` files), then ejects. |
| Send (Wi-Fi) | If no USB player: checks `http://192.168.4.1/api/status`, tries to join `Addy's MP3-Player` (CoreWLAN, best effort), uploads missing songs, then `POST /api/done` so the player turns Wi-Fi off. |

Only songs not already on the player are sent. File names are made FAT-safe with the
same rule as the firmware (`PlayerName.forSong` ↔ `sanitizeUploadName`), so the
"already there" check matches.

## Unverified bits

- The ffmpeg fallback URL (`ffmpeg.martin-riedl.de`) and deno URL were not checked from the
  Windows dev box. If they fail, opening MacYTDL once installs yt-dlp + ffmpeg.
- Auto-joining Wi-Fi may need Location permission on recent macOS; if it fails the app
  tells the user to join the network from the Wi-Fi menu.
