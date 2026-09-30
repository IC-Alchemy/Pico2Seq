#pragma once

#include <cstdint>

// TuningPageControls.h - gesture policy of the live Tuning page (Core 0).
// Pure edge logic, no hardware calls; the instance lives in UIState beside the Reverb
// page's ReverbPage::Controls and follows the same rules: the whole gesture, releases
// included, is consumed so nothing leaks into normal actions.
//
// Open: hold Shift (button 8) in Utility mode, then PRESS button 3. Button 3 alone keeps
// its short action (cycle the scale), so the chord is the only thing this page adds.
// There is exactly one page. On it:
//   - the 32 step pads hold the whole tuning library, one tuning per pad in library order,
//     lit in the colour of its family; touching a pad chooses that tuning;
//   - choosing a tuning also switches to a scale that belongs to it (tuning/TuningScales.h):
//     Dorian means nothing in 24 notes per octave, a maqam does;
//   - buttons 1-6 pick the first six scales of the tuning that is playing, button 7 swaps
//     between the current and previous tuning (A/B), Shift leaves;
//   - voice buttons 1-4 are the four hot favourites: tap recalls, hold stores the playing
//     tuning there (hold again to clear it);
//   - faders 1-3 set the tonic (Sa), the A4 reference and the scale, the encoder steps
//     through the tunings in library order.
// Everything the page decides is returned in Input; applying it (tuning::Selection, the
// bank, the voices) is TuningPageLogic.h and the bridge, so this file stays testable
// without a single global.
namespace TuningPage {

constexpr uint8_t kScaleKeys = 6; // buttons 1-6 choose scale slots 0-5
constexpr uint8_t kPadCount = 32;
constexpr uint8_t kHotSlots = 4; // voice buttons 1-4 == favourites 1-4
constexpr uint32_t kHoldMs = 600; // a press this long is a hold, not a tap
constexpr uint8_t kNoControl = 255;

// Which presses are down, when they went down, and which already fired as a hold. A press
// resolves exactly once: Tap on a short release, Hold at the threshold (or on a release
// that arrives late, before anything polled it), and nothing on the release that follows
// a fired hold.
template <uint8_t N> struct PressTracker {
    static_assert(N <= 32, "one bit per press");
    enum class Result : uint8_t { None, Tap, Hold };

    uint32_t downAt[N] = {};
    uint32_t down = 0;
    uint32_t fired = 0;

    void press(uint8_t index, uint32_t nowMs) noexcept {
        if (index >= N) return;
        downAt[index] = nowMs;
        down |= 1u << index;
        fired &= ~(1u << index);
    }

    Result release(uint8_t index, uint32_t nowMs) noexcept {
        if (index >= N || !(down & (1u << index))) return Result::None;
        down &= ~(1u << index);
        const bool alreadyFired = (fired & (1u << index)) != 0;
        fired &= ~(1u << index);
        if (alreadyFired) return Result::None;
        return (nowMs - downAt[index]) >= kHoldMs ? Result::Hold : Result::Tap;
    }

    // The lowest press that has just crossed the hold threshold, or -1. Call until it
    // returns -1 to drain several at once.
    int8_t pollHold(uint32_t nowMs) noexcept {
        for (uint8_t i = 0; i < N; ++i) {
            const uint32_t bit = 1u << i;
            if ((down & bit) && !(fired & bit) && (nowMs - downAt[i]) >= kHoldMs) {
                fired |= bit;
                return static_cast<int8_t>(i);
            }
        }
        return -1;
    }

    void clear() noexcept {
        down = 0;
        fired = 0;
    }
};

struct Input {
    bool consumed = false;     // this pass belongs to the page (or its release tail)
    bool open = false;         // the chord just opened the page
    bool exit = false;         // Shift closed it
    bool swap = false;         // button 7: A/B swap
    int8_t scaleSlot = -1;     // buttons 1-6: choose scale slot 0..5 of the playing tuning
    int8_t recallSlot = -1;    // voice button tap: recall hot favourite 0..3
    int8_t storeSlot = -1;     // voice button hold: store or clear hot favourite 0..3
};

struct Controls {
    static constexpr uint8_t kShift = 1u << 7;
    static constexpr uint8_t kOpenKey = 1u << 2; // button 3
    static constexpr uint8_t kSwapKey = 1u << 6; // button 7
    static constexpr uint8_t kScaleMask = 0x3F;  // buttons 1-6

    bool active = false;
    bool waitRelease = false; // every button up before normal input resumes
    // Fader the OLED highlights: the last one moved (TuningPage::Fader) or kNoControl.
    uint8_t lastControl = kNoControl;
    uint8_t previousButtons = 0;
    uint8_t previousVoices = 0;
    PressTracker<kHotSlots> voiceHolds;

    // Track levels without acting on them, for passes another modal screen owns, so a
    // button held across that screen is not read as a fresh press afterwards.
    void observe(uint8_t buttons, uint8_t voices) noexcept {
        previousButtons = buttons;
        previousVoices = voices;
    }

    // One pass of tile levels. `canOpen` is false while another page owns the chord (or
    // the strap is in Param mode).
    Input poll(uint8_t buttons, uint8_t voices, bool canOpen, uint32_t nowMs) noexcept {
        const uint8_t pressed = buttons & ~previousButtons;
        const uint8_t voicesDown = voices & ~previousVoices;
        const uint8_t voicesUp = previousVoices & ~voices;
        previousButtons = buttons;
        previousVoices = voices;
        Input out;
        if (waitRelease) {
            out.consumed = true;
            if (buttons == 0 && voices == 0) waitRelease = false;
            return out;
        }
        if (active) {
            out.consumed = true;
            if (pressed & kShift) {
                active = false;
                waitRelease = true;
                voiceHolds.clear();
                out.exit = true;
                return out;
            }
            const uint8_t scaleKey = pressed & kScaleMask;
            if (scaleKey) {
                uint8_t chosen = 0;
                while (!(scaleKey & (1u << chosen))) ++chosen; // lowest key wins
                out.scaleSlot = static_cast<int8_t>(chosen);
            }
            if (pressed & kSwapKey) out.swap = true;
            for (uint8_t v = 0; v < kHotSlots; ++v) {
                const uint8_t bit = static_cast<uint8_t>(1u << v);
                if (voicesDown & bit) voiceHolds.press(v, nowMs);
                if (voicesUp & bit) {
                    const auto result = voiceHolds.release(v, nowMs);
                    if (result == PressTracker<kHotSlots>::Result::Tap) out.recallSlot = static_cast<int8_t>(v);
                    else if (result == PressTracker<kHotSlots>::Result::Hold) out.storeSlot = static_cast<int8_t>(v);
                }
            }
            if (out.storeSlot < 0) {
                const int8_t held = voiceHolds.pollHold(nowMs);
                if (held >= 0) out.storeSlot = held;
            }
            return out;
        }
        if (canOpen && (pressed & kOpenKey) && (buttons & kShift)) {
            active = true;
            waitRelease = true; // the fingers that opened it must lift first
            lastControl = kNoControl;
            voiceHolds.clear();
            out.consumed = true;
            out.open = true;
        }
        return out;
    }
};

} // namespace TuningPage
