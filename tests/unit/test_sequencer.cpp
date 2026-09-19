#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sequencer/SequencerDefs.h"
#include "sequencer/Sequencer.h"
#include "voice/VoiceConfig.h"
#include "voice/VoiceEditParameters.h"
#include "voice/VoiceSystem.h"
#include "app/VoicePublication.h"

#include <array>
#include <cmath>

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

// ─── Randomization depth around the patch base ──────────────────────────────
//
// Playback composes track values as modifiers around the per-preset base
// (VoiceEdit::composeLane): 0.5 plays the base exactly, 0 and 1 reach the
// lane's ends, and each half is linear in between. The randomizer draws
// triangular offsets of at most depth/2 around 0.5, so depth D moves a step at
// most D% of the way from its base toward either end of the lane.

namespace
{
constexpr float kFilterLaneBase = 0.37f; // VoiceConfig::filterCutoffBase default

float composeFilterEffective(float stored)
{
    VoiceConfig config{};           // standard voice: filterCutoffBase = 0.37f
    VoiceEdit::enablePatch(config); // playback path (usePatchBases = true)
    return VoiceEdit::composeLane(ParamId::Filter, stored, &config);
}

struct Sweep { float minimum = 1.0f, maximum = 0.0f; };

Sweep composedFilterSweep(uint8_t depth, uint64_t seed)
{
    ParameterManager pm;
    pm.init();
    pm.randomizeParameters(depth, seed);
    Sweep sweep;
    for (uint8_t step = 0; step < pm.getStepCount(ParamId::Filter); ++step) {
        const float effective = composeFilterEffective(pm.getValue(ParamId::Filter, step));
        sweep.minimum = std::min(sweep.minimum, effective);
        sweep.maximum = std::max(sweep.maximum, effective);
    }
    return sweep;
}
} // namespace

TEST_CASE("randomizeParameters keeps Filter offsets inside the default depth radius", "[paramtrack][sequencer]") {
    ParameterManager pm;
    pm.init();
    const uint8_t steps = pm.getStepCount(ParamId::Filter);
    REQUIRE(steps > 0);
    const float radius = ParameterManager::kDefaultRandomizeDepth / 200.0f;
    // The default call is clock-seeded; the radius holds for any seed.
    for (int round = 0; round < 16; ++round) {
        pm.randomizeParameters();
        for (uint8_t step = 0; step < steps; ++step)
            REQUIRE(std::abs(pm.getValue(ParamId::Filter, step) - 0.5f) <= radius + 1e-5f);
    }
}

TEST_CASE("randomizeParameters composes a sweep that widens with depth and never goes dead", "[paramtrack][sequencer]") {
    // A neutral 0.5 modifier must compose to exactly the standard lane base.
    REQUIRE(composeFilterEffective(0.5f) == Catch::Approx(kFilterLaneBase));
    for (uint64_t seed : {7ull, 99ull, 2026ull}) {
        INFO("seed " << seed);
        const Sweep subtle = composedFilterSweep(10, seed);
        const Sweep adventurous = composedFilterSweep(60, seed);
        REQUIRE(adventurous.maximum - adventurous.minimum > subtle.maximum - subtle.minimum);
        // Depth 60 reaches at most 60% of the way to either end: never a flat zero.
        REQUIRE(adventurous.minimum >= kFilterLaneBase * 0.4f - 1e-4f);
        REQUIRE(adventurous.maximum <= kFilterLaneBase + 0.6f * (1.0f - kFilterLaneBase) + 1e-4f);
        REQUIRE(adventurous.minimum > 0.0f);
    }
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

// ─── clearPattern: full voice wipe (values, gates, track lengths) ────────────

TEST_CASE("Sequencer::clearPattern wipes every slot and restores default track lengths", "[sequencer]") {
    Sequencer seq(0);
    // Plant custom data across the full capacity while the tracks are long,
    // then shrink so the leftovers live beyond the active length.
    for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step) {
        seq.setStepParameterValue(ParamId::Gate, step, 1.0f);
        seq.setStepParameterValue(ParamId::Slide, step, 1.0f);
        seq.setStepParameterValue(ParamId::Velocity, step, 0.9f);
        seq.setStepParameterValue(ParamId::Attack, step, 0.8f);
        seq.setStepParameterValue(ParamId::Note, step, 30.0f);
    }
    seq.setParameterStepCount(ParamId::Gate, 7);
    seq.setParameterStepCount(ParamId::Velocity, 5);

    seq.clearPattern();

    // No gates, no slides, neutral values -- across the whole 64-slot
    // capacity, not just the active length, so later track growth cannot
    // resurrect pre-clear data.
    for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step) {
        REQUIRE(seq.getRawStepValue(ParamId::Gate, step) == 0.0f);
        REQUIRE(seq.getRawStepValue(ParamId::Slide, step) == 0.0f);
        REQUIRE(seq.getRawStepValue(ParamId::Velocity, step) == Catch::Approx(0.5f));
        REQUIRE(seq.getRawStepValue(ParamId::Attack, step) == Catch::Approx(0.01f));
        REQUIRE(seq.getRawStepValue(ParamId::Note, step) == Catch::Approx(0.0f));
    }
    for (uint8_t param = 0; param < PARAM_ID_COUNT; ++param) {
        REQUIRE(seq.getParameterStepCount(static_cast<ParamId>(param)) ==
                SequencerConstants::DEFAULT_STEPS_COUNT);
    }
}

TEST_CASE("Sequencer::clearPattern neutralizes modifier steps in patch mode", "[sequencer]") {
    Sequencer seq(0);
    seq.setPlaybackTransform(
        [](ParamId, float stored, const void *) { return stored; }, nullptr);
    REQUIRE(seq.usesPlaybackTransform());

    for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step) {
        seq.setStepParameterValue(ParamId::Gate, step, 1.0f);
        seq.setStepParameterValue(ParamId::Velocity, step, 0.9f);
    }

    seq.clearPattern();

    // Patch mode plays stored values as modifiers around the preset base:
    // cleared steps must sit at the neutral midpoint with gates off.
    for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step) {
        REQUIRE(seq.getRawStepValue(ParamId::Gate, step) == 0.0f);
        REQUIRE(seq.getRawStepValue(ParamId::Velocity, step) == Catch::Approx(0.5f));
    }
    REQUIRE(seq.getParameterStepCount(ParamId::Gate) == SequencerConstants::DEFAULT_STEPS_COUNT);
}

TEST_CASE("Sequencer::clearPattern releases a sounding note", "[sequencer]") {
    Sequencer seq(0);
    seq.setStepParameterValue(ParamId::Gate, 0, 1.0f);
    seq.startNote(60, 100, 480);
    REQUIRE(seq.isNotePlaying());

    seq.clearPattern();

    REQUIRE_FALSE(seq.isNotePlaying());
}

TEST_CASE("ParameterManager::randomizeParameters produces only integer values for Note", "[paramtrack][sequencer]") {
    ParameterManager pm;
    pm.init();
    const uint8_t steps = pm.getStepCount(ParamId::Note);
    REQUIRE(steps > 0);

    for (int round = 0; round < 16; ++round) {
        pm.randomizeParameters(round % 2 ? 100 : 0);
        for (uint8_t step = 0; step < steps; ++step) {
            float val = pm.getValue(ParamId::Note, step);
            REQUIRE(val == std::floor(val));
            REQUIRE(val >= 0.0f);
            REQUIRE(val <= 12.0f); // a compact run of scale steps at any depth
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

TEST_CASE("Sequencer::setStep and copyStep duplicate all step parameters", "[sequencer]") {
    Sequencer seq(0);

    Step sourceStep;
    sourceStep.noteIndex = 19.0f;
    sourceStep.velocityLevel = 0.85f;
    sourceStep.filterCutoff = 0.72f;
    sourceStep.attackTimeSeconds = 0.05f;
    sourceStep.decayTimeSeconds = 0.45f;
    sourceStep.octaveOffset = 12;
    sourceStep.gateLengthTicks = 90;
    sourceStep.isGateActive = true;
    sourceStep.hasSlide = true;

    seq.setStep(3, sourceStep);

    Step readBack = seq.getStep(3);
    REQUIRE(readBack.noteIndex == 19.0f);
    REQUIRE(readBack.velocityLevel == Catch::Approx(0.85f));
    REQUIRE(readBack.filterCutoff == Catch::Approx(0.72f));
    REQUIRE(readBack.attackTimeSeconds == Catch::Approx(0.05f));
    REQUIRE(readBack.decayTimeSeconds == Catch::Approx(0.45f));
    REQUIRE(readBack.octaveOffset == 12);
    REQUIRE(readBack.isGateActive == true);
    REQUIRE(readBack.hasSlide == true);
    REQUIRE(readBack.gateLengthTicks == 90);

    // Copy step 3 to step 11
    seq.copyStep(3, 11);
    Step copied = seq.getStep(11);
    REQUIRE(copied.noteIndex == 19.0f);
    REQUIRE(copied.velocityLevel == Catch::Approx(0.85f));
    REQUIRE(copied.filterCutoff == Catch::Approx(0.72f));
    REQUIRE(copied.attackTimeSeconds == Catch::Approx(0.05f));
    REQUIRE(copied.decayTimeSeconds == Catch::Approx(0.45f));
    REQUIRE(copied.octaveOffset == 12);
    REQUIRE(copied.isGateActive == true);
    REQUIRE(copied.hasSlide == true);
    REQUIRE(copied.gateLengthTicks == 90);

    // Bounds checking
    seq.copyStep(64, 0); // Out of bounds source should gracefully no-op
    seq.copyStep(0, 64); // Out of bounds dest should gracefully no-op
    Step s0 = seq.getStep(0);
    REQUIRE(s0.noteIndex == 0.0f); // Untouched
}

TEST_CASE("ParameterManager::copyStep copies values across tracks and bounds-checks", "[paramtrack][sequencer]") {
    ParameterManager pm;
    pm.init();

    pm.setValue(ParamId::Note, 2, 28.0f);
    pm.setValue(ParamId::Filter, 2, 0.9f);
    pm.setValue(ParamId::Gate, 2, 1.0f);

    pm.copyStep(2, 7);

    REQUIRE(pm.getValue(ParamId::Note, 7) == 28.0f);
    REQUIRE(pm.getValue(ParamId::Filter, 7) == Catch::Approx(0.9f));
    REQUIRE(pm.getValue(ParamId::Gate, 7) == 1.0f);

    // Out of bounds copy should no-op
    pm.copyStep(100, 0);
    pm.copyStep(0, 100);
}

namespace {
struct PublicationRig {
    VoiceSystem voices;
    VoiceManager manager{VoiceSystem::MAX_VOICES};
    std::array<Sequencer, VoiceSystem::MAX_VOICES> sequencers{
        Sequencer{0}, Sequencer{1}, Sequencer{2}, Sequencer{3}};
    std::array<unsigned, VoiceSystem::MAX_VOICES> publications{};
    std::array<VoiceState, VoiceSystem::MAX_VOICES> lastPublished{};

    PublicationRig() {
        VoiceConfig config;
        config.oscillatorCount = 1;
        config.oscWaveforms[0] = WAVE_SIN;
        config.hasFilter = false;
        for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i) {
            voices.setVoiceId(i, manager.addVoice(config));
            sequencers[i].setStepParameterValue(ParamId::Gate, 0, 1.0f);
            sequencers[i].setStepParameterValue(ParamId::GateLength, 0, (i + 1) * 0.25f);
            sequencers[i].start();
        }
        manager.init(48000);
        // Observe the actual manager handoff, including the transient event.
        manager.setVoiceUpdateCallback([this](uint8_t id, const VoiceState &state) {
            for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i) {
                if (voices.getVoiceId(i) == id) {
                    ++publications[i];
                    lastPublished[i] = state;
                }
            }
        });
    }

    void advance(uint8_t i, uint32_t step) {
        VoiceState state = voices.getVoiceState(i);
        sequencers[i].advanceStep(step, -1, false, false, false, false, false, false, -1, &state);
        REQUIRE(publishVoiceState(voices, manager, i, state));
    }

    void tick() {
        for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i)
            tickSequencerVoice(sequencers[i], voices, manager, i);
        REQUIRE(std::isfinite(manager.processAllVoices()));
    }
};
}

TEST_CASE("Voice publication delivers four independent sequencer expiries", "[sequencer][voice][voicesystem][voice_publication]") {
    PublicationRig rig;
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i) {
        rig.advance(i, 0);
        REQUIRE(rig.lastPublished[i].isGateHigh);
        REQUIRE(rig.lastPublished[i].shouldRetrigger);
        REQUIRE_FALSE(rig.voices.getVoiceState(i).shouldRetrigger);
    }

    // No step boundary here: note-offs must arrive at their own expiry ticks.
    for (unsigned tick = 1; tick <= 121; ++tick) {
        rig.tick();
        for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i) {
            const bool sounding = tick < (i + 1u) * 30u;
            CAPTURE(tick, i);
            CHECK(rig.voices.getVoiceState(i).isGateHigh == sounding);
            CHECK(rig.sequencers[i].isNotePlaying() == sounding);
            CHECK(rig.lastPublished[i].isGateHigh == sounding);
            CHECK(rig.publications[i] == (sounding ? 1u : 2u));
            const VoiceState *requested = rig.manager.getVoiceState(rig.voices.getVoiceId(i));
            REQUIRE(requested != nullptr);
            CHECK(requested->isGateHigh == sounding);
            if (!sounding) CHECK_FALSE(rig.lastPublished[i].shouldRetrigger);
        }
    }
}

TEST_CASE("Voice publication consumes retrigger once across live refresh and gate off", "[sequencer][voice_publication]") {
    PublicationRig rig;
    constexpr uint8_t i = 3; // Not a former MIDI voice.
    auto &seq = rig.sequencers[i];
    auto &state = rig.voices.getVoiceState(i);
    rig.advance(i, 0);
    REQUIRE(rig.lastPublished[i].shouldRetrigger);
    REQUIRE_FALSE(state.shouldRetrigger);

    // Aliasing the stored state must not replay the step event.
    REQUIRE(publishVoiceState(rig.voices, rig.manager, i, state));
    CHECK_FALSE(rig.lastPublished[i].shouldRetrigger);
    seq.setStepParameterValue(ParamId::Note, 0, 9.0f);
    seq.setStepParameterValue(ParamId::Filter, 0, 0.2f);
    seq.refreshVoiceParameters(&state);
    REQUIRE(publishVoiceState(rig.voices, rig.manager, i, state));
    CHECK(rig.lastPublished[i].noteIndex == 9.0f);
    CHECK(rig.lastPublished[i].filterCutoff == Catch::Approx(0.2f));
    CHECK(rig.lastPublished[i].isGateHigh);
    CHECK_FALSE(rig.lastPublished[i].shouldRetrigger);
    CHECK(rig.publications[i] == 3);

    // A gate-off step publishes once, retains release pitch and cancels expiry.
    seq.setStepParameterValue(ParamId::Gate, 1, 0.0f);
    rig.advance(i, 1);
    CHECK_FALSE(rig.lastPublished[i].isGateHigh);
    CHECK_FALSE(rig.lastPublished[i].shouldRetrigger);
    CHECK(rig.lastPublished[i].noteIndex == 9.0f);
    CHECK_FALSE(seq.isNotePlaying());
    for (unsigned tick = 0; tick < 121; ++tick) rig.tick();
    CHECK(rig.publications[i] == 4);
    seq.refreshVoiceParameters(&state);
    REQUIRE(publishVoiceState(rig.voices, rig.manager, i, state));
    CHECK_FALSE(rig.lastPublished[i].isGateHigh);
    CHECK_FALSE(rig.lastPublished[i].shouldRetrigger);

    // A fresh gate is a new one-shot event, even at the same pitch.
    rig.advance(i, 0);
    CHECK(rig.lastPublished[i].isGateHigh);
    CHECK(rig.lastPublished[i].shouldRetrigger);
    CHECK_FALSE(state.shouldRetrigger);
}

TEST_CASE("Voice publication preserves slide duration and non-retrigger semantics", "[sequencer][voice_publication]") {
    PublicationRig rig;
    constexpr uint8_t i = 2;
    auto &seq = rig.sequencers[i];
    seq.setStepParameterValue(ParamId::Gate, 1, 1.0f);
    seq.setStepParameterValue(ParamId::Slide, 1, 1.0f);
    seq.setStepParameterValue(ParamId::Note, 1, 12.0f);
    seq.setStepParameterValue(ParamId::GateLength, 1, 0.25f);
    rig.advance(i, 0);
    for (unsigned tick = 0; tick < 10; ++tick) rig.tick();
    rig.advance(i, 1);
    CHECK(rig.lastPublished[i].isGateHigh);
    CHECK(rig.lastPublished[i].hasSlide);
    CHECK(rig.lastPublished[i].noteIndex == 12.0f);
    CHECK_FALSE(rig.lastPublished[i].shouldRetrigger);
    for (unsigned tick = 0; tick < 29; ++tick) rig.tick();
    CHECK(rig.publications[i] == 2);
    rig.tick();
    CHECK(rig.publications[i] == 3);
    CHECK_FALSE(rig.lastPublished[i].isGateHigh);
    CHECK_FALSE(rig.lastPublished[i].shouldRetrigger);
}

TEST_CASE("Stopping all four sequencer voices releases notes and restart retriggers slides", "[sequencer][voice_publication]") {
    PublicationRig rig;
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i) {
        rig.sequencers[i].setStepParameterValue(ParamId::Slide, 0, 1.0f);
        rig.advance(i, 0);
        stopSequencerVoice(rig.sequencers[i], rig.voices, rig.manager, i);
        CHECK_FALSE(rig.sequencers[i].isNotePlaying());
        CHECK_FALSE(rig.lastPublished[i].isGateHigh);
        CHECK_FALSE(rig.lastPublished[i].shouldRetrigger);
        CHECK_FALSE(rig.lastPublished[i].hasSlide);
        CHECK(rig.publications[i] == 2);
    }
    for (unsigned tick = 0; tick < 121; ++tick) rig.tick();
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i) {
        CHECK(rig.publications[i] == 2); // No stale expiry after stop.
        CHECK(rig.sequencers[i].getStepParameterValue(ParamId::Slide, 0) == 1.0f);
        rig.sequencers[i].start();
        rig.advance(i, 0);
        CHECK(rig.lastPublished[i].isGateHigh);
        CHECK(rig.lastPublished[i].hasSlide);
        CHECK(rig.lastPublished[i].shouldRetrigger);
        CHECK_FALSE(rig.voices.getVoiceState(i).shouldRetrigger);
    }
}

TEST_CASE("Voice publication rejects invalid routing without changing requested state", "[voice_publication]") {
    PublicationRig rig;
    VoiceState event;
    event.isGateHigh = true;
    event.shouldRetrigger = true;
    CHECK_FALSE(publishVoiceState(rig.voices, rig.manager, VoiceSystem::MAX_VOICES, event));
    rig.voices.setVoiceId(0, UINT8_MAX);
    CHECK_FALSE(publishVoiceState(rig.voices, rig.manager, 0, event));
    CHECK_FALSE(rig.voices.getVoiceState(0).isGateHigh);
    for (auto count : rig.publications) CHECK(count == 0);
}

TEST_CASE("CORE_PARAMETERS metadata defines valid bounds and types for all parameters", "[seqdefs]") {
    for (size_t i = 0; i < PARAM_ID_COUNT; ++i) {
        const auto &def = CORE_PARAMETERS[i];
        REQUIRE(def.name != nullptr);
        float minVal = parameterValueAsFloat(def.minValue);
        float maxVal = parameterValueAsFloat(def.maxValue);
        float defVal = parameterValueAsFloat(def.defaultValue);
        REQUIRE(minVal <= maxVal);
        REQUIRE(defVal >= minVal);
        REQUIRE(defVal <= maxVal);
        REQUIRE(def.defaultSteps >= SequencerConstants::MIN_STEPS_COUNT);
        REQUIRE(def.defaultSteps <= SequencerConstants::MAX_STEPS_COUNT);
    }
}


TEST_CASE("refreshVoiceParameters updates a sounding voice without retriggering", "[sequencer]") {
    Sequencer seq(0);
    seq.setStepParameterValue(ParamId::Gate, 0, 1.0f);
    seq.setStepParameterValue(ParamId::Note, 0, 7.0f);
    seq.start();

    VoiceState state;
    seq.advanceStep(0, -1, false, false, false, false, false, false, -1, &state);
    REQUIRE(state.isGateHigh);
    REQUIRE(state.shouldRetrigger);
    const bool wasPlaying = seq.isNotePlaying();

    // A live edit of the playing step: new values, same note lifecycle.
    seq.setStepParameterValue(ParamId::Velocity, 0, 0.9f);
    seq.setStepParameterValue(ParamId::Filter, 0, 0.2f);
    seq.setStepParameterValue(ParamId::Note, 0, 9.0f);
    seq.refreshVoiceParameters(&state);
    CHECK(state.velocityLevel == Catch::Approx(0.9f));
    CHECK(state.filterCutoff == Catch::Approx(0.2f));
    CHECK(state.noteIndex == 9.0f);
    CHECK(state.isGateHigh);
    CHECK_FALSE(state.shouldRetrigger);
    CHECK(seq.isNotePlaying() == wasPlaying);

    // A released note keeps its pitch through the tail.
    state.isGateHigh = false;
    seq.setStepParameterValue(ParamId::Note, 0, 3.0f);
    seq.refreshVoiceParameters(&state);
    CHECK(state.noteIndex == 9.0f);
    CHECK_FALSE(state.isGateHigh);

    seq.refreshVoiceParameters(nullptr); // tolerated
}
