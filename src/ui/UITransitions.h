#pragma once

#include "UIState.h"

// Pure Core-0 UI transitions. Hardware effects (encoder reset,
// tile edge histories) remain with the caller, not in these state policies.
namespace UITransitions {
inline void clearParameterHolds(UIState &state) noexcept
{
    state.parameterFocus.reset();
    state.parameterFocus.applyTo(state.parameterButtonHeld, PARAM_ID_COUNT);
    state.latchedParameter = -1;
    state.editGesture.reset();
    state.encoderBaseViewUntil = 0;
}

inline void clearStepEdit(UIState &state) noexcept
{
    state.selectedStepForEdit = -1;
    state.currentEditParameter = ParamId::Count;
    state.editGesture.reset();
}

inline void openSettings(UIState &state) noexcept
{
    clearParameterHolds(state);
    state.settingsMode = true;
    state.currentSubMode = UIState::SettingsSubMode::PRESET_SELECTION;
    state.voiceParameterFeedbackPending = false;
    clearStepEdit(state);
}

inline void closeSettings(UIState &state) noexcept
{
    clearParameterHolds(state);
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
    clearParameterHolds(state);
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
    clearParameterHolds(state);
    state.selectedVoiceIndex = voice;
    clearStepEdit(state);
    state.voiceSwitchTriggered = true;
    return true;
}

// Pad focus preserves the parameter target and must not stop sounding notes.
inline void focusPad(UIState &state, uint8_t voice, uint8_t step) noexcept
{
    if (voice >= UIState::MAX_VOICES || step >= SequencerConstants::MAX_STEPS_COUNT)
        return;
    if (state.selectedVoiceIndex != voice)
        clearParameterHolds(state);
    if (state.selectedVoiceIndex != voice || state.selectedStepForEdit != step)
        state.editGesture.reset();
    state.selectedVoiceIndex = voice;
    state.selectedStepForEdit = step;
    state.voiceSwitchTriggered = true;
}
} // namespace UITransitions
