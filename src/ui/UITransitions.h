#pragma once

#include "UIState.h"

// Pure Core-0 UI transitions. Hardware effects (encoder reset,
// tile edge histories) remain with the caller, not in these state policies.
namespace UITransitions {
// Clear the armed set, the latch, and the derived focus together. Every
// transition that ends parameter-hold semantics must go through here so the
// bridge's latch history can never resurrect holds after the transition.
inline void clearParameterFocus(UIState &state) noexcept
{
    state.parameterLatch.reset();
    for (auto &held : state.parameterButtonHeld)
        held = false;
    state.latchedParameter = -1;
    state.focusedParameter = -1;
}

inline void clearStepEdit(UIState &state) noexcept
{
    state.selectedStepForEdit = -1;
    state.currentEditParameter = ParamId::Count;
    // The manual-edit target is gone; ownership must not survive it.
    state.stepEditOwner.reset();
}

inline void openSettings(UIState &state) noexcept
{
    state.settingsMode = true;
    state.currentSubMode = UIState::SettingsSubMode::PRESET_SELECTION;
    state.voiceParameterFeedbackPending = false;
    clearStepEdit(state);
}

inline void closeSettings(UIState &state) noexcept
{
    state.settingsMode = false;
    state.voiceParameterFeedbackPending = false;
    clearStepEdit(state);
}

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

inline void showVoiceParameterFeedback(UIState &state, uint8_t button,
                                       unsigned long now) noexcept
{
    state.voiceParameterFeedbackPending = true;
    state.lastVoiceParameterButton = button;
    state.voiceParameterChangeTime = now;
}

inline void toggleSlide(UIState &state) noexcept
{
    state.slideMode = !state.slideMode;
    clearStepEdit(state);
    if (!state.slideMode)
        return;
    clearParameterFocus(state);
    state.modGateParamSeqLengthsMode = false;
    state.gateSeqLengthMode = false;
    // A pre-slide hold/release must not re-enter length mode or select a step.
    state.encoderControlWasPressed = false;
    for (auto &pressedAt : state.padPressTimestamps)
        pressedAt = 0;
}

// Tile selection exits step editing and requests an immediate OLED refresh.
// Selection does not alter the sequencer/audio note lifecycle.
inline bool selectPerformanceVoice(UIState &state, uint8_t voice) noexcept
{
    if (voice >= UIState::MAX_VOICES)
        return false;
    state.selectedVoiceIndex = voice;
    clearStepEdit(state);
    state.voiceSwitchTriggered = true;
    return true;
}

// Pad focus preserves the parameter target and must not stop sounding notes.
inline void focusPad(UIState &state, uint8_t voice, uint8_t step) noexcept
{
    if (voice >= UIState::MAX_VOICES)
        return;
    state.selectedVoiceIndex = voice;
    state.selectedStepForEdit = step;
    state.voiceSwitchTriggered = true;
}
} // namespace UITransitions
