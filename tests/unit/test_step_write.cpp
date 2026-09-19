// Regression suite for the one shared stored-lane write,
// Sequencer::writeStepParameter(): validation, Normalized01 vs LaneValue
// domains, post-rounding change detection, gate-protected Note recording and
// live per-lane-cursor recording through advanceStep().

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sequencer/SequencerDefs.h"
#include "sequencer/Sequencer.h"

#include <cmath>
#include <limits>

using Catch::Approx;

namespace
{
// What Normalized01 input must become in the lane's storage domain: the shared
// mapping, followed by the ParameterManager's rounding of integer-min lanes
// (Note). Continuous lanes store the mapped value untouched.
float expectedStored(ParamId id, float normalized)
{
    float mapped = mapNormalizedValueToParamRange(id, normalized);
    if (id == ParamId::Note)
        mapped = std::round(mapped);
    return mapped;
}
} // namespace

TEST_CASE("Recordable lanes accept normalized min, mid, and max writes", "[step_write]")
{
    for (ParamId lane : {ParamId::Note, ParamId::Velocity, ParamId::Filter,
                         ParamId::Attack, ParamId::Decay, ParamId::Octave})
    {
        Sequencer seq; // fresh boot defaults; every probe below starts mismatched
        INFO("lane " << parameterDefinition(lane)->name);

        // 1.0 -> 0.5 -> 0.0 -> 1.0 all move; repeating 1.0 reports unchanged.
        const float probes[] = {1.0f, 0.5f, 0.0f, 1.0f, 1.0f};
        const StepWriteStatus expectedStatus[] = {
            StepWriteStatus::Changed, StepWriteStatus::Changed,
            StepWriteStatus::Changed, StepWriteStatus::Changed,
            StepWriteStatus::AcceptedUnchanged};
        const float expectedValue[] = {
            expectedStored(lane, 1.0f), expectedStored(lane, 0.5f),
            expectedStored(lane, 0.0f), expectedStored(lane, 1.0f),
            expectedStored(lane, 1.0f)};

        for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i)
        {
            INFO("normalized " << probes[i]);
            const auto result = seq.writeStepParameter(
                lane, 0, probes[i], StepWriteDomain::Normalized01, false);
            CHECK(result.status == expectedStatus[i]);
            CHECK(result.stored == Approx(expectedValue[i]));
            CHECK(seq.getStepParameterValue(lane, 0) == Approx(expectedValue[i]));
            if (expectedStatus[i] == StepWriteStatus::AcceptedUnchanged)
                CHECK(result.previousStored == Approx(result.stored));
        }
    }

    // The lane-domain facts behind the expected values above: Note quantizes
    // to 0/18/36, Octave picks the quarter-lane zone values, continuous lanes
    // store the identity.
    {
        Sequencer seq;
        CHECK(seq.writeStepParameter(ParamId::Note, 1, 0.0f,
                                     StepWriteDomain::Normalized01, false)
                  .stored == Approx(0.0f));
        CHECK(seq.writeStepParameter(ParamId::Note, 1, 0.5f,
                                     StepWriteDomain::Normalized01, false)
                  .stored == Approx(18.0f));
        CHECK(seq.writeStepParameter(ParamId::Note, 1, 1.0f,
                                     StepWriteDomain::Normalized01, false)
                  .stored == Approx(36.0f));
        CHECK(seq.writeStepParameter(ParamId::Octave, 1, 0.0f,
                                     StepWriteDomain::Normalized01, false)
                  .stored == Approx(SequencerConstants::OCTAVE_TRACK_MINUS_2)
                  .margin(1e-6f));
        CHECK(seq.writeStepParameter(ParamId::Octave, 1, 0.5f,
                                     StepWriteDomain::Normalized01, false)
                  .stored == Approx(SequencerConstants::OCTAVE_TRACK_ZERO)
                  .margin(1e-6f));
        CHECK(seq.writeStepParameter(ParamId::Octave, 1, 1.0f,
                                     StepWriteDomain::Normalized01, false)
                  .stored == Approx(SequencerConstants::OCTAVE_TRACK_PLUS_2)
                  .margin(1e-6f));
        CHECK(seq.writeStepParameter(ParamId::Velocity, 1, 0.37f,
                                     StepWriteDomain::Normalized01, false)
                  .stored == Approx(0.37f));
    }
}

TEST_CASE("Normalized writes clamp into the lane and non-finite input is rejected", "[step_write]")
{
    Sequencer seq;
    const float quietNan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    // Continuous value storage happens only after clamping to the lane range.
    CHECK(seq.writeStepParameter(ParamId::Velocity, 2, 0.25f,
                                 StepWriteDomain::Normalized01, false)
              .stored == Approx(0.25f));
    const auto above = seq.writeStepParameter(ParamId::Velocity, 2, 1.5f,
                                              StepWriteDomain::Normalized01, false);
    CHECK(above.status == StepWriteStatus::Changed);
    CHECK(above.stored == Approx(1.0f));
    const auto below = seq.writeStepParameter(ParamId::Velocity, 2, -0.25f,
                                              StepWriteDomain::Normalized01, false);
    CHECK(below.status == StepWriteStatus::Changed);
    CHECK(below.stored == Approx(0.0f));

    // Non-finite input is refused before any storage happens.
    for (float bad : {quietNan, infinity, -infinity})
    {
        INFO("bad value " << bad);
        const auto rejected = seq.writeStepParameter(
            ParamId::Velocity, 2, bad, StepWriteDomain::Normalized01, false);
        CHECK(rejected.status == StepWriteStatus::Rejected);
        CHECK(seq.getStepParameterValue(ParamId::Velocity, 2) == Approx(0.0f));
        const auto rejectedLane = seq.writeStepParameter(
            ParamId::Filter, 2, bad, StepWriteDomain::LaneValue, false);
        CHECK(rejectedLane.status == StepWriteStatus::Rejected);
        CHECK(seq.getStepParameterValue(ParamId::Filter, 2) == Approx(0.5f));
    }

    // Out-of-range normalized Note/Octave still lands on the lane ends.
    CHECK(seq.writeStepParameter(ParamId::Note, 2, -1.0f,
                                 StepWriteDomain::Normalized01, false)
              .stored == Approx(0.0f));
    CHECK(seq.writeStepParameter(ParamId::Note, 2, 7.0f,
                                 StepWriteDomain::Normalized01, false)
              .stored == Approx(36.0f));
    CHECK(seq.writeStepParameter(ParamId::Filter, 2, 2.0f,
                                 StepWriteDomain::Normalized01, false)
              .stored == Approx(1.0f));
}

TEST_CASE("Note and Octave sweeps track the shared normalized mapping", "[step_write]")
{
    Sequencer seq;
    for (int i = 0; i <= 20; ++i)
    {
        const float n = static_cast<float>(i) * 0.05f;
        const uint8_t step = static_cast<uint8_t>(i);

        // Note storage rounds the mapped range value to whole scale steps;
        // the production round and this expectation share the same float ops.
        const auto note = seq.writeStepParameter(ParamId::Note, step, n,
                                                 StepWriteDomain::Normalized01, false);
        CHECK(note.stored ==
              Approx(std::round(mapNormalizedValueToParamRange(ParamId::Note, n))));

        // Octave storage is the quarter-lane zone value itself.
        const auto octave = seq.writeStepParameter(ParamId::Octave, step, n,
                                                   StepWriteDomain::Normalized01, false);
        CHECK(octave.stored ==
              Approx(mapNormalizedValueToParamRange(ParamId::Octave, n)).margin(1e-6f));
    }
}

TEST_CASE("Change detection happens after storage-domain rounding", "[step_write]")
{
    Sequencer seq;

    // Both normalized inputs round to scale step 18.
    const auto first = seq.writeStepParameter(ParamId::Note, 3, 0.499f,
                                              StepWriteDomain::Normalized01, false);
    REQUIRE(first.status == StepWriteStatus::Changed);
    REQUIRE(first.stored == Approx(18.0f));
    const auto second = seq.writeStepParameter(ParamId::Note, 3, 0.501f,
                                               StepWriteDomain::Normalized01, false);
    CHECK(second.status == StepWriteStatus::AcceptedUnchanged);
    CHECK(second.previousStored == Approx(second.stored));
    CHECK(second.stored == Approx(18.0f));

    // Octave zones quantize the same way: anything inside the center third is
    // the neutral quarter-lane value.
    CHECK(seq.getStepParameterValue(ParamId::Octave, 4) ==
          Approx(SequencerConstants::OCTAVE_TRACK_ZERO).margin(1e-6f));
    const auto octaveSame = seq.writeStepParameter(ParamId::Octave, 4, 0.40f,
                                                   StepWriteDomain::Normalized01, false);
    CHECK(octaveSame.status == StepWriteStatus::AcceptedUnchanged);
    CHECK(octaveSame.previousStored == Approx(octaveSame.stored));
    const auto octaveMove = seq.writeStepParameter(ParamId::Octave, 4, 0.60f,
                                                   StepWriteDomain::Normalized01, false);
    CHECK(octaveMove.status == StepWriteStatus::Changed);
    CHECK(octaveMove.stored == Approx(SequencerConstants::OCTAVE_TRACK_PLUS_1).margin(1e-6f));

    // LaneValue writes round integer lanes too: 17.6 stores as 18.
    const auto laneRounded = seq.writeStepParameter(ParamId::Note, 5, 17.6f,
                                                    StepWriteDomain::LaneValue, false);
    CHECK(laneRounded.status == StepWriteStatus::Changed); // boot default 0 -> 18
    CHECK(laneRounded.stored == Approx(18.0f));
    const auto laneSame = seq.writeStepParameter(ParamId::Note, 5, 18.49f,
                                                 StepWriteDomain::LaneValue, false);
    CHECK(laneSame.status == StepWriteStatus::AcceptedUnchanged);
    CHECK(laneSame.previousStored == Approx(laneSame.stored));

    // Continuous lanes have no rounding: any real movement is a change.
    const auto continuous = seq.writeStepParameter(ParamId::Velocity, 6, 0.5001f,
                                                   StepWriteDomain::Normalized01, false);
    CHECK(continuous.status == StepWriteStatus::Changed);
    CHECK(continuous.stored == Approx(0.5001f));
}

TEST_CASE("Non-recordable lanes and out-of-range steps are rejected untouched", "[step_write]")
{
    Sequencer seq;

    // Gate, Slide and GateLength keep their dedicated interaction rules and
    // are never written through the shared write.
    CHECK(seq.writeStepParameter(ParamId::Gate, 5, 1.0f,
                                 StepWriteDomain::Normalized01, true)
              .status == StepWriteStatus::Rejected);
    CHECK(seq.getStepParameterValue(ParamId::Gate, 5) == Approx(0.0f));
    CHECK(seq.writeStepParameter(ParamId::Slide, 5, 1.0f,
                                 StepWriteDomain::LaneValue, true)
              .status == StepWriteStatus::Rejected);
    CHECK(seq.getStepParameterValue(ParamId::Slide, 5) == Approx(0.0f));
    CHECK(seq.writeStepParameter(ParamId::GateLength, 5, 0.8f,
                                 StepWriteDomain::LaneValue, false)
              .status == StepWriteStatus::Rejected);
    CHECK(seq.getStepParameterValue(ParamId::GateLength, 5) == Approx(0.5f));

    // ParamId::Count has no descriptor at all.
    CHECK(seq.writeStepParameter(ParamId::Count, 0, 0.5f,
                                 StepWriteDomain::Normalized01, false)
              .status == StepWriteStatus::Rejected);

    // Step indices beyond the 64-slot capacity never reach storage; in
    // particular a wrap modulo the track length must not happen.
    for (uint8_t step : {uint8_t{64}, uint8_t{200}, uint8_t{255}})
    {
        const auto rejected = seq.writeStepParameter(ParamId::Velocity, step, 0.9f,
                                                     StepWriteDomain::Normalized01, false);
        CHECK(rejected.status == StepWriteStatus::Rejected);
    }
    CHECK(seq.getStepParameterValue(ParamId::Velocity, 0) == Approx(0.5f));
    CHECK(seq.getStepParameterValue(ParamId::Velocity, 63) == Approx(0.5f));

    // A rejected write leaves neighbouring lanes alone.
    CHECK(seq.getStepParameterValue(ParamId::Filter, 5) == Approx(0.5f));
    CHECK(seq.getStepParameterValue(ParamId::Note, 5) == Approx(0.0f));
}

TEST_CASE("Recording Note honors the stored gate at the step", "[step_write]")
{
    Sequencer seq;
    VoiceState state;

    // Selected-step recording (AtStep) consults the stored Gate at the edited
    // step; a rest step refuses the Note.
    const auto refused = seq.writeStepParameter(ParamId::Note, 0, 0.3f,
                                                StepWriteDomain::Normalized01, true,
                                                NoteGateRule::AtStep);
    CHECK(refused.status == StepWriteStatus::Rejected);
    CHECK(seq.getStepParameterValue(ParamId::Note, 0) == Approx(0.0f));

    // The same write on a gated step lands.
    seq.setStepParameterValue(ParamId::Gate, 0, 1.0f);
    const auto accepted = seq.writeStepParameter(ParamId::Note, 0, 0.3f,
                                                 StepWriteDomain::Normalized01, true,
                                                 NoteGateRule::AtStep);
    CHECK(accepted.status == StepWriteStatus::Changed);
    CHECK(accepted.stored == Approx(11.0f)); // round(0.3 * 36)
    CHECK(seq.getStepParameterValue(ParamId::Note, 0) == Approx(11.0f));

    // Explicit editing ignores the gate rule entirely: rests stay writable.
    const auto explicitEdit = seq.writeStepParameter(ParamId::Note, 1, 0.5f,
                                                     StepWriteDomain::Normalized01, false);
    CHECK(explicitEdit.status == StepWriteStatus::Changed);
    CHECK(explicitEdit.stored == Approx(18.0f));
    const auto rulelessRecording = seq.writeStepParameter(
        ParamId::Note, 2, 0.5f, StepWriteDomain::Normalized01, true,
        NoteGateRule::None);
    CHECK(rulelessRecording.status == StepWriteStatus::Changed);
    CHECK(rulelessRecording.stored == Approx(18.0f));
    CHECK(seq.getStepParameterValue(ParamId::Gate, 1) == Approx(0.0f));
    CHECK(seq.getStepParameterValue(ParamId::Gate, 2) == Approx(0.0f));

    // The acceptance above never touched the gate lane itself.
    CHECK(seq.getStepParameterValue(ParamId::Gate, 0) == Approx(1.0f));
}

TEST_CASE("AtGateCursor recording follows the Gate lane's own cursor", "[step_write]")
{
    Sequencer seq;
    VoiceState state;
    seq.setParameterStepCount(ParamId::Gate, 4);
    seq.setStepParameterValue(ParamId::Gate, 0, 1.0f);
    seq.setStepParameterValue(ParamId::Gate, 1, 0.0f); // the rest step
    seq.setStepParameterValue(ParamId::Gate, 2, 1.0f);
    seq.setStepParameterValue(ParamId::Gate, 3, 1.0f);
    seq.start();

    // Drive the cursors (mm_distance -1 records nothing) until the Gate lane's
    // own cursor sits on the low step: 5 % 4 == 1.
    seq.advanceStep(5, -1, false, false, false, false, false, false, -1, &state);
    REQUIRE(seq.getCurrentStepForParameter(ParamId::Gate) == 1);

    const auto refused = seq.writeStepParameter(ParamId::Note, 0, 0.3f,
                                                StepWriteDomain::Normalized01, true,
                                                NoteGateRule::AtGateCursor);
    CHECK(refused.status == StepWriteStatus::Rejected);
    CHECK(seq.getStepParameterValue(ParamId::Note, 0) == Approx(0.0f));
    CHECK(seq.getStepParameterValue(ParamId::Note, 1) == Approx(0.0f));

    // Cursor on a gated step (6 % 4 == 2): the same recording write lands.
    seq.advanceStep(6, -1, false, false, false, false, false, false, -1, &state);
    REQUIRE(seq.getCurrentStepForParameter(ParamId::Gate) == 2);
    const auto accepted = seq.writeStepParameter(ParamId::Note, 0, 0.3f,
                                                 StepWriteDomain::Normalized01, true,
                                                 NoteGateRule::AtGateCursor);
    CHECK(accepted.status == StepWriteStatus::Changed);
    CHECK(accepted.stored == Approx(11.0f));

    // AtStep keeps consulting the edited step's own gate even while the
    // cursor rests on a low step: 9 % 4 == 1, but step 0 is gated high.
    seq.advanceStep(9, -1, false, false, false, false, false, false, -1, &state);
    REQUIRE(seq.getCurrentStepForParameter(ParamId::Gate) == 1);
    const auto byStep = seq.writeStepParameter(ParamId::Note, 0, 0.4f,
                                               StepWriteDomain::Normalized01, true,
                                               NoteGateRule::AtStep);
    CHECK(byStep.status == StepWriteStatus::Changed);
    CHECK(byStep.stored == Approx(14.0f)); // round(0.4 * 36)
}

TEST_CASE("Rest steps stay writable for every modifier lane", "[step_write]")
{
    Sequencer seq; // every Gate defaults low: step 3 is a rest

    for (ParamId lane : {ParamId::Velocity, ParamId::Filter, ParamId::Attack,
                         ParamId::Decay, ParamId::Octave})
    {
        INFO("lane " << parameterDefinition(lane)->name);
        // Recording semantics on a rest step are allowed for modifiers...
        const auto recorded = seq.writeStepParameter(lane, 3, 0.7f,
                                                     StepWriteDomain::Normalized01, true,
                                                     NoteGateRule::AtStep);
        CHECK(recorded.status == StepWriteStatus::Changed);
        CHECK(recorded.stored ==
              Approx(mapNormalizedValueToParamRange(lane, 0.7f)).margin(1e-6f));
        // ...and so is explicit editing.
        const auto edited = seq.writeStepParameter(lane, 3, 0.2f,
                                                   StepWriteDomain::Normalized01, false);
        CHECK(edited.status == StepWriteStatus::Changed);
        CHECK(edited.stored ==
              Approx(mapNormalizedValueToParamRange(lane, 0.2f)).margin(1e-6f));
    }
}

TEST_CASE("LaneValue writes use stored lane units directly", "[step_write]")
{
    Sequencer seq;

    // Encoder-style absolute lane units: 0.2 then +0.05 on the Filter lane.
    const auto base = seq.writeStepParameter(ParamId::Filter, 6, 0.2f,
                                             StepWriteDomain::LaneValue, false);
    CHECK(base.status == StepWriteStatus::Changed);
    CHECK(base.stored == Approx(0.2f));
    const auto nudged = seq.writeStepParameter(ParamId::Filter, 6, 0.25f,
                                               StepWriteDomain::LaneValue, false);
    CHECK(nudged.status == StepWriteStatus::Changed);
    CHECK(nudged.stored == Approx(0.25f));
    CHECK(seq.getStepParameterValue(ParamId::Filter, 6) == Approx(0.25f));

    // Integer lanes still round LaneValue input into whole scale steps...
    const auto noteWhole = seq.writeStepParameter(ParamId::Note, 6, 17.0f,
                                                  StepWriteDomain::LaneValue, false);
    CHECK(noteWhole.status == StepWriteStatus::Changed);
    CHECK(noteWhole.stored == Approx(17.0f));
    // ...and clamp LaneValue input into the lane range.
    const auto noteHigh = seq.writeStepParameter(ParamId::Note, 6, 99.0f,
                                                 StepWriteDomain::LaneValue, false);
    CHECK(noteHigh.status == StepWriteStatus::Changed);
    CHECK(noteHigh.stored == Approx(36.0f));
    const auto noteLow = seq.writeStepParameter(ParamId::Note, 6, -4.0f,
                                                StepWriteDomain::LaneValue, false);
    CHECK(noteLow.status == StepWriteStatus::Changed);
    CHECK(noteLow.stored == Approx(0.0f));

    const auto velocityHigh = seq.writeStepParameter(ParamId::Velocity, 6, 3.0f,
                                                     StepWriteDomain::LaneValue, false);
    CHECK(velocityHigh.status == StepWriteStatus::Changed);
    CHECK(velocityHigh.stored == Approx(1.0f));

    // The domain never widens which lanes are writable.
    CHECK(seq.writeStepParameter(ParamId::Gate, 6, 1.0f, StepWriteDomain::LaneValue, false)
              .status == StepWriteStatus::Rejected);
}

TEST_CASE("Live advanceStep recording writes armed lanes at their own cursors", "[step_write]")
{
    Sequencer seq;
    VoiceState state;
    // Polyrhythmic lane lengths: one clock position lands on different steps
    // per lane.
    seq.setParameterStepCount(ParamId::Note, 3);
    seq.setParameterStepCount(ParamId::Gate, 4);
    seq.setParameterStepCount(ParamId::Filter, 5);
    seq.start();

    // uclock 7: Filter cursor 7%5 = 2, Gate cursor 3, Note cursor 1. Only the
    // armed Filter lane records, at its own cursor.
    seq.setRecordingInput(0.7f);
    seq.advanceStep(7, 300, false, false, true, false, false, false, -1, &state);
    REQUIRE(seq.getCurrentStepForParameter(ParamId::Filter) == 2);
    CHECK(seq.getStepParameterValue(ParamId::Filter, 2) == Approx(0.7f));
    CHECK(seq.getStepParameterValue(ParamId::Filter,
                                    seq.getCurrentStepForParameter(ParamId::Filter)) ==
          Approx(0.7f));
    // Unarmed lanes keep their boot values everywhere.
    CHECK(seq.getStepParameterValue(ParamId::Velocity, 2) == Approx(0.5f));
    CHECK(seq.getStepParameterValue(ParamId::Note, 1) == Approx(0.0f));
    CHECK(seq.getStepParameterValue(ParamId::Attack, 2) == Approx(0.01f));
    CHECK(seq.getStepParameterValue(ParamId::Decay, 2) == Approx(0.3f));
    CHECK(seq.getStepParameterValue(ParamId::Octave, 2) == Approx(0.5f));
    // Other Filter steps stay untouched, including the Gate-lane cursor step.
    CHECK(seq.getStepParameterValue(ParamId::Filter, 1) == Approx(0.5f));
    CHECK(seq.getStepParameterValue(ParamId::Filter, 3) == Approx(0.5f));
    // The sounding step already reflects the fresh recording.
    CHECK(state.filterCutoff == Approx(0.7f));

    // Two armed lanes record simultaneously, each at its own cursor:
    // uclock 11 puts Filter on 1 and Velocity on 11.
    seq.setRecordingInput(0.3f);
    seq.advanceStep(11, 50, false, true, true, false, false, false, -1, &state);
    CHECK(seq.getStepParameterValue(ParamId::Filter, 1) == Approx(0.3f));
    CHECK(seq.getStepParameterValue(ParamId::Velocity, 11) == Approx(0.3f));
    CHECK(seq.getStepParameterValue(ParamId::Velocity, 1) == Approx(0.5f));

    // Without a hand in range nothing is written, even when armed.
    seq.setRecordingInput(0.9f);
    seq.advanceStep(3, -1, false, false, true, false, false, false, -1, &state);
    CHECK(seq.getStepParameterValue(ParamId::Filter, 3) == Approx(0.5f));

    // A selected step for edit disables live recording entirely.
    seq.setRecordingInput(0.4f);
    seq.advanceStep(4, 300, false, false, true, false, false, false, 1, &state);
    CHECK(seq.getStepParameterValue(ParamId::Filter, 4) == Approx(0.5f));

    // Without a calibrated input the raw sensor distance normalizes by the
    // full sensor span.
    seq.advanceStep(8, 300, false, false, true, false, false, false, -1, &state);
    CHECK(seq.getStepParameterValue(ParamId::Filter, 3) ==
          Approx(300.0f / 1100.0f));
}
