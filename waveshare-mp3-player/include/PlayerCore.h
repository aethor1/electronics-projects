#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

namespace player {
enum class ScreenMode : uint8_t { Awake, Dim, Off };
inline ScreenMode screenMode(uint32_t now, uint32_t activeAt, uint32_t dimMs, uint32_t offMs) {
    const uint32_t idle = now - activeAt; // Also valid across millis() rollover.
    return idle >= offMs ? ScreenMode::Off : idle >= dimMs ? ScreenMode::Dim : ScreenMode::Awake;
}
inline uint32_t backlightDuty(uint8_t percent) {
    return (uint32_t(percent > 100 ? 100 : percent) * 1023 + 50) / 100;
}
class WakeGesture {
public:
    bool filter(bool pressed, bool wasOff) {
        if (pressed && wasOff) suppress_ = true;
        if (!suppress_) return pressed;
        if (!pressed) suppress_ = false;
        return false; // The entire wake gesture is consumed, including release.
    }
private:
    bool suppress_ = false;
};
inline int16_t amplify(int32_t sample, uint8_t gainQ6) {
    const int32_t product = sample * gainQ6;
    const int32_t value = product >= 0 ? product / 64 : -((-product + 63) / 64);
    return int16_t(value < -32767 ? -32767 : value > 32767 ? 32767 : value);
}
inline uint32_t packPcm(int16_t left, int16_t right, uint8_t gainQ6, bool mono) {
    if (mono) {
        const int32_t sum = int32_t(left) + right;
        left = right = int16_t(sum >= 0 ? sum / 2 : -((-sum + 1) / 2));
    }
    return (uint32_t(uint16_t(amplify(right, gainQ6))) << 16) | uint16_t(amplify(left, gainQ6));
}

// A sample is accepted exactly once, even when the sink makes partial writes.
template<size_t Frames>
class PcmBatch {
public:
    template<class Writer> bool push(uint32_t sample, Writer write) {
        if (failed_ || (count_ == Frames && !drain(write))) return false;
        data_[count_++] = sample;
        return true;
    }
    template<class Writer> bool drain(Writer write) {
        if (failed_) return false;
        if (offset_ < count_ * sizeof(uint32_t)) {
            const size_t remaining = count_ * sizeof(uint32_t) - offset_;
            const size_t written = write(reinterpret_cast<const uint8_t*>(data_) + offset_, remaining);
            if (written > remaining || written % sizeof(uint32_t)) { failed_ = true; return false; }
            offset_ += written;
            if (offset_ != count_ * sizeof(uint32_t)) return false;
        }
        count_ = offset_ = 0;
        return true;
    }
    void reset() { count_ = offset_ = 0; failed_ = false; }
    bool failed() const { return failed_; }
    size_t pendingBytes() const { return count_ * sizeof(uint32_t) - offset_; }
private:
    alignas(4) uint32_t data_[Frames] = {};
    size_t count_ = 0, offset_ = 0;
    bool failed_ = false;
};

enum class TagResult : uint8_t { None, Valid, Invalid };
inline TagResult leadingTag(const uint8_t *header, size_t bytes, uint32_t fileBytes,
                            uint32_t at, uint32_t &next) {
    next = at;
    if (bytes < 3 || memcmp(header, "ID3", 3)) return TagResult::None;
    if (bytes < 10 || header[3] < 2 || header[3] > 4 || header[4] == 255) return TagResult::Invalid;
    const uint8_t reserved = header[3] == 2 ? 0x3f : header[3] == 3 ? 0x1f : 0x0f;
    if (header[5] & reserved) return TagResult::Invalid;
    uint32_t tagBytes = 0;
    for (size_t i=6; i<10; ++i) {
        if (header[i] & 0x80) return TagResult::Invalid;
        tagBytes = (tagBytes << 7) | header[i];
    }
    const uint32_t footer = header[3] == 4 && (header[5] & 0x10) ? 10 : 0;
    const uint64_t end = uint64_t(at) + 10 + tagBytes + footer;
    if (end > fileBytes || end > INT32_MAX) return TagResult::Invalid;
    next = uint32_t(end);
    return TagResult::Valid;
}

// Logical positions track bytes delivered to the decoder, not the SD prefetch.
// Source must provide read/seek/getSize/getPos; no heap allocation is required.
template<class Source>
class ReadAhead {
public:
    ReadAhead(Source &source, uint8_t *storage, size_t capacity)
        : source_(source), storage_(storage), capacity_(capacity), position_(source.getPos()) {}
    uint32_t read(void *dest, uint32_t bytes) {
        auto *out = static_cast<uint8_t*>(dest);
        uint32_t copied = 0;
        while (bytes && capacity_ && storage_ && !failed_) {
            if (cursor_ == available_) {
                start_ = position_;
                available_ = source_.read(storage_, capacity_);
                ++reads_;
                cursor_ = 0;
                if (available_ > capacity_) { failed_ = true; break; }
                if (!available_) break;
            }
            const size_t n = bytes < available_-cursor_ ? bytes : available_-cursor_;
            memcpy(out+copied, storage_+cursor_, n);
            cursor_ += n; position_ += n; copied += n; bytes -= n;
        }
        return copied;
    }
    bool seek(int32_t offset, int origin) {
        const int64_t base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? position_ : origin == SEEK_END ? source_.getSize() : -1;
        const int64_t target = base + int64_t(offset);
        if (base < 0 || target < 0 || target > source_.getSize() || target > INT32_MAX) return false;
        if (available_ && target >= start_ && uint64_t(target) <= uint64_t(start_)+available_) {
            cursor_ = size_t(target-start_); position_ = uint32_t(target); return true;
        }
        if (!source_.seek(int32_t(target), SEEK_SET)) return false;
        cursor_ = available_ = 0; position_ = start_ = uint32_t(target);
        failed_ = false;
        return true;
    }
    uint32_t position() const { return position_; }
    uint32_t reads() const { return reads_; }
    bool failed() const { return failed_; }
private:
    Source &source_;
    uint8_t *storage_;
    size_t capacity_, cursor_ = 0, available_ = 0;
    uint32_t position_, start_ = 0, reads_ = 0;
    bool failed_ = false;
};
}
