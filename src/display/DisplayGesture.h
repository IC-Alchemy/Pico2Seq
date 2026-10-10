#pragma once

#include <cstdint>

namespace DisplayGesture {
enum class Action : uint8_t { None, Swing, Page, Style };

// Utility Swing: tap still changes swing; a hold only changes the display.
// Shift is captured on press, never reinterpreted halfway through the gesture.
struct SwingHold {
    bool armed = false;
    bool spent = false;
    bool shifted = false;
    uint32_t pressedAt = 0;

    void cancel() noexcept { armed = spent = false; }
    Action update(bool pressed, bool released, bool held, bool shift,
                  uint32_t now, uint32_t threshold) noexcept {
        if (pressed) {
            armed = true;
            spent = false;
            shifted = shift;
            pressedAt = now;
        }
        if (!armed) return Action::None;
        if (!spent && now - pressedAt >= threshold) {
            spent = true;
            if (released || !held) armed = false;
            return shifted ? Action::Style : Action::Page;
        }
        if (released || !held) {
            const bool tap = !spent;
            cancel();
            return tap ? Action::Swing : Action::None;
        }
        return Action::None;
    }
};
}
