#include "display/DisplayGesture.h"
#include "display/DisplayPolicy.h"
#include "display/DisplayConfig.h"
#include "ui/UITransitions.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Display holds never also change swing", "[display][control_surface]") {
    using namespace DisplayGesture;
    SwingHold hold;
    CHECK(hold.update(true, false, true, false, 100, 400) == Action::None);
    CHECK(hold.update(false, false, true, false, 499, 400) == Action::None);
    CHECK(hold.update(false, false, true, true, 500, 400) == Action::Page);
    CHECK(hold.update(false, false, true, true, 900, 400) == Action::None);
    CHECK(hold.update(false, true, false, true, 901, 400) == Action::None);
    CHECK_FALSE(hold.armed);

    CHECK(hold.update(true, false, true, true, 1000, 400) == Action::None);
    CHECK(hold.update(false, false, true, false, 1400, 400) == Action::Style);
    CHECK(hold.update(false, true, false, false, 1410, 400) == Action::None);
}

TEST_CASE("Swing tap and cancelled display holds are one-shot", "[display][control_surface]") {
    using namespace DisplayGesture;
    SwingHold hold;
    CHECK(hold.update(false, true, false, false, 50, 400) == Action::None);
    CHECK(hold.update(true, false, true, false, 100, 400) == Action::None);
    CHECK(hold.update(false, true, false, false, 200, 400) == Action::Swing);
    CHECK(hold.update(false, true, false, false, 201, 400) == Action::None);
    hold.update(true, false, true, true, 300, 400);
    hold.cancel(); // mode change / modal page swallowed the release
    CHECK(hold.update(false, true, false, true, 900, 400) == Action::None);
    hold.update(true, false, true, false, UINT32_MAX - 200, 400);
    CHECK(hold.update(false, true, false, false, 199, 400) == Action::Page);
    CHECK_FALSE(hold.armed);
}

TEST_CASE("Display navigation reaches both lane banks and preserves musical state", "[display]") {
    UIState state;
    state.selectedVoiceIndex = 2;
    state.selectedStepForEdit = 7;
    state.currentEditParameter = ParamId::Release;
    state.parameterButtonHeld[0] = true;
    UITransitions::cycleDisplayPage(state);
    CHECK(state.display.page == LaneDisplay::Page::Matrix);
    CHECK(state.display.laneBank == 1);
    UITransitions::cycleDisplayPage(state);
    CHECK(state.display.page == LaneDisplay::Page::Observatory);
    CHECK(state.display.laneBank == 0);
    UITransitions::cycleDisplayPage(state);
    CHECK(state.display.page == LaneDisplay::Page::Focus);
    UITransitions::cycleDisplayPage(state);
    CHECK(state.display.page == LaneDisplay::Page::Matrix);
    CHECK(state.selectedVoiceIndex == 2);
    CHECK(state.selectedStepForEdit == 7);
    CHECK(state.currentEditParameter == ParamId::Release);
    CHECK(state.parameterButtonHeld[0]);
    for (int i = 0; i < static_cast<int>(LaneDisplay::Style::Count); ++i)
        UITransitions::cycleDisplayStyle(state);
    CHECK(state.display.style == LaneDisplay::Controls{}.style);
}

TEST_CASE("Context pages preempt and then restore the chosen overview", "[display]") {
    UIState state;
    state.display.page = LaneDisplay::Page::Observatory;
    CHECK(DisplayPolicy::effectivePage(state, 100) == LaneDisplay::Page::Observatory);
    for (bool *flag : {&state.voiceEditor.active, &state.voiceEnvelope.active,
                       &state.reverbPage.active, &state.tuningPage.active,
                       &state.settingsMode, &state.copyLane.active,
                       &state.gateSeqLengthMode}) {
        *flag = true;
        CHECK(DisplayPolicy::effectivePage(state, 100) == LaneDisplay::Page::Focus);
        *flag = false;
        CHECK(DisplayPolicy::effectivePage(state, 100) == LaneDisplay::Page::Observatory);
    }
    state.oledNoticeKind = UIState::OledNoticeKind::Saved;
    state.oledNoticeUntil = 200;
    CHECK(DisplayPolicy::effectivePage(state, 199) == LaneDisplay::Page::Focus);
    CHECK(DisplayPolicy::effectivePage(state, 200) == LaneDisplay::Page::Observatory);
    CHECK(DisplayPolicy::pending(100, UINT32_MAX - 100));
    CHECK_FALSE(DisplayPolicy::pending(100, 100));
    CHECK_FALSE(DisplayPolicy::pending(0, UINT32_MAX - 100));
    CHECK(state.display.page == LaneDisplay::Page::Observatory);
}
