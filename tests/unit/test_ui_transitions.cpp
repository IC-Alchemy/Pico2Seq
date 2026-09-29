#include "ui/UITransitions.h"
#include "ui/ControlSurfaceLogic.h"
#include "ui/ButtonManager.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>

TEST_CASE("Settings page is derived only from settings state", "[control_surface][ui_transitions]")
{
    UIState state;
    CHECK_FALSE(state.isPresetSelection());
    CHECK_FALSE(state.isVoiceParameterSettings());
    for (const auto page : {UIState::SettingsSubMode::PRESET_SELECTION,
                            UIState::SettingsSubMode::VOICE_PARAMETER})
    {
        state.currentSubMode = page;
        state.settingsMode = false;
        UITransitions::showVoiceParameterFeedback(state, 9, 100);
        CHECK_FALSE(state.isPresetSelection());
        CHECK_FALSE(state.isVoiceParameterSettings());
        state.settingsMode = true;
        CHECK(state.isPresetSelection() == (page == UIState::SettingsSubMode::PRESET_SELECTION));
        CHECK(state.isVoiceParameterSettings() == (page == UIState::SettingsSubMode::VOICE_PARAMETER));
    }

    state.selectedStepForEdit = 7;
    state.currentEditParameter = ParamId::Filter;
    UITransitions::openSettings(state);
    CHECK(state.isPresetSelection());
    CHECK_FALSE(state.hasVoiceParameterFeedback(100));
    CHECK(state.selectedStepForEdit == -1);
    CHECK(state.currentEditParameter == ParamId::Count);
    UITransitions::toggleSettingsPage(state);
    CHECK(state.isVoiceParameterSettings());
    CHECK_FALSE(state.hasVoiceParameterFeedback(100)); // entering a page is not feedback
    UITransitions::showVoiceParameterFeedback(state, 12, 100);
    UITransitions::toggleSettingsPage(state);
    CHECK(state.isPresetSelection());
    CHECK_FALSE(state.hasVoiceParameterFeedback(100));
    UITransitions::toggleSettingsPage(state);
    UITransitions::closeSettings(state);
    CHECK_FALSE(state.isPresetSelection());
    CHECK_FALSE(state.isVoiceParameterSettings());
    CHECK_FALSE(state.hasVoiceParameterFeedback(100));
    UITransitions::toggleSettingsPage(state); // closed menu ignores page navigation
    CHECK(state.currentSubMode == UIState::SettingsSubMode::VOICE_PARAMETER);
    UITransitions::openSettings(state); // reopening always starts on presets
    CHECK(state.isPresetSelection());
}

TEST_CASE("Parameter feedback is transient and independent of menus", "[control_surface][ui_transitions]")
{
    UIState state;
    CHECK_FALSE(state.hasVoiceParameterFeedback(0));
    for (const unsigned long start : {0UL, 100UL, std::numeric_limits<unsigned long>::max() - 1000})
    {
        UITransitions::showVoiceParameterFeedback(state, 12, start);
        CHECK(state.lastVoiceParameterButton == 12);
        CHECK(state.hasVoiceParameterFeedback(start));
        CHECK(state.hasVoiceParameterFeedback(start + 2999));
        CHECK_FALSE(state.hasVoiceParameterFeedback(start + 3000));
        CHECK_FALSE(state.isVoiceParameterSettings());
        CHECK_FALSE(state.isPresetSelection());
    }
}

TEST_CASE("Slide entry clears conflicting controls and consumes pending holds", "[control_surface][ui_transitions]")
{
    UIState state;
    state.selectedVoiceIndex = 3;
    state.currentEncoderParameter = EncoderParameterMode::Filter;
    state.settingsMode = true; // slide does not control transport or settings
    state.modGateParamSeqLengthsMode = true;
    state.gateSeqLengthMode = true;
    state.gateSeqLengthVoice = 3;
    state.selectedStepForEdit = 5;
    state.currentEditParameter = ParamId::Attack;
    state.latchedParameter = static_cast<int8_t>(ParamId::Attack);
    for (auto &held : state.parameterButtonHeld) held = true;
    for (auto &time : state.padPressTimestamps) time = 123;
    UITransitions::toggleSlide(state);
    CHECK(state.slideMode);
    CHECK(state.selectedStepForEdit == -1);
    CHECK(state.currentEditParameter == ParamId::Count);
    CHECK_FALSE(state.modGateParamSeqLengthsMode);
    CHECK_FALSE(state.gateSeqLengthMode);
    CHECK(state.gateSeqLengthVoice == -1);
    CHECK(state.latchedParameter == -1);
    for (const auto held : state.parameterButtonHeld) CHECK_FALSE(held);
    for (const auto time : state.padPressTimestamps)
        CHECK(ControlSurface::classifyPadRelease(time, 1000, 400) == ControlSurface::PadRelease::Ignore);
    CHECK(state.selectedVoiceIndex == 3);
    CHECK(state.currentEncoderParameter == EncoderParameterMode::Filter);
    CHECK(state.settingsMode);

    // Exit always leaves step edit clean, without restoring old holds/latches.
    state.selectedStepForEdit = 6;
    state.currentEditParameter = ParamId::Decay;
    UITransitions::toggleSlide(state);
    CHECK_FALSE(state.slideMode);
    CHECK(state.selectedStepForEdit == -1);
    CHECK(state.currentEditParameter == ParamId::Count);
    CHECK(state.latchedParameter == -1);
    for (const auto held : state.parameterButtonHeld) CHECK_FALSE(held);
}

TEST_CASE("Tile voice selection and pad focus preserve different edit semantics", "[control_surface][ui_transitions]")
{
    UIState state;
    for (uint8_t voice = 0; voice < UIState::MAX_VOICES; ++voice)
    {
        state.currentEditParameter = ParamId::Filter;
        state.voiceSwitchTriggered = false;
        UITransitions::focusPad(state, voice, 7);
        CHECK(state.selectedVoiceIndex == voice);
        CHECK(state.selectedStepForEdit == 7);
        CHECK(state.currentEditParameter == ParamId::Filter);
        CHECK(state.voiceSwitchTriggered);
        state.voiceSwitchTriggered = false;
        // Reselecting the same voice still exits step editing and refreshes.
        CHECK(UITransitions::selectPerformanceVoice(state, voice));
        CHECK(state.selectedVoiceIndex == voice);
        CHECK(state.selectedStepForEdit == -1);
        CHECK(state.currentEditParameter == ParamId::Count);
        CHECK(state.voiceSwitchTriggered);
    }
    state.voiceSwitchTriggered = false;
    state.selectedStepForEdit = 9;
    CHECK_FALSE(UITransitions::selectPerformanceVoice(state, 4));
    UITransitions::focusPad(state, 255, 0);
    CHECK(state.selectedVoiceIndex == 3);
    CHECK(state.selectedStepForEdit == 9);
    CHECK_FALSE(state.voiceSwitchTriggered);
}

TEST_CASE("Arpeggiator mode entry clears the sequencer's modal surface", "[control_surface][ui_transitions]")
{
    UIState state;
    CHECK_FALSE(state.arp.active());

    // Leave the sequencer in every mode that could reinterpret the next gesture.
    state.selectedStepForEdit = 5;
    state.currentEditParameter = ParamId::Velocity;
    state.slideMode = true;
    state.gateSeqLengthMode = true;
    state.modGateParamSeqLengthsMode = true;
    state.gateSeqLengthVoice = 0;
    state.latchedParameter = static_cast<int8_t>(ParamId::Filter);
    state.envFaderLane = ParamId::Attack;
    state.envViewUntil = 1000;
    state.settingsMode = true;
    state.voiceParameterFeedbackPending = true;
    state.parameterButtonHeld[static_cast<size_t>(ParamId::Note)] = true;
    state.padPressTimestamps[3] = 42;
    state.arp.pressPad(7); // a chord entered before the mode was on

    UITransitions::enterArpMode(state);
    CHECK(state.arp.active());
    CHECK(state.selectedStepForEdit == -1);
    CHECK(state.currentEditParameter == ParamId::Count);
    CHECK_FALSE(state.slideMode);
    CHECK_FALSE(state.gateSeqLengthMode);
    CHECK_FALSE(state.modGateParamSeqLengthsMode);
    CHECK(state.gateSeqLengthVoice == -1);
    CHECK(state.latchedParameter == -1);
    CHECK(state.envFaderLane == ParamId::Count);
    CHECK(state.envViewUntil == 0);
    CHECK_FALSE(state.settingsMode);
    CHECK_FALSE(state.voiceParameterFeedbackPending);
    CHECK_FALSE(state.parameterButtonHeld[static_cast<size_t>(ParamId::Note)]);
    CHECK(state.padPressTimestamps[3] == 0);
    // The mode's own edges drop the chord, so nothing entered before the switch
    // can sound in the new mode.
    CHECK(state.arp.chordCount() == 0);

    // Settings still opens from inside the mode (the preset browser is reachable
    // while the arp plays), and exits without disturbing it.
    UITransitions::openSettings(state);
    CHECK(state.settingsMode);
    CHECK(state.arp.active());
    UITransitions::closeSettings(state);
    CHECK_FALSE(state.settingsMode);
    CHECK(state.arp.active());

    // Leaving clears the sequencer-shaped leftovers too.
    state.selectedStepForEdit = 2;
    state.gateSeqLengthMode = true;
    UITransitions::exitArpMode(state);
    CHECK_FALSE(state.arp.active());
    CHECK(state.selectedStepForEdit == -1);
    CHECK_FALSE(state.gateSeqLengthMode);

    // Voice selection is not the arp's business: it keeps working in both modes.
    state.selectedVoiceIndex = 2;
    CHECK(UITransitions::selectPerformanceVoice(state, 1));
    CHECK(state.selectedVoiceIndex == 1);
}

namespace {
bool pollVoiceHold(UIState &state, uint8_t voice, bool held, uint32_t heldMs)
{
    return UITransitions::updateGateLengthHold(state, voice, held, heldMs,
                                               UITimingConstants::LONG_PRESS_THRESHOLD_MS);
}
}

TEST_CASE("Voice holds edit only their own gate bank in either panel mode", "[control_surface][ui_transitions][gate_length]")
{
    for (const auto mode : {UIState::AlchemyMode::Param, UIState::AlchemyMode::Utility})
    for (uint8_t voice = 0; voice < UIState::MAX_VOICES; ++voice)
    {
        UIState state;
        state.alchemyMode = mode;
        CAPTURE(voice, static_cast<int>(mode));
        UITransitions::selectPerformanceVoice(state, voice);
        UITransitions::beginGateLengthHold(state, voice);
        CHECK(state.selectedVoiceIndex == voice); // immediate selection, before hold
        CHECK_FALSE(pollVoiceHold(state, voice, true, 399));
        CHECK_FALSE(state.gateSeqLengthMode);
        CHECK(pollVoiceHold(state, voice, true, 400));
        CHECK(state.gateSeqLengthMode);
        CHECK_FALSE(pollVoiceHold(state, voice, true, 900)); // one entry per press
        for (uint8_t index = 0; index < 32; ++index)
        {
            const auto pad = ControlSurface::PadBank::resolve(index, voice);
            const uint8_t expected = pad.voice != voice ? 0 : (pad.step == 0 ? 2 : pad.step + 1);
            CHECK(UITransitions::gateLengthForPad(state, pad.voice, pad.step) == expected);
        }
        CHECK(UITransitions::gateLengthForPad(state, voice, 16) == 0);
        // Another voice's release cannot close the held voice's editor.
        CHECK_FALSE(pollVoiceHold(state, (voice + 1) % 4, false, 0));
        CHECK(state.gateSeqLengthMode);
        state.resetStepsLightsFlag = false;
        CHECK_FALSE(pollVoiceHold(state, voice, false, 0));
        CHECK_FALSE(state.gateSeqLengthMode);
        CHECK(state.gateSeqLengthVoice == -1);
        CHECK(state.resetStepsLightsFlag);
        CHECK(UITransitions::gateLengthForPad(state, voice, 7) == 0);
    }
}

TEST_CASE("Short voice taps and unarmed levels never open length entry", "[control_surface][ui_transitions][gate_length]")
{
    UIState state;
    CHECK_FALSE(pollVoiceHold(state, 0, true, 1000)); // held through boot / editor exit
    CHECK_FALSE(state.gateSeqLengthMode);
    UITransitions::selectPerformanceVoice(state, 2);
    UITransitions::beginGateLengthHold(state, 2);
    CHECK_FALSE(pollVoiceHold(state, 2, false, 399));
    CHECK(state.selectedVoiceIndex == 2);
    CHECK(state.gateSeqLengthVoice == -1);
    CHECK_FALSE(state.gateSeqLengthMode);
}

TEST_CASE("Gate length entry clears competing edits and pending pad releases", "[control_surface][ui_transitions][gate_length]")
{
    UIState state;
    UITransitions::beginGateLengthHold(state, 0);
    state.slideMode = state.modGateParamSeqLengthsMode = true;
    state.selectedStepForEdit = 5;
    state.currentEditParameter = ParamId::Attack;
    state.latchedParameter = static_cast<int8_t>(ParamId::Attack);
    for (auto &held : state.parameterButtonHeld) held = true;
    for (auto &time : state.padPressTimestamps) time = 100;
    REQUIRE(pollVoiceHold(state, 0, true, 400));
    CHECK_FALSE(state.slideMode);
    CHECK_FALSE(state.modGateParamSeqLengthsMode);
    CHECK(state.latchedParameter == -1);
    CHECK(state.selectedStepForEdit == -1);
    CHECK(state.currentEditParameter == ParamId::Count);
    for (const auto held : state.parameterButtonHeld) CHECK_FALSE(held);
    // Release after leaving length mode must not toggle a previously held pad.
    pollVoiceHold(state, 0, false, 0);
    for (const auto time : state.padPressTimestamps)
        CHECK(ControlSurface::classifyPadRelease(time, 1000, 400) == ControlSurface::PadRelease::Ignore);
}

TEST_CASE("Shift and modal gestures cannot become voice length holds", "[control_surface][ui_transitions][gate_length]")
{
    for (uint8_t blocker = 0; blocker < 8; ++blocker)
    for (const bool alreadyArmed : {false, true})
    {
        UIState state;
        CAPTURE(blocker, alreadyArmed);
        if (alreadyArmed) UITransitions::beginGateLengthHold(state, 0);
        switch (blocker)
        {
        case 0: state.shiftHeld = true; break;
        case 1: state.settingsMode = true; break;
        case 2: state.arp.setActive(true); break;
        case 3: state.voiceEditor.active = true; break;
        case 4: state.controlsWaitRelease = true; break;
        case 5: state.voiceEnvelope.active = true; break;
        case 6: state.voiceEnvelope.chordPending = true; break;
        case 7: state.voiceEnvelope.waitRelease = true; break;
        }
        if (!alreadyArmed) UITransitions::beginGateLengthHold(state, 0);
        CHECK_FALSE(pollVoiceHold(state, 0, true, 400));
        CHECK_FALSE(state.gateSeqLengthMode);
        CHECK(state.gateSeqLengthVoice == -1);
        state.shiftHeld = state.settingsMode = state.voiceEditor.active = false;
        state.controlsWaitRelease = false;
        state.voiceEnvelope = {};
        state.arp.setActive(false);
        CHECK_FALSE(pollVoiceHold(state, 0, true, 1000)); // release + fresh press required
    }
}

TEST_CASE("Voice switches and modal transitions cancel length ownership", "[control_surface][ui_transitions][gate_length]")
{
    UIState state;
    UITransitions::beginGateLengthHold(state, 0);
    REQUIRE(pollVoiceHold(state, 0, true, 400));
    UITransitions::selectPerformanceVoice(state, 3);
    UITransitions::beginGateLengthHold(state, 3);
    CHECK_FALSE(pollVoiceHold(state, 0, false, 0));
    CHECK(state.gateSeqLengthVoice == 3);
    REQUIRE(pollVoiceHold(state, 3, true, 400));
    SECTION("Settings") { UITransitions::openSettings(state); }
    SECTION("Slide") { UITransitions::toggleSlide(state); }
    SECTION("Arpeggiator") { UITransitions::enterArpMode(state); }
    SECTION("Voice envelope") { UITransitions::openVoiceEnvelope(state, 2); }
    SECTION("Mode strap") { UITransitions::cancelGateLengthHold(state); }
    CHECK_FALSE(state.gateSeqLengthMode);
    CHECK(state.gateSeqLengthVoice == -1);
    CHECK_FALSE(pollVoiceHold(state, 3, true, 1000));
}
