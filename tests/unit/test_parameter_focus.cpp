// Parameter focus regression suite (tests/unit/test_parameter_focus.cpp).
//
// Covers the parameter-focus policies shared by the Alchemy control bridge
// and the UI transitions:
//
//   - record buttons (bits 0..5) map to their ParamId and encoder base target
//     (ControlSurface::encoderBaseModeForRecordParam);
//   - ShiftLatch::focus() picks the MOST RECENTLY PRESSED physical hold, then
//     earlier holds, then the Shift latch — never first-in-enum-order (the
//     core regression: a latched Note used to mask every higher lane);
//   - applyTo() keeps the full armed set so multi-lane live recording still
//     sees every held lane while focus narrows to one;
//   - transitions that end parameter-hold semantics (slide entry, voice
//     change, settings, explicit clears) reset the UIState-owned latch, so a
//     stale latch can never resurrect holds afterwards;
//   - ControlSurface::stepEditParameter() fallback chain when nothing is
//     held or latched.
//
// The latch under test lives INSIDE UIState (UIState::parameterLatch); every
// test constructs a local UIState and drives it exactly the way
// AlchemyControlBridge does: feed edges to uiState.parameterLatch, then
// mirror applyTo()/latched()/focus() into the UIState fields.

#include "ui/ControlSurfaceLogic.h"
#include "ui/UIState.h"
#include "ui/UITransitions.h"

#include <catch2/catch_test_macros.hpp>

// ParamId and EncoderParameterMode live in the global namespace
// (SequencerDefs.h); the ControlSurface policies are imported wholesale,
// same as the sibling control-surface tests.
using namespace ControlSurface;

namespace
{
// ParamId as uint8_t, the form onParamButton() takes.
constexpr uint8_t kNote = static_cast<uint8_t>(ParamId::Note);         // 0
constexpr uint8_t kVelocity = static_cast<uint8_t>(ParamId::Velocity); // 1
constexpr uint8_t kFilter = static_cast<uint8_t>(ParamId::Filter);     // 2
constexpr uint8_t kAttack = static_cast<uint8_t>(ParamId::Attack);     // 3
constexpr uint8_t kDecay = static_cast<uint8_t>(ParamId::Decay);       // 4
constexpr uint8_t kOctave = static_cast<uint8_t>(ParamId::Octave);     // 5
constexpr uint8_t kGateLength = static_cast<uint8_t>(ParamId::GateLength); // 6
constexpr uint8_t kGate = static_cast<uint8_t>(ParamId::Gate);         // 7
constexpr uint8_t kSlide = static_cast<uint8_t>(ParamId::Slide);       // 8

// One bridge derivation pass: exactly what AlchemyControlBridge performs
// after each button edge (applyTo into the armed array, then mirror the
// latch and the focus into the UIState fields).
void bridgePass(UIState &state)
{
    state.parameterLatch.applyTo(state.parameterButtonHeld, PARAM_ID_COUNT);
    state.latchedParameter = state.parameterLatch.latched();
    state.focusedParameter = state.parameterLatch.focus();
}

// Shift+tap: press and release with Shift held toggles the latch on.
void shiftLatchParam(UIState &state, uint8_t paramId)
{
    state.parameterLatch.onParamButton(paramId, true, true);
    state.parameterLatch.onParamButton(paramId, false, true);
    bridgePass(state);
}

// Plain press without Shift: a momentary physical hold.
void holdParam(UIState &state, uint8_t paramId)
{
    state.parameterLatch.onParamButton(paramId, true, false);
    bridgePass(state);
}

void releaseParam(UIState &state, uint8_t paramId)
{
    state.parameterLatch.onParamButton(paramId, false, false);
    bridgePass(state);
}

// The transition-clears invariant: a subsequent bridge pass deriving from the
// UIState-owned latch must produce an empty armed set and no focus — nothing
// in the bridge state can resurrect the holds that were cleared.
void requireNoStaleHolds(UIState &state)
{
    bridgePass(state);
    for (uint8_t i = 0; i < PARAM_ID_COUNT; ++i)
    {
        CAPTURE(i);
        REQUIRE_FALSE(state.parameterButtonHeld[i]);
    }
    REQUIRE(state.parameterLatch.focus() == ShiftLatch::kNoLatch);
    REQUIRE(state.parameterLatch.latched() == ShiftLatch::kNoLatch);
    REQUIRE(state.focusedParameter == -1);
    REQUIRE(state.latchedParameter == -1);
    REQUIRE(focusedParameterId(state) == ParamId::Count);
}
} // namespace

// ---------------------------------------------------------------------------
// Record button -> ParamId -> encoder base target
// ---------------------------------------------------------------------------

TEST_CASE("All six record buttons resolve to their ParamId and encoder base target",
          "[parameter_focus]")
{
    struct RecordButton
    {
        uint8_t bit;
        ParamId param;
        EncoderParameterMode mode;
    };
    // Bits 0..5 of the ButtonModule8 tile, in ParamId order: Note..Octave.
    constexpr RecordButton buttons[] = {
        {0, ParamId::Note, EncoderParameterMode::Note},
        {1, ParamId::Velocity, EncoderParameterMode::Velocity},
        {2, ParamId::Filter, EncoderParameterMode::Filter},
        {3, ParamId::Attack, EncoderParameterMode::Attack},
        {4, ParamId::Decay, EncoderParameterMode::Decay},
        {5, ParamId::Octave, EncoderParameterMode::Octave},
    };
    static_assert(sizeof(buttons) / sizeof(buttons[0]) == 6,
                  "the six continuous record buttons");

    for (const auto &button : buttons)
    {
        CAPTURE(button.bit);
        CAPTURE(static_cast<int>(button.param));
        // The bit resolves to exactly its ParamId...
        REQUIRE(static_cast<uint8_t>(button.param) == button.bit);
        REQUIRE(parameterDefinition(button.param) != nullptr);
        REQUIRE(parameterDefinition(button.param)->recordable);
        // ...and to the matching encoder base target.
        EncoderParameterMode mode = EncoderParameterMode::COUNT;
        REQUIRE(encoderBaseModeForRecordParam(button.param, mode));
        REQUIRE(mode == button.mode);
        REQUIRE(parameterForEncoderMode(button.mode) == button.param);
    }

    // Bits 6..8 are step/toggle controls, not recordable lanes: no encoder
    // base target, and rejection preserves the caller's mode.
    for (const uint8_t nonRecord : {kGateLength, kGate, kSlide})
    {
        CAPTURE(nonRecord);
        const auto param = static_cast<ParamId>(nonRecord);
        EncoderParameterMode mode = EncoderParameterMode::Velocity;
        REQUIRE_FALSE(encoderBaseModeForRecordParam(param, mode));
        REQUIRE(mode == EncoderParameterMode::Velocity);
        REQUIRE(parameterDefinition(param)->recordable == false);
    }
}

// ---------------------------------------------------------------------------
// Focus policy: recency, then latch fallback
// ---------------------------------------------------------------------------

TEST_CASE("Focus follows the most recent physical hold and falls back to the latch",
          "[parameter_focus]")
{
    UIState state;

    // Shift+tap latches Note; with no finger anywhere, focus is the latch.
    shiftLatchParam(state, kNote);
    REQUIRE(state.parameterLatch.latched() == static_cast<int8_t>(kNote));
    REQUIRE(state.parameterLatch.focus() == static_cast<int8_t>(kNote));
    REQUIRE(state.focusedParameter == static_cast<int8_t>(kNote));

    // Physically hold Filter (no Shift): the hold outranks the latch.
    holdParam(state, kFilter);
    REQUIRE(state.parameterLatch.focus() == static_cast<int8_t>(kFilter));
    REQUIRE(state.focusedParameter == static_cast<int8_t>(kFilter));

    // Release Filter: nothing physical is held any more, so the latch
    // (Note) becomes the focus again.
    releaseParam(state, kFilter);
    REQUIRE(state.parameterLatch.focus() == static_cast<int8_t>(kNote));

    // Hold Attack as well: with two physical holds, the most recent wins.
    holdParam(state, kFilter);
    holdParam(state, kAttack);
    REQUIRE(state.parameterLatch.focus() == static_cast<int8_t>(kAttack));

    // Release Attack: Filter is still physically held and takes focus back.
    releaseParam(state, kAttack);
    REQUIRE(state.parameterLatch.focus() == static_cast<int8_t>(kFilter));
    REQUIRE(state.parameterLatch.isMomentary(kFilter));

    // Release Filter: back to the Note latch, the last remaining source.
    releaseParam(state, kFilter);
    REQUIRE(state.parameterLatch.focus() == static_cast<int8_t>(kNote));
    REQUIRE_FALSE(state.parameterLatch.isMomentary(kFilter));
}

TEST_CASE("Armed lanes stay available to live recording while focus narrows",
          "[parameter_focus]")
{
    UIState state;

    // Note latched via Shift, Filter held live: both lanes are armed.
    shiftLatchParam(state, kNote);
    holdParam(state, kFilter);

    // applyTo() must expose BOTH lanes to the live recorder...
    REQUIRE(state.parameterButtonHeld[kNote]);  // latched, no finger
    REQUIRE(state.parameterButtonHeld[kFilter]); // momentary hold
    // ...while focus narrows to the newest hold alone.
    REQUIRE(state.parameterLatch.focus() == static_cast<int8_t>(kFilter));

    // Releasing Filter leaves the latched Note lane armed for recording.
    releaseParam(state, kFilter);
    REQUIRE(state.parameterButtonHeld[kNote]);
    REQUIRE_FALSE(state.parameterButtonHeld[kFilter]);
    REQUIRE(state.parameterLatch.focus() == static_cast<int8_t>(kNote));
}

TEST_CASE("Focus follows recency, not enum order", "[parameter_focus]")
{
    // Core regression: the old focus resolution returned the first armed
    // parameter in enum order, so a latched Note (ParamId 0) masked every
    // physically-held higher lane. Recency must win instead.
    UIState state;
    shiftLatchParam(state, kNote);    // latch Note (enum 0)...
    holdParam(state, kVelocity);      // ...then hold Velocity (enum 1)
    REQUIRE(state.parameterLatch.focus() == static_cast<int8_t>(kVelocity));
    REQUIRE(focusedParameterId(state) == ParamId::Velocity);

    // Mirror image: a HIGH-enum latch must not outrank a LOW-enum hold
    // either — the rule is physical hold beats latch, not bigger enum wins.
    UIState mirrored;
    shiftLatchParam(mirrored, kOctave); // latch Octave (enum 5)
    holdParam(mirrored, kNote);         // hold Note (enum 0)
    REQUIRE(mirrored.parameterLatch.focus() == static_cast<int8_t>(kNote));
    REQUIRE(focusedParameterId(mirrored) == ParamId::Note);

    // And among several holds, the newest press wins regardless of order
    // in the enum: Filter (2) held before Decay (4), Decay is focused.
    holdParam(mirrored, kFilter);
    holdParam(mirrored, kDecay);
    REQUIRE(mirrored.parameterLatch.focus() == static_cast<int8_t>(kDecay));
    releaseParam(mirrored, kDecay);
    REQUIRE(mirrored.parameterLatch.focus() == static_cast<int8_t>(kFilter));
}

// ---------------------------------------------------------------------------
// Transitions must not leave a latch that resurrects stale holds
// ---------------------------------------------------------------------------

TEST_CASE("Transition clears leave no latch that can resurrect stale holds",
          "[parameter_focus]")
{
    SECTION("Entering slide clears the state-owned latch and armed set")
    {
        UIState state;
        shiftLatchParam(state, kNote);
        holdParam(state, kFilter);
        REQUIRE(state.parameterButtonHeld[kNote]);
        REQUIRE(state.parameterButtonHeld[kFilter]);

        UITransitions::toggleSlide(state); // entering slide
        REQUIRE(state.slideMode);
        requireNoStaleHolds(state);
    }

    SECTION("Voice selection exits step editing; the paired focus clear owns the latch")
    {
        // Tile voice selection (UITransitions::selectPerformanceVoice) clears
        // step editing; every path that ends parameter-hold semantics pairs
        // it with the shared clearParameterFocus (gate-length entry,
        // UIEventHandler.cpp; voice-editor takeover, AlchemyControlBridge.cpp).
        UIState state;
        shiftLatchParam(state, kNote);
        holdParam(state, kFilter);
        state.selectedStepForEdit = 5;
        state.currentEditParameter = ParamId::Filter;

        REQUIRE(UITransitions::selectPerformanceVoice(state, 2));
        REQUIRE(state.selectedVoiceIndex == 2);
        REQUIRE(state.selectedStepForEdit == -1);
        REQUIRE(state.currentEditParameter == ParamId::Count);

        UITransitions::clearParameterFocus(state);
        requireNoStaleHolds(state);
    }

    SECTION("Opening settings clears step editing; the focus clear owns the latch")
    {
        // openSettings/closeSettings clear step edit state; the bridge pairs
        // the settings/editor takeover with clearParameterFocus so the
        // session's armed lanes cannot leak into the editor.
        UIState state;
        shiftLatchParam(state, kVelocity);
        holdParam(state, kOctave);
        state.selectedStepForEdit = 9;

        UITransitions::openSettings(state);
        REQUIRE(state.settingsMode);
        REQUIRE(state.isPresetSelection());
        REQUIRE(state.selectedStepForEdit == -1);
        UITransitions::clearParameterFocus(state);
        requireNoStaleHolds(state);
    }

    SECTION("Closing settings paired with the focus clear also leaves nothing")
    {
        UIState state;
        UITransitions::openSettings(state);
        shiftLatchParam(state, kAttack);
        REQUIRE(state.parameterButtonHeld[kAttack]);

        UITransitions::closeSettings(state);
        REQUIRE_FALSE(state.settingsMode);
        UITransitions::clearParameterFocus(state);
        requireNoStaleHolds(state);
    }

    SECTION("Explicit clearStepEdit + clearParameterFocus combo (mode-flip style)")
    {
        // Mode flips, gate-length entry and voice-editor takeover all route
        // through this pairing: drop the edit target AND reset the latch
        // through the same owner (UITransitions::clearParameterFocus).
        UIState state;
        shiftLatchParam(state, kDecay);
        holdParam(state, kGate); // momentary hold on a toggle lane
        REQUIRE(state.parameterButtonHeld[kDecay]);
        REQUIRE(state.parameterButtonHeld[kGate]);

        UITransitions::clearStepEdit(state);
        UITransitions::clearParameterFocus(state);
        requireNoStaleHolds(state);
    }
}

// ---------------------------------------------------------------------------
// Fallback chain when nothing is held or latched
// ---------------------------------------------------------------------------

TEST_CASE("Step edit target falls back through toggled parameter to encoder lane",
          "[parameter_focus]")
{
    // Nothing held (held == Count) and a toggled edit parameter set: the
    // toggled parameter wins.
    REQUIRE(stepEditParameter(ParamId::Count, ParamId::Filter,
                              EncoderParameterMode::Note) == ParamId::Filter);
    REQUIRE(stepEditParameter(ParamId::Count, ParamId::Gate,
                              EncoderParameterMode::Velocity) == ParamId::Gate);

    // Nothing held and nothing toggled: fall through to the encoder's lane.
    REQUIRE(stepEditParameter(ParamId::Count, ParamId::Count,
                              EncoderParameterMode::Decay) == ParamId::Decay);
    REQUIRE(stepEditParameter(ParamId::Count, ParamId::Count,
                              EncoderParameterMode::Attack) == ParamId::Attack);

    // Slide Time is a voice setting, not a step lane: the chain ends at
    // Count (no target).
    REQUIRE(stepEditParameter(ParamId::Count, ParamId::Count,
                              EncoderParameterMode::SlideTime) == ParamId::Count);
    REQUIRE(stepEditParameter(ParamId::Count, ParamId::Count,
                              EncoderParameterMode::COUNT) == ParamId::Count);

    // The held parameter outranks both fallbacks when it exists.
    REQUIRE(stepEditParameter(ParamId::Note, ParamId::Filter,
                              EncoderParameterMode::Decay) == ParamId::Note);
}
