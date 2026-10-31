#pragma once
#include <stdint.h>

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
constexpr uint8_t kGainQ6 = 5; // Same quantized gain as the original SetGain(0.08f).
}
