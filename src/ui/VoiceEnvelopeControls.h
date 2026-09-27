#pragma once

#include <cstdint>

// Shift (8) + button 6 reserves a chord; a voice button opens its ADSR page.
// Consume the entire gesture, including releases, before normal actions resume.
namespace VoiceEnvelope {
struct Input {
    bool consumed = false;
    int8_t voice = -1;
    bool exit = false;
    bool modifierTap = false; // no voice selected: retain Shift+6's old action
};

struct Controls {
    static constexpr uint8_t kShift = 1u << 7;
    static constexpr uint8_t kModifiers = kShift | (1u << 5);
    bool active = false;
    bool waitRelease = false;
    bool chordPending = false;
    uint8_t previousButtons = 0;
    uint8_t previousVoices = 0;

    Input poll(uint8_t buttons, uint8_t voices) noexcept {
        const uint8_t pressed = buttons & ~previousButtons;
        const uint8_t voicePress = voices & ~previousVoices;
        previousButtons = buttons;
        previousVoices = voices;
        Input out;
        if (waitRelease) {
            out.consumed = true;
            if (buttons == 0 && voices == 0) waitRelease = false;
            return out;
        }
        const bool modifiers = (buttons & kModifiers) == kModifiers;
        if (modifiers || chordPending) {
            out.consumed = true;
            chordPending = true;
            if (modifiers && voices) {
                for (uint8_t v = 0; v < 4; ++v)
                    if (voices & (1u << v)) { out.voice = v; break; }
                active = true;
                waitRelease = true;
                chordPending = false;
            } else if (buttons == 0 && voices == 0) {
                chordPending = false;
                out.modifierTap = !active;
            }
            return out;
        }
        if (!active) return out;
        out.consumed = true;
        if (pressed & kShift) {
            active = false;
            waitRelease = true;
            out.exit = true;
        } else {
            for (uint8_t v = 0; v < 4; ++v)
                if (voicePress & (1u << v)) { out.voice = v; break; }
        }
        return out;
    }
};
} // namespace VoiceEnvelope
