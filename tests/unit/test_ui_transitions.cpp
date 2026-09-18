#include "ui/UITransitions.h"
#include "ui/ControlSurfaceLogic.h"
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
    state.encoderControlWasPressed = true;
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
    CHECK_FALSE(state.encoderControlWasPressed);
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
