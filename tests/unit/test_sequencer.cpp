#include <catch2/catch_test_macros.hpp>

#include "sequencer/SequencerDefs.h"
#include "sequencer/Sequencer.h"

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
    track.currentStepCount = 4;
    track.setValue(0, 1.0f);
    // Step 4 should wrap to step 0
    REQUIRE(track.getValue(4) == 1.0f);
}

TEST_CASE("ParameterTrack resize extends with default values", "[paramtrack]") {
    ParameterTrack<16> track;
    track.init(0.2f);
    track.currentStepCount = 4;
    track.resize(8);
    REQUIRE(track.currentStepCount == 8);
    for (uint8_t i = 4; i < 8; ++i) {
        REQUIRE(track.getValue(i) == 0.2f);
    }
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

TEST_CASE("Sequencer setParameterStepCount changes count", "[sequencer]") {
    Sequencer seq(0);
    seq.setParameterStepCount(ParamId::Note, 8);
    REQUIRE(seq.getParameterStepCount(ParamId::Note) == 8);
}

TEST_CASE("Sequencer isNotePlaying is false after construction", "[sequencer]") {
    Sequencer seq(0);
    REQUIRE_FALSE(seq.isNotePlaying());
}

TEST_CASE("Sequencer getCurrentStep returns 0 after construction", "[sequencer]") {
    Sequencer seq(0);
    REQUIRE(seq.getCurrentStep() == 0);
}
