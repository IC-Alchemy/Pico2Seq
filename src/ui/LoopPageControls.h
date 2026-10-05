#pragma once

#include <cstdint>

// LoopPageControls.h — gesture policy of the loop button and the live Loop Settings page
// (Core 0). Pure edge logic, no hardware calls, so the host tests drive it directly; the
// instance lives in UIState like the Reverb page's ReverbPage::Controls and follows the
// same rules: the whole gesture, releases included, is consumed so nothing leaks into
// normal actions.
//
// The loop button is its own pin (GP6, to ground, pull-up): Button debounces it and sorts
// each press into one of
//   Tap         released inside the hold time -> arm / cancel a take
//   Hold        still down after kHoldMs       -> clear the loop (fires once, mid-hold)
//   ChordPress  Shift was held at the press    -> open the Loop Settings page
//
// Page: hold Shift (button 8) and press the loop button. Faders 1-4 are loop volume, loop
// length, sequencer volume and regen; the loop button keeps its tap/hold meaning on the
// page so a take can be started while the levels are set. Shift leaves. Voice buttons,
// step pads, tile buttons and the encoder do nothing while the page is open.
namespace LoopPage {

class Button {
public:
    enum class Event : uint8_t { None, Tap, Hold, ChordPress };

    static constexpr uint32_t kDebounceMs = 20;
    static constexpr uint32_t kHoldMs = 800; // longer than the 400 ms tap/hold split: clearing is destructive

    // Seed from the boot-time level so a button held through reset fires nothing.
    void begin(bool pressed, uint32_t nowMs) noexcept {
        stable_ = candidate_ = pressed;
        candidateSince_ = nowMs;
        chord_ = holdFired_ = true; // swallow the release of a boot-time press
    }

    // One pass. `rawPressed` is the pin reading already inverted for the pull-up (true =
    // down); `shiftHeld` is the Shift level at this pass, latched at the press edge.
    Event update(bool rawPressed, uint32_t nowMs, bool shiftHeld) noexcept {
        if (rawPressed != candidate_) {
            candidate_ = rawPressed;
            candidateSince_ = nowMs;
        }
        if (candidate_ != stable_ && nowMs - candidateSince_ >= kDebounceMs) {
            stable_ = candidate_;
            if (stable_) {
                pressedAt_ = nowMs;
                chord_ = shiftHeld;
                holdFired_ = false;
                return chord_ ? Event::ChordPress : Event::None;
            }
            // Released: a plain press that neither chorded nor held is a tap.
            return (!chord_ && !holdFired_) ? Event::Tap : Event::None;
        }
        if (stable_ && !chord_ && !holdFired_ && nowMs - pressedAt_ >= kHoldMs) {
            holdFired_ = true;
            return Event::Hold;
        }
        return Event::None;
    }

    bool held() const noexcept { return stable_; }

private:
    bool stable_ = false;
    bool candidate_ = false;
    bool chord_ = false;
    bool holdFired_ = false;
    uint32_t candidateSince_ = 0;
    uint32_t pressedAt_ = 0;
};

struct Input {
    bool consumed = false; // this pass belongs to the page (or its release tail)
    bool open = false;     // the chord just opened the page
    bool exit = false;     // Shift closed it
};

struct Controls {
    static constexpr uint8_t kShift = 1u << 7;

    bool active = false;
    bool waitRelease = false; // every button up before normal input resumes
    // Fader the OLED highlights: the last one moved, as ControlSurface::LoopControl (0..3) or 255 = none.
    uint8_t lastControl = 255;
    uint8_t previousButtons = 0;
    uint8_t previousVoices = 0;

    // Track levels without acting on them, for passes another modal screen owns, so a
    // button held across that screen is not read as a fresh press afterwards.
    void observe(uint8_t buttons, uint8_t voices) noexcept {
        previousButtons = buttons;
        previousVoices = voices;
    }

    // One pass of tile levels plus the loop button. `chordPress` is the loop button's
    // ChordPress event this pass; `loopHeld` its debounced level. `canOpen` is false while
    // another page owns the panel so the gesture cannot open two screens.
    Input poll(uint8_t buttons, uint8_t voices, bool loopHeld, bool chordPress, bool canOpen) noexcept {
        const uint8_t pressed = buttons & ~previousButtons;
        previousButtons = buttons;
        previousVoices = voices;
        Input out;
        if (waitRelease) {
            out.consumed = true;
            if (buttons == 0 && voices == 0 && !loopHeld) waitRelease = false;
            return out;
        }
        if (active) {
            out.consumed = true;
            if (pressed & kShift) {
                active = false;
                waitRelease = true;
                out.exit = true;
            }
            return out;
        }
        if (canOpen && chordPress) {
            active = true;
            waitRelease = true; // the fingers that opened it must lift first
            lastControl = 255;
            out.consumed = true;
            out.open = true;
        }
        return out;
    }
};
} // namespace LoopPage
