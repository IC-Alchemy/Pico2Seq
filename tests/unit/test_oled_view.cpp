// tests/unit/test_oled_view.cpp — regression suite for the OLED routing
// state machine (OledView::route). Drives a real Sequencer with a real
// patch-enabled VoiceConfig and local UIState objects, and pins down:
// page selection per focused parameter, base-feedback priority, composed
// selected-step values, gate-rejected note retention, the encoder-target
// fallback, the resting screen, the modal priority order, the purity of
// route(), and the exclusivity of the sensor-distance readout.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "src/ui/OledView.h"
#include "src/voice/MusicalValues.h"
#include "src/voice/VoiceEditParameters.h"
#include "src/voice/VoicePresets.h"

using Catch::Approx;
using OledView::Page;
using OledView::route;

namespace
{

constexpr unsigned long kNowMs = 1000;

// A real Sequencer seeded with neutral modifiers and composing playback
// through the live VoiceConfig, exactly as the firmware does. `config` is
// declared first so the playback context outlives the sequencer.
struct PatchRig
{
    VoiceConfig config;
    Sequencer seq;

    PatchRig()
    {
        config = VoicePresets::getDigitalVoice();
        VoiceEdit::enablePatch(config);
        VoiceEdit::seedModifiers(seq);
        seq.setPlaybackTransform(VoiceEdit::composeLane, &config, VoiceEdit::mapOctave);
    }
};

// Step has no operator==; route() must return exactly what the same
// authoritative sequencer queries return, so exact comparison is intended.
bool sameStep(const Step &a, const Step &b)
{
    return a.noteIndex == b.noteIndex && a.velocityLevel == b.velocityLevel &&
           a.filterCutoff == b.filterCutoff &&
           a.attackTimeSeconds == b.attackTimeSeconds &&
           a.decayTimeSeconds == b.decayTimeSeconds &&
           a.octaveOffset == b.octaveOffset &&
           a.gateLengthTicks == b.gateLengthTicks &&
           a.isGateActive == b.isGateActive && a.hasSlide == b.hasSlide;
}

// Everything route() must leave untouched in the sequencer.
struct SeqSnapshot
{
    std::vector<float> stored; // every lane, steps 0..15
    std::vector<uint8_t> cursors;
    uint8_t currentStep = 0;
    Step playing;
    Step at2;
    Step at5;
    bool notePlaying = false;
    bool running = false;

    static SeqSnapshot capture(const Sequencer &s)
    {
        SeqSnapshot snap;
        for (uint8_t lane = 0; lane < PARAM_ID_COUNT; ++lane)
            for (uint8_t step = 0; step < 16; ++step)
                snap.stored.push_back(
                    s.getStepParameterValue(static_cast<ParamId>(lane), step));
        for (uint8_t lane = 0; lane < PARAM_ID_COUNT; ++lane)
            snap.cursors.push_back(s.getCurrentStepForParameter(static_cast<ParamId>(lane)));
        snap.currentStep = s.getCurrentStep();
        snap.playing = s.getPlaybackStep();
        snap.at2 = s.getPlaybackStep(2);
        snap.at5 = s.getPlaybackStep(5);
        snap.notePlaying = s.isNotePlaying();
        snap.running = s.isRunning();
        return snap;
    }

    bool operator==(const SeqSnapshot &other) const
    {
        return stored == other.stored && cursors == other.cursors &&
               currentStep == other.currentStep && sameStep(playing, other.playing) &&
               sameStep(at2, other.at2) && sameStep(at5, other.at5) &&
               notePlaying == other.notePlaying && running == other.running;
    }
};

// The UIState fields route() reads.
struct UiSnapshot
{
    int8_t focusedParameter = -1;
    int selectedStepForEdit = -1;
    ParamId currentEditParameter = ParamId::Count;
    EncoderParameterMode currentEncoderParameter = EncoderParameterMode::Velocity;
    bool settingsMode = false;
    bool gateSeqLengthMode = false;
    unsigned long oledNoticeUntil = 0;
    UIState::OledNoticeKind oledNoticeKind = UIState::OledNoticeKind::None;
    unsigned long alchemyModeBannerUntil = 0;
    unsigned long encoderBaseViewUntil = 0;
    bool editorActive = false;
    bool held[PARAM_ID_COUNT] = {};

    static UiSnapshot capture(const UIState &ui)
    {
        UiSnapshot snap;
        snap.focusedParameter = ui.focusedParameter;
        snap.selectedStepForEdit = ui.selectedStepForEdit;
        snap.currentEditParameter = ui.currentEditParameter;
        snap.currentEncoderParameter = ui.currentEncoderParameter;
        snap.settingsMode = ui.settingsMode;
        snap.gateSeqLengthMode = ui.gateSeqLengthMode;
        snap.oledNoticeUntil = ui.oledNoticeUntil;
        snap.oledNoticeKind = ui.oledNoticeKind;
        snap.alchemyModeBannerUntil = ui.alchemyModeBannerUntil;
        snap.encoderBaseViewUntil = ui.encoderBaseViewUntil;
        snap.editorActive = ui.voiceEditor.active;
        for (uint8_t i = 0; i < PARAM_ID_COUNT; ++i)
            snap.held[i] = ui.parameterButtonHeld[i];
        return snap;
    }

    bool operator==(const UiSnapshot &other) const
    {
        if (focusedParameter != other.focusedParameter ||
            selectedStepForEdit != other.selectedStepForEdit ||
            currentEditParameter != other.currentEditParameter ||
            currentEncoderParameter != other.currentEncoderParameter ||
            settingsMode != other.settingsMode ||
            gateSeqLengthMode != other.gateSeqLengthMode ||
            oledNoticeUntil != other.oledNoticeUntil ||
            oledNoticeKind != other.oledNoticeKind ||
            alchemyModeBannerUntil != other.alchemyModeBannerUntil ||
            encoderBaseViewUntil != other.encoderBaseViewUntil ||
            editorActive != other.editorActive)
            return false;
        for (uint8_t i = 0; i < PARAM_ID_COUNT; ++i)
            if (held[i] != other.held[i])
                return false;
        return true;
    }
};

} // namespace

TEST_CASE("Focused parameter page follows the held lane for all six parameters",
          "[oled_view]")
{
    PatchRig rig;
    // Reverse enum order: no lane may be masked by an earlier ParamId (the
    // old first-held-in-enum-order bug), and every focused lane wins.
    const ParamId lanes[] = {ParamId::Octave, ParamId::Decay,  ParamId::Attack,
                             ParamId::Filter, ParamId::Velocity, ParamId::Note};
    for (const ParamId lane : lanes)
    {
        CAPTURE(static_cast<int>(lane));
        UIState ui;
        ui.focusedParameter = static_cast<int8_t>(lane);
        const auto r = route(ui, rig.seq, &rig.config, kNowMs, ParamId::Count);
        REQUIRE(r.page == Page::Parameter);
        REQUIRE(r.param == lane);
        REQUIRE_FALSE(r.base);
        REQUIRE(r.showDistance);
        REQUIRE_FALSE(r.stepSelected);
        REQUIRE(r.step == rig.seq.getCurrentStepForParameter(lane));
    }
}

TEST_CASE("Encoder base feedback outranks a held parameter and shows the patch base",
          "[oled_view]")
{
    PatchRig rig;
    const Step base = MusicalValues::baseStep(rig.config);

    UIState ui;
    ui.focusedParameter = static_cast<int8_t>(ParamId::Filter); // a parameter is held
    ui.encoderBaseViewUntil = kNowMs + 1000;
    const auto r = route(ui, rig.seq, &rig.config, kNowMs, ParamId::Filter);

    REQUIRE(r.page == Page::Parameter);
    REQUIRE(r.param == ParamId::Filter);
    REQUIRE(r.base);
    REQUIRE(r.showBase);
    REQUIRE_FALSE(r.showDistance);
    REQUIRE_FALSE(r.stepSelected);
    // The values are the live config's base, field for field.
    REQUIRE(r.values.velocityLevel == base.velocityLevel);
    REQUIRE(r.values.filterCutoff == base.filterCutoff);
    REQUIRE(r.values.noteIndex == base.noteIndex);
    REQUIRE(r.values.octaveOffset == base.octaveOffset);
    REQUIRE(r.values.gateLengthTicks == base.gateLengthTicks);
    REQUIRE(r.values.isGateActive == base.isGateActive);
}

TEST_CASE("Focused page shows the composed value of the selected step", "[oled_view]")
{
    PatchRig rig;
    rig.seq.setStepParameterValue(ParamId::Filter, 2, 0.25f);
    rig.seq.setStepParameterValue(ParamId::Filter, 3, 0.75f);
    REQUIRE(rig.seq.getPlaybackStep(2).filterCutoff !=
            rig.seq.getPlaybackStep(3).filterCutoff);

    UIState ui;
    ui.focusedParameter = static_cast<int8_t>(ParamId::Filter);
    ui.selectedStepForEdit = 3;
    const auto r = route(ui, rig.seq, &rig.config, kNowMs, ParamId::Count);
    REQUIRE(r.page == Page::Parameter);
    REQUIRE(r.param == ParamId::Filter);
    REQUIRE(r.stepSelected);
    REQUIRE(r.step == 3);
    // Exactly the composed playback value of step 3, never the stored
    // modifier or another step's value.
    REQUIRE(r.values.filterCutoff == rig.seq.getPlaybackStep(3).filterCutoff);
    REQUIRE(r.values.filterCutoff ==
            Approx(VoiceEdit::composeLane(ParamId::Filter, 0.75f, &rig.config)));

    // With no step in edit, each lane follows its own playing cursor.
    UIState playing;
    playing.focusedParameter = static_cast<int8_t>(ParamId::Filter);
    const auto rPlaying = route(playing, rig.seq, &rig.config, kNowMs, ParamId::Count);
    REQUIRE(rPlaying.page == Page::Parameter);
    REQUIRE_FALSE(rPlaying.stepSelected);
    REQUIRE(rPlaying.step == rig.seq.getCurrentStepForParameter(ParamId::Filter));
    REQUIRE(sameStep(rPlaying.values, rig.seq.getPlaybackStep()));
}

TEST_CASE("Gate-rejected note keeps displaying the retained stored value", "[oled_view]")
{
    PatchRig rig;
    rig.seq.setStepParameterValue(ParamId::Gate, 2, 0.0f); // explicit rest step
    rig.seq.setStepParameterValue(ParamId::Note, 2, 5.0f);

    // A sensor recording write against the rest step is refused by the gate
    // rule; route() receives no sensor input at all and must keep showing
    // the stored note, not a prediction of the rejected write.
    const auto write = rig.seq.writeStepParameter(ParamId::Note, 2, 0.9f,
                                                  StepWriteDomain::Normalized01, true,
                                                  NoteGateRule::AtStep);
    REQUIRE(write.status == StepWriteStatus::Rejected);
    REQUIRE(rig.seq.getStepParameterValue(ParamId::Note, 2) == 5.0f);

    UIState ui;
    ui.focusedParameter = static_cast<int8_t>(ParamId::Note);
    ui.selectedStepForEdit = 2;
    const auto r = route(ui, rig.seq, &rig.config, kNowMs, ParamId::Count);
    REQUIRE(r.page == Page::Parameter);
    REQUIRE(r.param == ParamId::Note);
    REQUIRE(r.stepSelected);
    REQUIRE(r.step == 2);
    REQUIRE(r.values.noteIndex == rig.seq.getPlaybackStep(2).noteIndex);
    REQUIRE(r.values.noteIndex == 5.0f);
    REQUIRE_FALSE(r.values.isGateActive);
}

TEST_CASE("Encoder-target fallback mirrors selected-step edits", "[oled_view]")
{
    PatchRig rig;
    rig.seq.setStepParameterValue(ParamId::Filter, 2, 0.8f);

    // No focus, no toggled edit parameter: the screen shows the value a turn
    // changes — the encoder target's lane at the selected step (the old
    // "Hold parameter" placeholder bug while the encoder edited the lane).
    UIState ui;
    ui.selectedStepForEdit = 2;
    ui.currentEncoderParameter = EncoderParameterMode::Filter;
    auto r = route(ui, rig.seq, &rig.config, kNowMs, ParamId::Filter);
    REQUIRE(r.page == Page::Parameter);
    REQUIRE(r.param == ParamId::Filter);
    REQUIRE(r.stepSelected);
    REQUIRE(r.step == 2);
    REQUIRE_FALSE(r.base);
    REQUIRE_FALSE(r.showDistance);
    REQUIRE(r.values.filterCutoff == rig.seq.getPlaybackStep(2).filterCutoff);

    // A toggled edit parameter outranks the encoder target.
    UIState toggled;
    toggled.selectedStepForEdit = 2;
    toggled.currentEditParameter = ParamId::Attack;
    toggled.currentEncoderParameter = EncoderParameterMode::Filter;
    r = route(toggled, rig.seq, &rig.config, kNowMs, ParamId::Filter);
    REQUIRE(r.page == Page::Parameter);
    REQUIRE(r.param == ParamId::Attack);
    REQUIRE(r.stepSelected);
    REQUIRE(r.step == 2);

    // Nothing targeted on the selected step (voice-only Slide Time):
    // the placeholder, not a made-up parameter page.
    UIState voiceOnly;
    voiceOnly.selectedStepForEdit = 2;
    voiceOnly.currentEncoderParameter = EncoderParameterMode::SlideTime;
    r = route(voiceOnly, rig.seq, &rig.config, kNowMs, ParamId::Count);
    REQUIRE(r.page == Page::StepPlaceholder);
}

TEST_CASE("Resting screen shows the encoder target's playing value or base",
          "[oled_view]")
{
    PatchRig rig;
    rig.seq.setStepParameterValue(ParamId::Note, 0, 4.0f);
    rig.seq.setStepParameterValue(ParamId::Filter, 0, 0.3f);

    UIState ui;
    ui.currentEncoderParameter = EncoderParameterMode::Filter;
    const auto r = route(ui, rig.seq, &rig.config, kNowMs, ParamId::Filter);
    REQUIRE(r.page == Page::EncoderDefault);
    REQUIRE_FALSE(r.showBase);
    REQUIRE_FALSE(r.stepSelected);
    REQUIRE(r.param == ParamId::Filter);
    REQUIRE(sameStep(r.values, rig.seq.getPlaybackStep()));

    // While the turn window is open, a real encoder lane promotes to the
    // labeled BASE page even with nothing focused or selected.
    UIState turning;
    turning.encoderBaseViewUntil = kNowMs + 1000;
    const auto rTurning = route(turning, rig.seq, &rig.config, kNowMs, ParamId::Velocity);
    REQUIRE(rTurning.page == Page::Parameter);
    REQUIRE(rTurning.base);
    REQUIRE(rTurning.param == ParamId::Velocity);

    // A voice-only encoder target (Slide Time) keeps the resting screen and
    // shows the patch base while the window is open.
    UIState slideTarget;
    slideTarget.encoderBaseViewUntil = kNowMs + 1000;
    const auto rBase = route(slideTarget, rig.seq, &rig.config, kNowMs, ParamId::Count);
    REQUIRE(rBase.page == Page::EncoderDefault);
    REQUIRE(rBase.showBase);
    REQUIRE(rBase.param == ParamId::Count);
    const Step base = MusicalValues::baseStep(rig.config);
    REQUIRE(rBase.values.velocityLevel == base.velocityLevel);
    REQUIRE(rBase.values.filterCutoff == base.filterCutoff);
    REQUIRE(rBase.values.noteIndex == base.noteIndex);
    REQUIRE(rBase.values.octaveOffset == base.octaveOffset);
    REQUIRE(rBase.values.gateLengthTicks == base.gateLengthTicks);
}

TEST_CASE("Modal windows outrank parameter, settings and length screens in order",
          "[oled_view]")
{
    PatchRig rig;
    const auto armEverything = [](UIState &ui) {
        ui.focusedParameter = static_cast<int8_t>(ParamId::Velocity);
        ui.selectedStepForEdit = 4;
        ui.settingsMode = true;
        ui.gateSeqLengthMode = true;
        ui.alchemyModeBannerUntil = kNowMs + 1000;
        ui.oledNoticeUntil = kNowMs + 1000;
        ui.oledNoticeKind = UIState::OledNoticeKind::Randomized;
        ui.voiceEditor.active = true;
    };

    // The voice editor outranks everything.
    UIState editor;
    armEverything(editor);
    REQUIRE(route(editor, rig.seq, &rig.config, kNowMs, ParamId::Count).page ==
            Page::VoiceEditor);

    // The strap banner outranks everything below it.
    UIState banner;
    armEverything(banner);
    banner.voiceEditor.active = false;
    REQUIRE(route(banner, rig.seq, &rig.config, kNowMs, ParamId::Count).page ==
            Page::ModeBanner);

    // The notice window outranks focus and the settings screens.
    UIState notice;
    armEverything(notice);
    notice.voiceEditor.active = false;
    notice.alchemyModeBannerUntil = 0;
    REQUIRE(route(notice, rig.seq, &rig.config, kNowMs, ParamId::Count).page ==
            Page::Notice);

    // A closed or kindless notice does not hold the screen.
    UIState stale;
    armEverything(stale);
    stale.voiceEditor.active = false;
    stale.alchemyModeBannerUntil = 0;
    stale.oledNoticeKind = UIState::OledNoticeKind::None;
    auto r = route(stale, rig.seq, &rig.config, kNowMs, ParamId::Count);
    REQUIRE(r.page == Page::Parameter);
    REQUIRE(r.param == ParamId::Velocity);

    // A focused parameter outranks the settings and gate-length screens.
    UIState focusWins;
    armEverything(focusWins);
    focusWins.voiceEditor.active = false;
    focusWins.alchemyModeBannerUntil = 0;
    focusWins.oledNoticeUntil = 0;
    r = route(focusWins, rig.seq, &rig.config, kNowMs, ParamId::Count);
    REQUIRE(r.page == Page::Parameter);
    REQUIRE(r.param == ParamId::Velocity);

    // Settings outranks gate-length editing.
    UIState settings;
    armEverything(settings);
    settings.voiceEditor.active = false;
    settings.alchemyModeBannerUntil = 0;
    settings.oledNoticeUntil = 0;
    settings.focusedParameter = -1;
    REQUIRE(route(settings, rig.seq, &rig.config, kNowMs, ParamId::Count).page ==
            Page::SettingsMenu);

    // Gate-length editing alone.
    UIState length;
    armEverything(length);
    length.voiceEditor.active = false;
    length.alchemyModeBannerUntil = 0;
    length.oledNoticeUntil = 0;
    length.focusedParameter = -1;
    length.settingsMode = false;
    REQUIRE(route(length, rig.seq, &rig.config, kNowMs, ParamId::Count).page ==
            Page::GateLength);
}

TEST_CASE("route() is read-only against the sequencer and the UI state", "[oled_view]")
{
    PatchRig rig;
    rig.seq.setStepParameterValue(ParamId::Gate, 0, 1.0f);
    rig.seq.setStepParameterValue(ParamId::Gate, 5, 1.0f);
    rig.seq.setStepParameterValue(ParamId::Note, 2, 7.0f);
    rig.seq.setStepParameterValue(ParamId::Note, 4, 3.0f);
    rig.seq.setStepParameterValue(ParamId::Velocity, 4, 0.85f);
    rig.seq.setStepParameterValue(ParamId::Filter, 4, 0.65f);
    rig.seq.setStepParameterValue(ParamId::Attack, 4, 0.4f);
    rig.seq.setStepParameterValue(ParamId::Decay, 4, 0.9f);
    rig.seq.start();
    VoiceState state;
    rig.seq.advanceStep(3, -1, false, false, false, false, false, false, -1, &state);

    const auto seqBefore = SeqSnapshot::capture(rig.seq);

    UIState resting;
    UIState editing;
    editing.focusedParameter = static_cast<int8_t>(ParamId::Filter);
    editing.selectedStepForEdit = 4;
    editing.encoderBaseViewUntil = kNowMs + 1000; // base window + focus + selection
    UIState fallback;
    fallback.selectedStepForEdit = 2;
    fallback.currentEncoderParameter = EncoderParameterMode::Velocity;
    UIState modals;
    modals.settingsMode = true;
    modals.gateSeqLengthMode = true;
    modals.alchemyModeBannerUntil = kNowMs + 1000;
    modals.oledNoticeUntil = kNowMs + 1000;
    modals.oledNoticeKind = UIState::OledNoticeKind::Saved;

    const UIState *states[] = {&resting, &editing, &fallback, &modals};
    constexpr size_t stateCount = sizeof(states) / sizeof(states[0]);
    std::vector<UiSnapshot> before;
    before.reserve(stateCount);
    for (const UIState *ui : states)
    {
        before.push_back(UiSnapshot::capture(*ui));
        // Repeated evaluation in every routing regime must change nothing:
        // no track writes, no cursor advance, no note lifecycle change.
        for (int i = 0; i < 3; ++i)
            (void)route(*ui, rig.seq, &rig.config, kNowMs, ParamId::Filter);
    }

    REQUIRE(SeqSnapshot::capture(rig.seq) == seqBefore);
    for (size_t i = 0; i < stateCount; ++i)
        REQUIRE(UiSnapshot::capture(*states[i]) == before[i]);
}

TEST_CASE("showDistance is exclusive to focused parameter pages", "[oled_view]")
{
    PatchRig rig;

    UIState focused;
    focused.focusedParameter = static_cast<int8_t>(ParamId::Attack);
    REQUIRE(route(focused, rig.seq, &rig.config, kNowMs, ParamId::Count).showDistance);

    // The base page labels the patch base, not the sensor.
    UIState base;
    base.focusedParameter = static_cast<int8_t>(ParamId::Attack);
    base.encoderBaseViewUntil = kNowMs + 1000;
    REQUIRE_FALSE(route(base, rig.seq, &rig.config, kNowMs, ParamId::Attack).showDistance);

    // The selected-step encoder-target fallback shows step data only.
    UIState fallback;
    fallback.selectedStepForEdit = 6;
    fallback.currentEncoderParameter = EncoderParameterMode::Decay;
    REQUIRE_FALSE(
        route(fallback, rig.seq, &rig.config, kNowMs, ParamId::Decay).showDistance);

    UIState placeholder;
    placeholder.selectedStepForEdit = 6;
    placeholder.currentEncoderParameter = EncoderParameterMode::SlideTime;
    REQUIRE_FALSE(
        route(placeholder, rig.seq, &rig.config, kNowMs, ParamId::Count).showDistance);

    UIState resting;
    REQUIRE_FALSE(route(resting, rig.seq, &rig.config, kNowMs, ParamId::Velocity).showDistance);
}
