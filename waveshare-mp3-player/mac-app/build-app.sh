#!/bin/bash
# Builds "Send to MP3 Player.app" into ./build. Needs Xcode or the Xcode Command Line Tools.
set -euo pipefail
cd "$(dirname "$0")"

APP="build/Send to MP3 Player.app"

if swift build -c release --arch arm64 --arch x86_64 2>/dev/null; then
    BIN_DIR="$(swift build -c release --arch arm64 --arch x86_64 --show-bin-path)"
else
    echo "Universal build unavailable (needs full Xcode); building for this Mac only."
    swift build -c release
    BIN_DIR="$(swift build -c release --show-bin-path)"
fi

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN_DIR/SendToMP3" "$APP/Contents/MacOS/SendToMP3"
cp Info.plist "$APP/Contents/Info.plist"

# Ad-hoc signature: runs on this Mac. On another Mac, right-click > Open the first time
# (or System Settings > Privacy & Security > Open Anyway).
codesign --force --sign - "$APP"

echo "Built: $APP"
