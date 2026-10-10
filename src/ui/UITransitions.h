#pragma once

#include "TuningPageLogic.h"
#include "UIState.h"

// UITransitions.h — the only place that mutates UI mode state.
// Pure Core-0 state policies (no hardware, no OLED/LED writes): callers own
// side effects. Every transition leaves no modal residue so the pads always
// return to a known grid. Add new mode switches here, not inline in handlers.
namespace UITransitions {
// Display-only navigation never changes a lane, sounding voice, or pad gesture.
// Both eight-row banks are reached before leaving the Matrix page.
inline void cycleDisplayPage(UIState &state) noexcept
{
    if (state.display.page == LaneDisplay::Page::Matrix && state.display.laneBank == 0)
        state.display.laneBank = 1;
    else {
        state.display.laneBank = 0;
        state.display.page = LaneDisplay::cyclePage(state.display.page);
    }
}

inline void cycleDisplayStyle(UIState &state) noexcept
{
    state.display.style = LaneDisplay::cycleStyle(state.display.style);
}

// Leave step-edit: pads go back to toggling gates. Never stops sounding notes.
inline void clearStepEdit(UIState &state) noexcept
{
    state.selectedStepForEdit = -1;
    state.currentEditParameter = ParamId::Count;
}

inline void cancelGateLengthHold(UIState &state) noexcept
{
    if (state.gateSeqLengthMode) state.resetStepsLightsFlag = true;
    state.gateSeqLengthMode = false;
    state.gateSeqLengthVoice = -1;
}

// A page (voice editor, ADSR, reverb, tuning) owns the panel, or is still draining the
// release tail of its chord. Performance gestures stand down until it is gone.
inline bool pageOwnsPanel(const UIState &state) noexcept
{
    return state.voiceEditor.active || state.controlsWaitRelease ||
           state.voiceEnvelope.active || state.voiceEnvelope.chordPending ||
           state.voiceEnvelope.waitRelease || state.reverbPage.active ||
           state.reverbPage.waitRelease || state.tuningPage.active ||
           state.tuningPage.waitRelease;
}

// Only a fresh, unmodified voice press can arm length entry. Modal exits and
// Shift changes cannot resurrect an old hold before the next press edge. In copy
// mode a held voice button is the paste modifier, so it never becomes length entry.
inline void beginGateLengthHold(UIState &state, uint8_t voice) noexcept
{
    cancelGateLengthHold(state);
    if (voice >= UIState::MAX_VOICES || state.shiftHeld || state.settingsMode ||
        state.arp.active() || state.copyLane.active || pageOwnsPanel(state)) return;
    state.gateSeqLengthVoice = static_cast<int8_t>(voice);
}

// Returns true only on entry, so the tile bridge can also clear its latch.
inline bool updateGateLengthHold(UIState &state, uint8_t voice, bool held,
                                 uint32_t heldMs, uint32_t thresholdMs) noexcept
{
    if (state.gateSeqLengthVoice != static_cast<int8_t>(voice)) return false;
    if (!held || state.shiftHeld || state.settingsMode || state.arp.active() ||
        state.copyLane.active || pageOwnsPanel(state) || state.selectedVoiceIndex != voice)
    {
        cancelGateLengthHold(state);
        return false;
    }
    if (state.gateSeqLengthMode || heldMs < thresholdMs) return false;
    state.gateSeqLengthMode = true;
    state.slideMode = state.modGateParamSeqLengthsMode = false;
    state.latchedParameter = -1;
    for (auto &parameterHeld : state.parameterButtonHeld) parameterHeld = false;
    for (auto &pressedAt : state.padPressTimestamps) pressedAt = 0;
    clearStepEdit(state);
    state.resetStepsLightsFlag = true;
    return true;
}

// The dark partner bank must not edit another voice behind the length screen.
inline uint8_t gateLengthForPad(const UIState &state, uint8_t voice, uint8_t step) noexcept
{
    if (!state.gateSeqLengthMode || state.gateSeqLengthVoice != voice || step >= 16)
        return 0;
    return step == 0 ? 2 : static_cast<uint8_t>(step + 1);
}

// The ADSR page is live: only clear competing UI gestures, never transport.
inline void openVoiceEnvelope(UIState &state, uint8_t voice) noexcept
{
    if (voice >= UIState::MAX_VOICES) return;
    state.voiceEnvelope.active = true;
    state.selectedVoiceIndex = voice;
    clearStepEdit(state);
    state.settingsMode = state.slideMode = false;
    cancelGateLengthHold(state);
    state.modGateParamSeqLengthsMode = false;
    state.latchedParameter = -1;
    for (auto &held : state.parameterButtonHeld) held = false;
    for (auto &held : state.randomizeWasPressed) held = false;
    for (auto &time : state.padPressTimestamps) time = 0;
    state.envFaderLane = ParamId::Count;
    state.envViewUntil = state.encoderBaseViewUntil = 0;
    state.voiceParameterFeedbackPending = false;
    state.alchemyModeBannerUntil = state.oledNoticeUntil = 0;
    state.voiceSwitchTriggered = state.resetStepsLightsFlag = true;
}

// The reverb page is live too (transport keeps running): clear only the competing
// UI gestures. ReverbPage::Controls::poll() has already set active/waitRelease.
inline void openReverbPage(UIState &state) noexcept
{
    state.voiceEnvelope = {};
    clearStepEdit(state);
    state.settingsMode = state.slideMode = false;
    cancelGateLengthHold(state);
    state.modGateParamSeqLengthsMode = false;
    state.latchedParameter = -1;
    for (auto &held : state.parameterButtonHeld) held = false;
    for (auto &held : state.randomizeWasPressed) held = false;
    for (auto &time : state.padPressTimestamps) time = 0;
    state.envFaderLane = ParamId::Count;
    state.envViewUntil = state.encoderBaseViewUntil = 0;
    state.voiceParameterFeedbackPending = false;
    state.alchemyModeBannerUntil = state.oledNoticeUntil = 0;
    state.voiceSwitchTriggered = state.resetStepsLightsFlag = true;
}

// Leaving redraws the step lights; the page struct already cleared its own flags.
inline void closeReverbPage(UIState &state) noexcept
{
    state.reverbPage.lastControl = 255;
    state.voiceSwitchTriggered = state.resetStepsLightsFlag = true;
}

// The Tuning page is live too (transport keeps running) and competes with the same
// gestures the Reverb page does, so it drops the same ones. TuningPage::Controls::poll()
// has already set active/waitRelease.
inline void openTuningPage(UIState &state) noexcept
{
    openReverbPage(state);
    state.tuningPage.lastControl = TuningPage::kNoControl;
    state.tuningNotice[0] = '\0';
    state.tuningNoticeUntil = 0;
}

// Leaving redraws the step lights; the page struct already cleared its own flags.
inline void closeTuningPage(UIState &state) noexcept
{
    state.tuningPage.lastControl = TuningPage::kNoControl;
    state.tuningNotice[0] = '\0';
    state.tuningNoticeUntil = 0;
    state.voiceSwitchTriggered = state.resetStepsLightsFlag = true;
}

// How long the OLED holds a Tuning page confirmation ("Saved to Hot 2").
constexpr unsigned long kTuningNoticeMs = 1500;

// Remember what a Tuning page gesture did, for the OLED's notice line. Gestures that changed
// nothing (a dark pad, the tuning that is already playing) say nothing; a tuning that brought
// a new scale along says so in the same line.
inline void showTuningNotice(UIState &state, const TuningPage::Result &result,
                             unsigned long nowMs) noexcept
{
    if (result.change == TuningPage::Change::None) return;
    TuningPage::formatNotice(result, state.tuningNotice, sizeof(state.tuningNotice));
    state.tuningNoticeUntil = nowMs + kTuningNoticeMs;
}

// Open the preset browser on the selected voice; always starts at presets so
// the grid and the screen agree. Safe while playing (apply is staged).
inline void openSettings(UIState &state) noexcept
{
    cancelGateLengthHold(state);
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
    cancelGateLengthHold(state);
    // A pre-slide hold/release must not re-enter length mode or select a step.
    for (auto &pressedAt : state.padPressTimestamps)
        pressedAt = 0;
}

// Switch the selected voice (0..3): pad banks follow, editor closes, OLED
// refreshes. Never touches sounding notes. Returns false out-of-range.
inline bool selectPerformanceVoice(UIState &state, uint8_t voice) noexcept
{
    if (voice >= UIState::MAX_VOICES)
        return false;
    cancelGateLengthHold(state);
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

// --- Arpeggiator mode --------------------------------------------------------

// Why: Arpeggiator mode reuses every sequencer control, so any sequencer-shaped
// modal state left over from the other mode would reinterpret the next gesture
// (a step in edit, a held parameter, gate-length entry, a slide toggle). Both
// transitions drop that state and switch the engine, which owns the mode flag
// and the chord; the audio side (silencing sounding notes, the OLED notice)
// stays in ArpPlayback::arpModeToggle, which calls these.
inline void enterArpMode(UIState &state) noexcept
{
    state.voiceEnvelope = {};
    state.arp.setActive(true);
    state.arpLastNotes[0] = 0;
    state.arpControl = UIState::ArpControl::None;
    state.selectedStepForEdit = -1;
    state.currentEditParameter = ParamId::Count;
    state.slideMode = false;
    cancelGateLengthHold(state);
    state.modGateParamSeqLengthsMode = false;
    state.latchedParameter = -1;
    state.envFaderLane = ParamId::Count;
    state.envViewUntil = 0;
    state.settingsMode = false;
    state.voiceParameterFeedbackPending = false;
    for (auto &held : state.parameterButtonHeld)
        held = false;
    for (auto &pressedAt : state.padPressTimestamps)
        pressedAt = 0;
}

inline void exitArpMode(UIState &state) noexcept
{
    state.voiceEnvelope = {};
    state.arp.setActive(false);
    state.arpLastNotes[0] = 0;
    state.arpControl = UIState::ArpControl::None;
    state.selectedStepForEdit = -1;
    state.currentEditParameter = ParamId::Count;
    cancelGateLengthHold(state);
    for (auto &pressedAt : state.padPressTimestamps)
        pressedAt = 0;
}
} // namespace UITransitions
