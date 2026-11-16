#pragma once
// Host-testable click detector for one active-low push button.
#include <stdint.h>

namespace player {

enum class ButtonEvent : uint8_t { None, Single, Double, Long };

struct ButtonTiming {
    uint32_t debounceMs;
    uint32_t doubleGapMs;  // max release->press gap that still counts as a double click
    uint32_t longMs;       // hold this long for a long press
};

// Feed it the debounced-or-not raw level as often as you like (>= every ~10 ms while
// pending() is true). Single clicks are reported only after the double-click window
// closes, so a button with no double/long mapping can use reportSingleImmediately.
class ButtonGesture {
public:
    explicit ButtonGesture(ButtonTiming timing, bool reportSingleImmediately = false)
        : t_(timing), immediate_(reportSingleImmediately) {}

    ButtonEvent update(bool pressedRaw, uint32_t now) {
        // Debounce: accept a level only after it is stable for debounceMs.
        if (pressedRaw != raw_) { raw_ = pressedRaw; rawSince_ = now; }
        if (raw_ != stable_ && uint32_t(now - rawSince_) >= t_.debounceMs) {
            stable_ = raw_;
            return stable_ ? onPress(now) : onRelease(now);
        }
        if (stable_ && !longFired_ && uint32_t(now - pressedAt_) >= t_.longMs) {
            longFired_ = true;
            clicks_ = 0;
            return ButtonEvent::Long;
        }
        if (!stable_ && clicks_ == 1 && uint32_t(now - releasedAt_) > t_.doubleGapMs) {
            clicks_ = 0;
            return ButtonEvent::Single;
        }
        return ButtonEvent::None;
    }

    // True while a decision is still waiting on time passing.
    bool pending() const { return raw_ != stable_ || stable_ || clicks_ != 0; }

private:
    ButtonEvent onPress(uint32_t now) {
        pressedAt_ = now;
        longFired_ = false;
        return ButtonEvent::None;
    }
    ButtonEvent onRelease(uint32_t now) {
        releasedAt_ = now;
        if (longFired_) { longFired_ = false; return ButtonEvent::None; }
        if (immediate_) return ButtonEvent::Single;
        if (++clicks_ >= 2) { clicks_ = 0; return ButtonEvent::Double; }
        return ButtonEvent::None;
    }

    ButtonTiming t_;
    bool immediate_;
    bool raw_ = false, stable_ = false, longFired_ = false;
    uint32_t rawSince_ = 0, pressedAt_ = 0, releasedAt_ = 0;
    uint8_t clicks_ = 0;
};

}  // namespace player
