#pragma once

#include <cstdint>

// ReverbPageControls.h — gesture policy of the live Reverb page (Core 0).
// Pure edge logic, no hardware calls; the instance lives in UIState like the ADSR
// page's VoiceEnvelope::Controls, and follows the same rules: the whole gesture,
// releases included, is consumed so nothing leaks into normal actions.
//
// Open: hold Shift (button 8), then button 6, then PRESS button 2. Shift + 6 alone
// keeps its short action (VoiceEnvelope defers it to release), so the third key is
// the only thing this page adds to that chord.
// On the page: faders 1-4 are the four settings of the current layer, button 1
// toggles Freeze, button 2 switches layer (MAIN <-> TONE), Shift leaves. Voice
// buttons, step pads and the encoder do nothing while it is open.
namespace ReverbPage {
struct Input {
    bool consumed = false;     // this pass belongs to the page (or its release tail)
    bool open = false;         // the chord just opened the page
    bool exit = false;         // Shift closed it
    bool toggleFreeze = false; // button 1 pressed
    bool toggleLayer = false;  // button 2 pressed (layer already flipped)
};

struct Controls {
    static constexpr uint8_t kShift = 1u << 7;
    static constexpr uint8_t kChordHeld = kShift | (1u << 5); // Shift + button 6
    static constexpr uint8_t kLayerKey = 1u << 1;             // button 2
    static constexpr uint8_t kFreezeKey = 1u << 0;            // button 1

    bool active = false;
    bool waitRelease = false; // every button up before normal input resumes
    uint8_t layer = 0;        // 0 = MAIN, 1 = TONE
    // Fader the OLED highlights: the last one moved, as ReverbControl (0..6) or 255 = none.
    uint8_t lastControl = 255;
    uint8_t previousButtons = 0;
    uint8_t previousVoices = 0;

    // Track levels without acting on them, for passes another modal screen owns, so
    // a button held across that screen is not read as a fresh press afterwards.
    void observe(uint8_t buttons, uint8_t voices) noexcept {
        previousButtons = buttons;
        previousVoices = voices;
    }

    // One pass of tile levels. `canOpen` is false while another page owns the
    // chord (the ADSR page) so the gesture cannot open two screens.
    Input poll(uint8_t buttons, uint8_t voices, bool canOpen) noexcept {
        const uint8_t pressed = buttons & ~previousButtons;
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
                out.exit = true;
                return out;
            }
            if (pressed & kFreezeKey) out.toggleFreeze = true;
            if (pressed & kLayerKey) {
                layer = static_cast<uint8_t>(layer ^ 1u);
                out.toggleLayer = true;
            }
            return out;
        }
        if (canOpen && (pressed & kLayerKey) && (buttons & kChordHeld) == kChordHeld) {
            active = true;
            waitRelease = true; // the fingers that opened it must lift first
            layer = 0;
            lastControl = 255;
            out.consumed = true;
            out.open = true;
        }
        return out;
    }
};
} // namespace ReverbPage
