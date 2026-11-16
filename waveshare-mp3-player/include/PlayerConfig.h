#pragma once
#include <stdint.h>

// Build variants (set in platformio.ini):
//   PLAYER_EARBUDS=1     stereo output + capped earbud gain (headphone conversion)
//   PLAYER_USB_DRIVE=1   SD card can be exposed as a USB drive (needs ARDUINO_USB_MODE=0)
#ifndef PLAYER_EARBUDS
#define PLAYER_EARBUDS 0
#endif
#ifndef PLAYER_USB_DRIVE
#define PLAYER_USB_DRIVE 0
#endif

namespace player {
constexpr uint8_t kBrightness = 50;
constexpr uint8_t kDimBrightness = 12;
constexpr uint32_t kDimAfterMs = 15000;
constexpr uint32_t kScreenOffAfterMs = 30000;
constexpr uint32_t kTouchFallbackMs = 250;
constexpr uint32_t kUiUpdateMs = 200;
constexpr uint32_t kStatePublishMs = 200;
constexpr uint32_t kAudioReadAhead = 16 * 1024;
constexpr uint32_t kPcmBatchFrames = 256;
constexpr uint32_t kPcmSliceFrames = 512;
constexpr uint32_t kDmaFrames = 256;
constexpr uint32_t kDmaBuffers = 8;
constexpr uint32_t kI2sWaitMs = 8;
constexpr unsigned kMaxTracks = 128;
constexpr unsigned kMaxPath = 256;
constexpr unsigned kMaxScanDepth = 4;
constexpr unsigned kMaxScanEntries = 4096;
constexpr uint32_t kMaxScanMs = 2000;

// ---- Audio output -------------------------------------------------------------
// Digital gain in 1/64 steps, applied before the DAC; the analog knob comes after.
constexpr uint8_t kSpeakerGainQ6 = 5;  // Same quantized gain as the original SetGain(0.08f).
// Earbuds: the amp can reach ~117 dB SPL in 16-ohm earbuds uncapped (2026-11-03 audit).
// START LOW and raise only after a listening test with the knob at its lowest.
constexpr uint8_t kEarbudGainQ6 = 2;
constexpr uint8_t kEarbudMaxGainQ6 = 5;  // hard ceiling, enforced at compile time
constexpr bool kMonoOutput = !PLAYER_EARBUDS;  // stock speaker is mono; earbuds get stereo
constexpr uint8_t kGainQ6 = PLAYER_EARBUDS ? kEarbudGainQ6 : kSpeakerGainQ6;
static_assert(!PLAYER_EARBUDS || kGainQ6 <= kEarbudMaxGainQ6, "earbud gain above the safety cap");

// ---- Physical buttons (J8 pin 9 = GPIO43, pin 10 = GPIO44; to GND, internal pull-up) ----
// These are UART0 TX/RX. The ROM bootloader prints on GPIO43 at reset, so wire the
// button through a ~1 k series resistor. Set a pin to -1 to disable that button.
constexpr int kButtonAPin = 43;
constexpr int kButtonBPin = 44;
enum class ButtonAction : uint8_t { None, PlayPause, Next, Previous, Restart, WakeScreen };
// Mapping is a placeholder until it is decided; change only these six lines.
constexpr ButtonAction kButtonASingle = ButtonAction::PlayPause;
constexpr ButtonAction kButtonADouble = ButtonAction::None;
constexpr ButtonAction kButtonALong = ButtonAction::None;
constexpr ButtonAction kButtonBSingle = ButtonAction::Next;
constexpr ButtonAction kButtonBDouble = ButtonAction::Previous;
constexpr ButtonAction kButtonBLong = ButtonAction::None;
constexpr uint32_t kButtonDebounceMs = 25;
constexpr uint32_t kButtonDoubleGapMs = 350;
constexpr uint32_t kButtonLongMs = 700;

// ---- Add music over Wi-Fi --------------------------------------------------------
// The Mac app (mac-app/Sources/SendToMP3/PlayerConfig.swift) must use the same values.
constexpr char kWifiName[] = "Addy's MP3-Player";
constexpr char kWifiPassword[] = "whyisadityasupercool";
constexpr uint32_t kUploadIdleTimeoutMs = 5UL * 60UL * 1000UL;
constexpr char kMusicFolder[] = "/Music";
constexpr char kDriveMarkerFile[] = "/.addys-mp3-player";
constexpr uint32_t kUploadWriteBuffer = 32 * 1024;
constexpr uint64_t kUploadSpareBytes = 1024 * 1024;  // keep 1 MB free on the card
}
