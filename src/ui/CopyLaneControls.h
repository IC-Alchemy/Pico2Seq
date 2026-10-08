#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "../pico2seq-core/sequencer/LaneCopy.h"

// CopyLaneControls.h — gesture policy and "COPY LANE" memory (Core 0).
// Pure edge logic, no hardware calls; the instance lives in UIState beside the
// Reverb and Tuning pages. Unlike those pages it does NOT own the panel: pads,
// voice selection and recording keep working while copy mode is on. It only
// claims one gesture and one key.
//
// The gesture is a chord: a VOICE button (V1-V4) together with a LANE button
// (Param mode buttons 1-6: Note, Velocity, Filter, Attack, Release, Octave).
//   - Copy mode off: the chord copies that voice's lane into the COPY LANE memory
//     and turns copy mode on. Every button can then be released; the memory stays.
//   - Copy mode on: the chord PASTES the memory onto that voice's lane (any voice,
//     any lane button), as often as wanted. The memory is not consumed.
//   - Shift leaves copy mode and forgets the memory.
// Order is forgiving: the voice held first and the lane pressed second always
// counts; the other way round counts only when the voice follows within
// kChordWindowMs, so a two-finger "simultaneous" press works whichever finger
// lands first, while holding a lane to record and tapping a voice later still
// just switches voice.
namespace CopyLane {

constexpr uint8_t kLaneKeys = 6;            // buttons 1-6 have a lane
constexpr uint8_t kNoKey = 255;
constexpr uint32_t kChordWindowMs = 60;     // lane-first chords: how long the voice may trail
constexpr uint32_t kPasteNoticeMs = 1200;   // "PASTED" confirmation before the COPY LANE screen returns

// OLED copy for the two screens (21 columns of the 6x8 font).
constexpr const char *kTitle = "COPY LANE";
constexpr const char *kPasteHint = "Hold voice+press lane";
constexpr const char *kExitHint = "SHIFT: exit copy mode";

enum class Action : uint8_t { None, Copy, Paste, Exit };

struct Input {
    Action action = Action::None;
    uint8_t voice = 0;           // Copy/Paste: the voice button of the chord (0..3)
    uint8_t laneKey = 0;         // Copy/Paste: the lane button of the chord (0..5 = ButtonModule8 bit)
    uint8_t claimed = 0;         // ButtonModule8 bits whose press edge THIS pass belongs to the chord
    bool undoLanePress = false;  // lane pressed first: its normal arm already ran and must be taken back
};

struct Controls {
    static constexpr uint8_t kShift = 1u << 7;
    static constexpr uint8_t kLaneMask = 0x3F; // buttons 1-6

    bool active = false;
    uint8_t sourceVoice = 0;       // where the memory was copied from (0-based)
    lanecopy::LaneSnapshot clip;   // the COPY LANE memory; clip.lane is the lane it holds

    // Last paste, for the brief confirmation screen.
    uint8_t pastedVoice = 0;
    ParamId pastedLane = ParamId::Count;
    unsigned long noticeUntil = 0;

    uint8_t previousButtons = 0;
    uint8_t previousVoices = 0;
    uint8_t newestVoice = kNoKey;  // most recent voice press still relevant
    uint8_t pendingKey = kNoKey;   // lane pressed with no voice down yet
    uint32_t pendingAt = 0;

    // Copy mode on, memory filled by the caller (lanecopy::capture into `clip`).
    // Stays off if the capture produced nothing.
    void begin(uint8_t voice) noexcept {
        active = clip.valid();
        sourceVoice = voice;
        noticeUntil = 0;
    }

    // Leave copy mode and forget the memory.
    void end() noexcept {
        active = false;
        clip = lanecopy::LaneSnapshot{};
        noticeUntil = 0;
        pendingKey = kNoKey;
    }

    void notePaste(uint8_t voice, ParamId lane, uint32_t nowMs) noexcept {
        pastedVoice = voice;
        pastedLane = lane;
        noticeUntil = nowMs + kPasteNoticeMs;
    }

    bool pasteNoticeShowing(unsigned long nowMs) const noexcept {
        return active && noticeUntil != 0 && nowMs < noticeUntil;
    }

    // Track levels without acting on them, for passes another modal screen owns, so
    // a button held across that screen is not read as a fresh press afterwards.
    void observe(uint8_t buttons, uint8_t voices) noexcept {
        previousButtons = buttons;
        previousVoices = voices;
        pendingKey = kNoKey;
    }

    // One pass of tile levels. `allowed` is false whenever the lane buttons mean
    // something else (Utility mode, Arpeggiator, Settings) or a page owns the panel;
    // Shift still leaves copy mode then.
    Input poll(uint8_t buttons, uint8_t voices, bool allowed, uint32_t nowMs) noexcept {
        const uint8_t pressed = buttons & ~previousButtons;
        const uint8_t voicesDown = voices & ~previousVoices;
        previousButtons = buttons;
        previousVoices = voices;
        if (voicesDown) newestVoice = lowestBit(voicesDown);
        Input out;

        if (pressed & kShift) {
            pendingKey = kNoKey;
            if (active) {
                end();
                out.action = Action::Exit;
            }
            return out;
        }
        // Shift held means these buttons belong to a Shift chord (latch, ADSR page...).
        if (!allowed || (buttons & kShift)) {
            pendingKey = kNoKey;
            return out;
        }

        const uint8_t lanePress = pressed & kLaneMask;
        if (lanePress) {
            const uint8_t key = lowestBit(lanePress);
            if (voices) { // the held voice is the modifier
                out.action = active ? Action::Paste : Action::Copy;
                out.voice = chosenVoice(voices);
                out.laneKey = key;
                out.claimed = static_cast<uint8_t>(1u << key);
                pendingKey = kNoKey;
                return out;
            }
            pendingKey = key; // wait briefly for the voice finger
            pendingAt = nowMs;
        } else if (voicesDown && pendingKey != kNoKey) {
            const bool laneStillDown = (buttons >> pendingKey) & 1u;
            if (laneStillDown && nowMs - pendingAt <= kChordWindowMs) {
                out.action = active ? Action::Paste : Action::Copy;
                out.voice = lowestBit(voicesDown);
                out.laneKey = pendingKey;
                out.undoLanePress = true;
            }
            pendingKey = kNoKey;
        }
        return out;
    }

private:
    static uint8_t lowestBit(uint8_t mask) noexcept {
        uint8_t bit = 0;
        while (bit < 7 && !(mask & (1u << bit))) ++bit;
        return bit;
    }

    // Several voice buttons down: the newest one wins, as the selected voice does.
    uint8_t chosenVoice(uint8_t voices) const noexcept {
        if (newestVoice < 8 && (voices & (1u << newestVoice))) return newestVoice;
        return lowestBit(voices);
    }
};

// "V2 Analog", cut to the buffer. The same line heads both screens.
inline void formatVoiceLine(char *out, size_t size, uint8_t voice, const char *preset) noexcept {
    std::snprintf(out, size, "V%u %s", static_cast<unsigned>(voice) + 1u, preset ? preset : "");
}

} // namespace CopyLane
