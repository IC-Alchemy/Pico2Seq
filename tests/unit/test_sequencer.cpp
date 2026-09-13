#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sequencer/SequencerDefs.h"
#include "sequencer/Sequencer.h"
#include "voice/VoiceConfig.h"
#include "voice/VoiceEditParameters.h"

#include <algorithm>

// ─── ParameterTrack template ─────────────────────────────────────────────────

TEST_CASE("ParameterTrack initialises all steps to default value", "[paramtrack]") {
    ParameterTrack<16> track;
    track.init(0.5f);
    for (uint8_t i = 0; i < 16; ++i) {
        REQUIRE(track.getValue(i) == 0.5f);
    }
}

TEST_CASE("ParameterTrack setValue and getValue round-trip", "[paramtrack]") {
    ParameterTrack<16> track;
    track.init(0.0f);
    track.setValue(3, 0.75f);
    REQUIRE(track.getValue(3) == 0.75f);
}

TEST_CASE("ParameterTrack wraps step index at currentStepCount", "[paramtrack]") {
    ParameterTrack<16> track;
    track.init(0.0f);
    track.resize(4);
    track.setValue(0, 1.0f);
    // Step 4 should wrap to step 0
    REQUIRE(track.getValue(4) == 1.0f);
}

TEST_CASE("ParameterTrack resize extends with default values", "[paramtrack]") {
    ParameterTrack<16> track;
    track.init(0.2f);
    track.resize(4);
    track.resize(8);
    REQUIRE(track.stepCount() == 8);
    for (uint8_t i = 4; i < 8; ++i) {
        REQUIRE(track.getValue(i) == 0.2f);
    }
}

// ─── Filter randomization audibility (regression: narrowed dead zone) ────────
//
// Playback composes track values as ±0.5 modifiers around the per-preset base
// (VoiceEdit::composeLane): effective = clamp(laneBase + stored - 0.5, 0, 1).
// A standard voice uses filterCutoffBase = 0.37, so any stored value < 0.13
// clamps flat at 0 (inaudible dead zone) and the usable sweep width equals
// the stored subrange width. The random subrange must therefore stay inside
// [0.2, 0.8] — modifier-symmetric around 0.5 and dead-zone free.

namespace
{
constexpr float kFilterLaneBase = 0.37f; // VoiceConfig::filterCutoffBase default

float composeFilterEffective(float stored)
{
    VoiceConfig config{};           // standard voice: filterCutoffBase = 0.37f
    VoiceEdit::enablePatch(config); // playback path (usePatchBases = true)
    return VoiceEdit::composeLane(ParamId::Filter, stored, &config);
}
} // namespace

TEST_CASE("randomizeParameters Filter draws stay within the audible subrange [0.2, 0.8]", "[paramtrack][sequencer]") {
    ParameterManager pm;
    pm.init();
    const uint8_t steps = pm.getStepCount(ParamId::Filter);
    REQUIRE(steps > 0);

    float observedMin = 1.0f;
    float observedMax = 0.0f;
    // randomizeParameters() is time-seeded; enough draws make the range
    // assertions stable without depending on any particular seed.
    for (int round = 0; round < 16; ++round) {
        pm.randomizeParameters();
        for (uint8_t step = 0; step < steps; ++step) {
            const float value = pm.getValue(ParamId::Filter, step);
            observedMin = std::min(observedMin, value);
            observedMax = std::max(observedMax, value);
        }
    }

    // lcg_rand_float() never leaves [min, max], so after the fix the observed
    // range can only tighten inward; the epsilon only absorbs float rounding.
    constexpr float kEps = 1e-3f;
    REQUIRE(observedMin >= 0.2f - kEps);
    REQUIRE(observedMax <= 0.8f + kEps);
}

TEST_CASE("randomizeParameters Filter draws stay out of the standard-voice dead zone", "[paramtrack][sequencer]") {
    ParameterManager pm;
    pm.init();
    const uint8_t steps = pm.getStepCount(ParamId::Filter);
    REQUIRE(steps > 0);

    float effectiveMin = 1.0f;
    float effectiveMax = 0.0f;
    // A neutral 0.5 modifier must compose to exactly the standard lane base.
    REQUIRE(composeFilterEffective(0.5f) == Catch::Approx(kFilterLaneBase));
    for (int round = 0; round < 16; ++round) {
        pm.randomizeParameters();
        for (uint8_t step = 0; step < steps; ++step) {
            const float effective =
                composeFilterEffective(pm.getValue(ParamId::Filter, step));
            // Draws below the laneBase floor clamp flat at 0: no audible change.
            REQUIRE(effective > 0.0f);
            effectiveMin = std::min(effectiveMin, effective);
            effectiveMax = std::max(effectiveMax, effective);
        }
    }

    // The composed sweep across the legal subrange must stay audibly wide
    // (~4 octaves at env peak on standard voices), not collapse to a sliver.
    REQUIRE(effectiveMax - effectiveMin >= 0.5f);
}

TEST_CASE("Step filterCutoff default is the neutral modifier value 0.5", "[seqdefs]") {
    Step step{};
    // 0.5 composes to exactly the preset base (neutral); 0.35 previewed an
    // audibly darker filter than the preset actually sounds.
    REQUIRE(step.filterCutoff == 0.5f);
}

// ─── NoteDurationTracker ──────────────────────────────────────────────────────

TEST_CASE("NoteDurationTracker starts inactive", "[duration]") {
    NoteDurationTracker t;
    REQUIRE_FALSE(t.isActive());
}

TEST_CASE("NoteDurationTracker becomes active on start()", "[duration]") {
    NoteDurationTracker t;
    t.start(5);
    REQUIRE(t.isActive());
}

TEST_CASE("NoteDurationTracker deactivates after counter reaches zero", "[duration]") {
    NoteDurationTracker t;
    t.start(3);
    t.tick(); t.tick(); t.tick();
    REQUIRE_FALSE(t.isActive());
}

TEST_CASE("NoteDurationTracker reset() stops tracker immediately", "[duration]") {
    NoteDurationTracker t;
    t.start(100);
    t.reset();
    REQUIRE_FALSE(t.isActive());
}

// ─── EnvelopeController ───────────────────────────────────────────────────────

TEST_CASE("EnvelopeController starts released", "[envelope_ctrl]") {
    EnvelopeController ec;
    REQUIRE_FALSE(ec.isTriggered());
    REQUIRE(ec.isReleased());
}

TEST_CASE("EnvelopeController trigger/release cycle", "[envelope_ctrl]") {
    EnvelopeController ec;
    ec.trigger();
    REQUIRE(ec.isTriggered());
    REQUIRE_FALSE(ec.isReleased());
    ec.release();
    REQUIRE_FALSE(ec.isTriggered());
    REQUIRE(ec.isReleased());
}

// ─── SequencerDefs constants ──────────────────────────────────────────────────

TEST_CASE("Sequencer step count limits are sane", "[seqdefs]") {
    REQUIRE(SequencerConstants::MAX_STEPS_COUNT == 64);
    REQUIRE(SequencerConstants::MIN_STEPS_COUNT == 2);
    REQUIRE(SequencerConstants::DEFAULT_STEPS_COUNT == 16);
    REQUIRE(SequencerConstants::DEFAULT_STEPS_COUNT <= SequencerConstants::MAX_STEPS_COUNT);
}

TEST_CASE("PPQN constants derive correctly", "[seqdefs]") {
    REQUIRE(SequencerConstants::PULSES_PER_QUARTER_NOTE_PPQN == 480);
    REQUIRE(SequencerConstants::PULSES_PER_SEQUENCER_STEP_TICKS == 120);
}

// ─── Sequencer ────────────────────────────────────────────────────────────────

TEST_CASE("Sequencer default parameter step count is 16", "[sequencer]") {
    Sequencer seq(0);
    REQUIRE(seq.getParameterStepCount(ParamId::Note) == 16);
    REQUIRE(seq.getParameterStepCount(ParamId::Velocity) == 16);
    REQUIRE(seq.getParameterStepCount(ParamId::Filter) == 16);
}

TEST_CASE("Sequencer starts stopped", "[sequencer]") {
    Sequencer seq(0);
    REQUIRE_FALSE(seq.isRunning());
}

TEST_CASE("Sequencer start/stop", "[sequencer]") {
    Sequencer seq(0);
    seq.start();
    REQUIRE(seq.isRunning());
    seq.stop();
    REQUIRE_FALSE(seq.isRunning());
}

TEST_CASE("Sequencer toggleStep sets Gate parameter to 1", "[sequencer]") {
    Sequencer seq(0);
    seq.toggleStep(0);
    REQUIRE(seq.getStepParameterValue(ParamId::Gate, 0) == 1.0f);
}

TEST_CASE("Sequencer toggleStep on already-set step clears it", "[sequencer]") {
    Sequencer seq(0);
    seq.toggleStep(0);
    seq.toggleStep(0);
    REQUIRE(seq.getStepParameterValue(ParamId::Gate, 0) == 0.0f);
}

TEST_CASE("Sequencer setStepParameterValue persists", "[sequencer]") {
    Sequencer seq(0);
    seq.setStepParameterValue(ParamId::Velocity, 5, 0.8f);
    REQUIRE(seq.getStepParameterValue(ParamId::Velocity, 5) == 0.8f);
}

TEST_CASE("Note track supports a three-octave chromatic range", "[sequencer]") {
    Sequencer seq(0);
    seq.toggleStep(0); // Note programming only applies to gated steps.

    seq.setStepParameterValue(ParamId::Note, 0, 36.0f);
    REQUIRE(seq.getStepParameterValue(ParamId::Note, 0) == 36.0f);

    seq.setStepParameterValue(ParamId::Note, 0, 37.0f);
    REQUIRE(seq.getStepParameterValue(ParamId::Note, 0) == 36.0f);

    seq.setStepParameterValue(ParamId::Note, 0, 35.5f);
    REQUIRE(seq.getStepParameterValue(ParamId::Note, 0) == 36.0f);
}

TEST_CASE("Octave track emits signed semitone transposes", "[sequencer]") {
    Sequencer seq(0);
    VoiceState state;
    seq.toggleStep(0);

    seq.setStepParameterValue(ParamId::Octave, 0, 0.0f);
    seq.playStepNow(0, &state);
    REQUIRE(state.octaveOffset == -12);

    seq.setStepParameterValue(ParamId::Octave, 0, 0.5f);
    seq.playStepNow(0, &state);
    REQUIRE(state.octaveOffset == 0);

    seq.setStepParameterValue(ParamId::Octave, 0, 1.0f);
    seq.playStepNow(0, &state);
    REQUIRE(state.octaveOffset == 12);
}

TEST_CASE("Sequencer setParameterStepCount changes count", "[sequencer]") {
    Sequencer seq(0);
    seq.setParameterStepCount(ParamId::Note, 8);
    REQUIRE(seq.getParameterStepCount(ParamId::Note) == 8);
}

TEST_CASE("Sequencer isNotePlaying is false after construction", "[sequencer]") {
    Sequencer seq(0);
    REQUIRE_FALSE(seq.isNotePlaying());
}

TEST_CASE("Gate length expires mid-step and reports the note-off tick", "[sequencer]") {
    Sequencer seq(0);
    seq.start();
    seq.toggleStep(0); // gate the step
    // 0.25 of a 120-tick step = 30 ticks of gate length.
    seq.setStepParameterValue(ParamId::GateLength, 0, 0.25f);

    VoiceState state;
    seq.playStepNow(0, &state);
    REQUIRE(state.isGateHigh);
    REQUIRE(seq.isNotePlaying());

    bool expired = false;
    for (int i = 0; i < 29 && !expired; ++i)
        expired = seq.tickNoteDuration(&state);
    REQUIRE_FALSE(expired);       // gate length not elapsed yet
    REQUIRE(state.isGateHigh);    // audio gate still high
    REQUIRE(seq.isNotePlaying());

    REQUIRE(seq.tickNoteDuration(&state)); // expiry fires exactly here
    REQUIRE_FALSE(state.isGateHigh);       // and the note-off is visible
    REQUIRE_FALSE(seq.isNotePlaying());

    REQUIRE_FALSE(seq.tickNoteDuration(&state)); // no double note-off
}

TEST_CASE("Sequencer getCurrentStep returns 0 after construction", "[sequencer]") {
    Sequencer seq(0);
    REQUIRE(seq.getCurrentStep() == 0);
}

// ─── advanceStep against the global uClock counter ───────────────────────────

TEST_CASE("advanceStep phase follows global step modulo across the 8-bit wrap", "[sequencer]") {
    Sequencer seq(0);
    seq.start();
    seq.setParameterStepCount(ParamId::Gate, 12);
    seq.setParameterStepCount(ParamId::Note, 12);
    VoiceState voiceState;
    for (uint32_t step = 0; step <= 512; ++step) {
        seq.advanceStep(step, -1, false, false, false, false, false, false, -1, &voiceState);
        // 256 % 12 == 4: a counter truncated to uint8_t before the modulo would
        // land on 0 here instead.
        REQUIRE(seq.getCurrentStep() == step % 12);
        REQUIRE(seq.getCurrentStepForParameter(ParamId::Note) == step % 12);
    }
}

TEST_CASE("Default sequencer starts at neutral octave and half-step gate", "[sequencer]") {
    Sequencer seq;
    const Step defaults = seq.getPlaybackStep(0);
    REQUIRE(defaults.octaveOffset == 0);
    REQUIRE(defaults.gateLengthTicks == 60);
    REQUIRE_FALSE(defaults.isGateActive);
    seq.randomizeParameters();
    REQUIRE(seq.getPlaybackStep(0).octaveOffset == 0);
}

// ─── Regression Tests for Parameter Writing, Reading, and Editing ─────────────

TEST_CASE("Sequencer::resetAllSteps resets Note track even when gate is low", "[sequencer]") {
    Sequencer seq(0);
    // Directly set note on an ungated step (Gate defaults to 0.0f)
    seq.setStepParameterValue(ParamId::Note, 5, 24.0f);
    REQUIRE(seq.getStepParameterValue(ParamId::Note, 5) == 24.0f);
    REQUIRE(seq.getStepParameterValue(ParamId::Gate, 5) == 0.0f);

    seq.resetAllSteps();
    // After resetAllSteps, Note on step 5 should be reset to default (0.0f)
    REQUIRE(seq.getStepParameterValue(ParamId::Note, 5) == 0.0f);
}

TEST_CASE("ParameterManager::randomizeParameters produces only integer values for Note", "[paramtrack][sequencer]") {
    ParameterManager pm;
    pm.init();
    const uint8_t steps = pm.getStepCount(ParamId::Note);
    REQUIRE(steps > 0);

    for (int round = 0; round < 16; ++round) {
        pm.randomizeParameters(false);
        for (uint8_t step = 0; step < steps; ++step) {
            float val = pm.getValue(ParamId::Note, step);
            REQUIRE(val == std::floor(val));
            REQUIRE(val >= 0.0f);
            REQUIRE(val <= 36.0f);
        }
    }
}

TEST_CASE("ParameterManager bounds-checks ParamId on read and write", "[paramtrack]") {
    ParameterManager pm;
    pm.init();
    // ParamId::Count is past valid range
    REQUIRE(pm.getStepCount(ParamId::Count) == 0);
    REQUIRE(pm.getValue(ParamId::Count, 0) == 0.0f);

    // Write attempts should gracefully no-op without crashing or buffer overrun
    pm.setStepCount(ParamId::Count, 8);
    pm.setValue(ParamId::Count, 0, 1.0f);
}

TEST_CASE("Sequencer::processStep clamps negative notes to 0", "[sequencer]") {
    Sequencer seq(0);
    seq.setPlaybackTransform(
        [](ParamId, float val, const void *) { return val; },
        nullptr,
        [](float) -> int8_t { return -24; }); // -2 octaves = -24 semitones
    seq.setStepParameterValue(ParamId::Note, 0, 0.0f);
    seq.setStepParameterValue(ParamId::Gate, 0, 1.0f);

    VoiceState state;
    seq.playStepNow(0, &state);
    // Note value should be clamped, not wrapped to 232
    REQUIRE(state.isGateHigh);
    REQUIRE(seq.getCurrentNote() >= 0);
    REQUIRE(seq.getCurrentNote() == 0);
}

TEST_CASE("Sequencer::processStep triggers envelope on initial slide note", "[sequencer]") {
    Sequencer seq(0);
    seq.setStepParameterValue(ParamId::Gate, 0, 1.0f);
    seq.setStepParameterValue(ParamId::Slide, 0, 1.0f); // Slide on step 0 while no note is active

    VoiceState state;
    seq.playStepNow(0, &state);
    // Must retrigger envelope because no note was previously sounding
    REQUIRE(state.shouldRetrigger);
    REQUIRE(state.isGateHigh);
    REQUIRE(seq.isNotePlaying());
}

TEST_CASE("Polyrhythmic advanceStep checks sounding gate step for Note recording", "[sequencer]") {
    Sequencer seq(0);
    seq.start();
    // Set Note track to 3 steps, Gate track to 2 steps
    seq.setParameterStepCount(ParamId::Note, 3);
    seq.setParameterStepCount(ParamId::Gate, 2);

    // Gate step 0 is HIGH (1.0f), Gate step 1 is LOW (0.0f)
    seq.setStepParameterValue(ParamId::Gate, 0, 1.0f);
    seq.setStepParameterValue(ParamId::Gate, 1, 0.0f);

    VoiceState state;
    // Step 0: Note cursor 0, Gate cursor 0 (Gate is HIGH)
    // Note button held with recording distance 100mm -> normalized distance > 0
    seq.advanceStep(0, 100, true, false, false, false, false, false, -1, &state);
    REQUIRE(seq.getStepParameterValue(ParamId::Note, 0) > 0.0f);

    // Step 1: Note cursor 1, Gate cursor 1 (Gate is LOW)
    // Reset step 1 note to 0 first
    seq.setStepParameterValue(ParamId::Note, 1, 0.0f);
    seq.advanceStep(1, 100, true, false, false, false, false, false, -1, &state);
    // Gate is LOW at Gate step 1, so Note on step 1 must NOT be recorded
    REQUIRE(seq.getStepParameterValue(ParamId::Note, 1) == 0.0f);
}

TEST_CASE("Sequencer::getStep reflects configured octaveMapper", "[sequencer]") {
    Sequencer seq(0);
    seq.setPlaybackTransform(
        [](ParamId, float val, const void *) { return val; },
        nullptr,
        [](float val) -> int8_t {
            return val > 0.75f ? 24 : (val < 0.25f ? -24 : 0);
        });
    seq.setStepParameterValue(ParamId::Octave, 0, 1.0f);
    Step s = seq.getStep(0);
    REQUIRE(s.octaveOffset == 24);

    seq.setStepParameterValue(ParamId::Octave, 0, 0.0f);
    s = seq.getStep(0);
    REQUIRE(s.octaveOffset == -24);
}

TEST_CASE("previewActiveStep preserves independent polyrhythmic parameter cursors", "[sequencer]") {
    Sequencer seq(0);
    seq.start();
    seq.setParameterStepCount(ParamId::Note, 16);
    seq.setParameterStepCount(ParamId::Filter, 5);

    VoiceState state;
    // Advance 7 steps
    for (uint32_t i = 0; i <= 7; ++i) {
        seq.advanceStep(i, -1, false, false, false, false, false, false, -1, &state);
    }
    REQUIRE(seq.getCurrentStepForParameter(ParamId::Note) == 7 % 16);
    REQUIRE(seq.getCurrentStepForParameter(ParamId::Filter) == 7 % 5);

    // previewActiveStep should evaluate at the current cursors (7 and 2) without resetting them
    VoiceState previewState;
    seq.previewActiveStep(&previewState);
    REQUIRE(seq.getCurrentStepForParameter(ParamId::Note) == 7);
    REQUIRE(seq.getCurrentStepForParameter(ParamId::Filter) == 2);
}

