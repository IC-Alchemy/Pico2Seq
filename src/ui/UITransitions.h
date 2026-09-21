#pragma once

#include "UIState.h"

// UITransitions.h — the only place that mutates UI mode state.
// Pure Core-0 state policies (no hardware, no OLED/LED writes): callers own
// side effects. Every transition leaves no modal residue so the pads always
// return to a known grid. Add new mode switches here, not inline in handlers.
namespace UITransitions {
// Leave step-edit: pads go back to toggling gates. Never stops sounding notes.
inline void clearStepEdit(UIState &state) noexcept
{
    state.selectedStepForEdit = -1;
    state.currentEditParameter = ParamId::Count;
}

// Open the preset browser on the selected voice; always starts at presets so
// the grid and the screen agree. Safe while playing (apply is staged).
inline void openSettings(UIState &state) noexcept
{
    state.settingsMode = true;
    state.currentSubMode = UIState::SettingsSubMode::PRESET_SELECTION;
    state.voiceParameterFeedbackPending = false;
    clearStepEdit(state);
}

// Close the browser and drop any half-open editor: next pad tap toggles.
inline void closeSettings(UIState &state) noexcept
{
    state.settingsMode = false;
    state.voiceParameterFeedbackPending = false;
    clearStepEdit(state);
}

// Flip browser page (presets <-> voice timbre); drops step-edit with it.
inline void toggleSettingsPage(UIState &state) noexcept
{
    if (!state.settingsMode)
        return;
    using Sub = UIState::SettingsSubMode;
    state.currentSubMode = state.currentSubMode == Sub::PRESET_SELECTION
                               ? Sub::VOICE_PARAMETER : Sub::PRESET_SELECTION;
    state.voiceParameterFeedbackPending = false;
    clearStepEdit(state);
}

// Post a short timbre-change banner (name/value filled in by the caller).
inline void showVoiceParameterFeedback(UIState &state, uint8_t button,
                                       unsigned long now) noexcept
{
    state.voiceParameterFeedbackPending = true;
    state.lastVoiceParameterButton = button;
    state.voiceParameterChangeTime = now;
}

// Toggle legato-edit: pads flip slide per step. Entering clears holds/latches
// and length modes so one gesture cannot do two jobs; exiting keeps the grid.
inline void toggleSlide(UIState &state) noexcept
{
    state.slideMode = !state.slideMode;
    clearStepEdit(state);
    if (!state.slideMode)
        return;
    for (auto &held : state.parameterButtonHeld)
        held = false;
    state.latchedParameter = -1;
    state.modGateParamSeqLengthsMode = false;
    state.gateSeqLengthMode = false;
    // A pre-slide hold/release must not re-enter length mode or select a step.
    state.encoderControlWasPressed = false;
    for (auto &pressedAt : state.padPressTimestamps)
        pressedAt = 0;
}

// Switch the selected voice (0..3): pad banks follow, editor closes, OLED
// refreshes. Never touches sounding notes. Returns false out-of-range.
inline bool selectPerformanceVoice(UIState &state, uint8_t voice) noexcept
{
    if (voice >= UIState::MAX_VOICES)
        return false;
    state.selectedVoiceIndex = voice;
    clearStepEdit(state);
    state.voiceSwitchTriggered = true;
    return true;
}

// Hold-to-edit a step: focus this voice+step for encoder/fader writes.
// Preserves the parameter target and never stops sounding notes.
inline void focusPad(UIState &state, uint8_t voice, uint8_t step) noexcept
{
    if (voice >= UIState::MAX_VOICES)
        return;
    state.selectedVoiceIndex = voice;
    state.selectedStepForEdit = step;
    state.voiceSwitchTriggered = true;
}
} // namespace UITransitions
