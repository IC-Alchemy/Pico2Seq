#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sequencer/LaneCopy.h"
#include "sequencer/Sequencer.h"

#include <cmath>

// COPY LANE core: one lane's stored contents (all 64 slots + loop length) moves
// between sequencers verbatim, or through the 0..1 domain onto another lane.

using Catch::Approx;
using lanecopy::LaneSnapshot;

namespace
{
constexpr uint8_t kMax = SequencerConstants::MAX_STEPS_COUNT;

// Fill a lane's complete storage through the same grow/write/shrink path the codec uses,
// so the tail behind a short loop holds recognisable values too.
void fillLane(Sequencer &seq, ParamId lane, uint8_t length, float (*valueAt)(uint8_t step))
{
    seq.setParameterStepCount(lane, kMax);
    for (uint8_t step = 0; step < kMax; ++step)
        seq.setRawStepValue(lane, step, valueAt(step));
    seq.setParameterStepCount(lane, length);
}

float ramp(uint8_t step) { return 0.01f * static_cast<float>(step); }                    // 0.00 .. 0.63
float degrees(uint8_t step) { return static_cast<float>(step % 37); }                     // Note: 0..36
float other(uint8_t step) { return 0.9f - 0.005f * static_cast<float>(step); }            // another shape

void requireSameLane(const Sequencer &a, const Sequencer &b, ParamId lane)
{
    REQUIRE(a.getParameterStepCount(lane) == b.getParameterStepCount(lane));
    for (uint8_t step = 0; step < kMax; ++step)
    {
        INFO("lane " << int(lane) << " step " << int(step));
        REQUIRE(a.getRawStepValue(lane, step) == b.getRawStepValue(lane, step));
    }
}
} // namespace

TEST_CASE("capture records the lane, its loop length and every raw slot", "[lanecopy][sequencer]")
{
    Sequencer source;
    fillLane(source, ParamId::Filter, 8, ramp);

    LaneSnapshot clip;
    CHECK_FALSE(clip.valid()); // a fresh snapshot holds nothing
    lanecopy::capture(source, ParamId::Filter, clip);

    REQUIRE(clip.valid());
    CHECK(clip.lane == ParamId::Filter);
    CHECK(clip.length == 8);
    for (uint8_t step = 0; step < kMax; ++step) // the tail behind the 8-step loop is kept
        CHECK(clip.values[step] == source.getRawStepValue(ParamId::Filter, step));

    lanecopy::capture(source, ParamId::Count, clip);
    CHECK_FALSE(clip.valid()); // an unknown lane leaves nothing behind
}

TEST_CASE("pasting the same lane onto another voice is verbatim, length and tail included", "[lanecopy][sequencer]")
{
    for (const uint8_t destinationLength : {2, 5, 16, 33, 64})
    for (const uint8_t sourceLength : {2, 5, 16, 33, 64})
    {
        CAPTURE(int(destinationLength), int(sourceLength));
        Sequencer source, destination;
        fillLane(source, ParamId::Attack, sourceLength, ramp);
        fillLane(destination, ParamId::Attack, destinationLength, other);

        LaneSnapshot clip;
        lanecopy::capture(source, ParamId::Attack, clip);
        REQUIRE(lanecopy::paste(clip, destination, ParamId::Attack));

        // A long source onto a short lane is the case a naive per-step copy corrupts:
        // writes wrap at the active length unless the lane is widened first.
        requireSameLane(source, destination, ParamId::Attack);
    }
}

TEST_CASE("a paste touches only the destination lane", "[lanecopy][sequencer]")
{
    Sequencer source, destination, untouched;
    fillLane(source, ParamId::Filter, 7, ramp);
    for (uint8_t id = 0; id < PARAM_ID_COUNT; ++id)
    {
        fillLane(destination, static_cast<ParamId>(id), 9, other);
        fillLane(untouched, static_cast<ParamId>(id), 9, other);
    }

    LaneSnapshot clip;
    lanecopy::capture(source, ParamId::Filter, clip);
    REQUIRE(lanecopy::paste(clip, destination, ParamId::Filter));

    for (uint8_t id = 0; id < PARAM_ID_COUNT; ++id)
    {
        const auto lane = static_cast<ParamId>(id);
        if (lane == ParamId::Filter)
            requireSameLane(source, destination, lane);
        else
            requireSameLane(untouched, destination, lane); // other lanes, lengths included, are as they were
    }
}

TEST_CASE("the snapshot is a copy: later edits to the source do not reach a paste", "[lanecopy][sequencer]")
{
    Sequencer source, first, second;
    fillLane(source, ParamId::Velocity, 16, ramp);
    LaneSnapshot clip;
    lanecopy::capture(source, ParamId::Velocity, clip);

    source.setStepParameterValue(ParamId::Velocity, 3, 0.99f);
    source.setParameterStepCount(ParamId::Velocity, 4);

    REQUIRE(lanecopy::paste(clip, first, ParamId::Velocity));
    REQUIRE(lanecopy::paste(clip, second, ParamId::Velocity)); // the memory is not consumed
    CHECK(first.getParameterStepCount(ParamId::Velocity) == 16);
    CHECK(first.getRawStepValue(ParamId::Velocity, 3) == Approx(0.03f));
    requireSameLane(first, second, ParamId::Velocity);
}

TEST_CASE("Note degrees and the patch-following marker survive a same-lane paste exactly", "[lanecopy][sequencer]")
{
    Sequencer source, destination;
    fillLane(source, ParamId::Note, 12, degrees);
    source.setStepParameterValue(ParamId::Note, 0, 36.0f);
    fillLane(source, ParamId::Velocity, 16, ramp);
    source.setStepParameterValue(ParamId::Velocity, 5, SequencerConstants::LANE_FOLLOWS_PATCH);

    LaneSnapshot note, velocity;
    lanecopy::capture(source, ParamId::Note, note);
    lanecopy::capture(source, ParamId::Velocity, velocity);
    REQUIRE(lanecopy::paste(note, destination, ParamId::Note));
    REQUIRE(lanecopy::paste(velocity, destination, ParamId::Velocity));

    requireSameLane(source, destination, ParamId::Note);
    requireSameLane(source, destination, ParamId::Velocity);
    CHECK(destination.getRawStepValue(ParamId::Note, 0) == 36.0f);
    // That step still has no value of its own, so it plays the destination voice's patch.
    CHECK(followsPatch(destination.getRawStepValue(ParamId::Velocity, 5)));
}

TEST_CASE("an invalid snapshot or lane writes nothing", "[lanecopy][sequencer]")
{
    Sequencer destination, reference;
    fillLane(destination, ParamId::Filter, 11, other);
    fillLane(reference, ParamId::Filter, 11, other);

    const LaneSnapshot empty;
    CHECK_FALSE(lanecopy::paste(empty, destination, ParamId::Filter));

    Sequencer source;
    LaneSnapshot clip;
    lanecopy::capture(source, ParamId::Filter, clip);
    CHECK_FALSE(lanecopy::paste(clip, destination, ParamId::Count));
    clip.length = 0;
    CHECK_FALSE(lanecopy::paste(clip, destination, ParamId::Filter));

    requireSameLane(reference, destination, ParamId::Filter);
}

TEST_CASE("a pasted lane plays on its new loop length", "[lanecopy][sequencer]")
{
    Sequencer source, destination;
    fillLane(source, ParamId::Octave, 5, [](uint8_t step) { return step % 2 ? 0.25f : 0.75f; });
    LaneSnapshot clip;
    lanecopy::capture(source, ParamId::Octave, clip);
    REQUIRE(lanecopy::paste(clip, destination, ParamId::Octave));

    destination.start();
    VoiceState state;
    destination.advanceStep(7, -1, StepEditButtons{}, -1, &state);
    CHECK(destination.getCurrentStepForParameter(ParamId::Octave) == 7 % 5);
    CHECK(destination.getStepParameterValue(ParamId::Octave, 7) == source.getStepParameterValue(ParamId::Octave, 7));
}

// --- Different lane: through the 0..1 domain -------------------------------------

TEST_CASE("lanes that share the 0..1 range keep their values across a paste", "[lanecopy][sequencer]")
{
    for (const ParamId from : {ParamId::Velocity, ParamId::Filter, ParamId::Attack, ParamId::Sustain})
    for (const ParamId to : {ParamId::Velocity, ParamId::Filter, ParamId::Attack, ParamId::Sustain})
    {
        CAPTURE(int(from), int(to));
        for (const float value : {0.0f, 0.2f, 0.5f, 0.87f, 1.0f})
            CHECK(lanecopy::convertValue(from, to, value) == Approx(value));
    }
}

TEST_CASE("a lane's own minimum and maximum map onto the target's", "[lanecopy][sequencer]")
{
    // Release 0.01..1 and Decay 0.1..1 do not start at zero.
    CHECK(lanecopy::convertValue(ParamId::Release, ParamId::Velocity, 0.01f) == Approx(0.0f));
    CHECK(lanecopy::convertValue(ParamId::Release, ParamId::Velocity, 1.0f) == Approx(1.0f));
    CHECK(lanecopy::convertValue(ParamId::Velocity, ParamId::Decay, 0.0f) == Approx(0.1f));
    CHECK(lanecopy::convertValue(ParamId::Velocity, ParamId::Decay, 1.0f) == Approx(1.0f));
    // Note is 0..36 scale degrees.
    CHECK(lanecopy::convertValue(ParamId::Note, ParamId::Velocity, 0.0f) == Approx(0.0f));
    CHECK(lanecopy::convertValue(ParamId::Note, ParamId::Velocity, 18.0f) == Approx(0.5f));
    CHECK(lanecopy::convertValue(ParamId::Note, ParamId::Velocity, 36.0f) == Approx(1.0f));
    CHECK(lanecopy::convertValue(ParamId::Filter, ParamId::Note, 0.5f) == Approx(18.0f));
    // Out-of-range input is clamped, not extrapolated.
    CHECK(lanecopy::convertValue(ParamId::Note, ParamId::Filter, 99.0f) == Approx(1.0f));
}

TEST_CASE("pasting onto Octave snaps to its five detents", "[lanecopy][sequencer]")
{
    // A neutral 0.5 must stay neutral: the hand-distance zones of the live-record
    // mapper would turn it into +1 octave.
    CHECK(lanecopy::convertValue(ParamId::Filter, ParamId::Octave, 0.5f) == Approx(0.5f));
    CHECK(lanecopy::convertValue(ParamId::Filter, ParamId::Octave, 0.0f) == Approx(0.0f));
    CHECK(lanecopy::convertValue(ParamId::Filter, ParamId::Octave, 1.0f) == Approx(1.0f));
    CHECK(lanecopy::convertValue(ParamId::Filter, ParamId::Octave, 0.2f) == Approx(0.25f));
    CHECK(lanecopy::convertValue(ParamId::Filter, ParamId::Octave, 0.7f) == Approx(0.75f));
    // And leaving Octave, its detents are plain levels.
    for (const float detent : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
        CHECK(lanecopy::convertValue(ParamId::Octave, ParamId::Filter, detent) == Approx(detent));
}

TEST_CASE("a step without a value of its own follows the patch where it can", "[lanecopy][sequencer]")
{
    const float follows = SequencerConstants::LANE_FOLLOWS_PATCH;
    CHECK(lanecopy::convertValue(ParamId::Velocity, ParamId::Filter, follows) == follows);
    CHECK(lanecopy::convertValue(ParamId::Release, ParamId::Attack, follows) == follows);
    // Note and Octave have no patch value to follow: they get their own default.
    CHECK(lanecopy::convertValue(ParamId::Velocity, ParamId::Note, follows) == 0.0f);
    CHECK(lanecopy::convertValue(ParamId::Velocity, ParamId::Octave, follows) == Approx(0.5f));
    CHECK(lanecopy::convertValue(ParamId::Velocity, ParamId::GateLength, follows) == Approx(0.8f));
}

TEST_CASE("convertValue is the identity for the same or an unknown lane", "[lanecopy][sequencer]")
{
    CHECK(lanecopy::convertValue(ParamId::Filter, ParamId::Filter, 0.37f) == 0.37f);
    CHECK(lanecopy::convertValue(ParamId::Count, ParamId::Filter, 0.37f) == 0.37f);
    CHECK(lanecopy::convertValue(ParamId::Filter, ParamId::Count, 0.37f) == 0.37f);
}

TEST_CASE("a cross-lane paste keeps the shape, length and tail, and stores clamped values", "[lanecopy][sequencer]")
{
    Sequencer source, destination;
    fillLane(source, ParamId::Filter, 6, ramp); // 0.00 .. 0.63 across all 64 slots
    fillLane(destination, ParamId::Note, 20, degrees);

    LaneSnapshot clip;
    lanecopy::capture(source, ParamId::Filter, clip);
    REQUIRE(lanecopy::paste(clip, destination, ParamId::Note)); // Filter shape -> Note degrees

    CHECK(destination.getParameterStepCount(ParamId::Note) == 6);
    for (uint8_t step = 0; step < kMax; ++step)
    {
        INFO("step " << int(step));
        const float stored = destination.getRawStepValue(ParamId::Note, step);
        CHECK(stored == Approx(std::round(clip.values[step] * 36.0f))); // whole degrees, as Note stores them
        CHECK(stored >= 0.0f);
        CHECK(stored <= 36.0f);
    }
    // The source is read-only.
    CHECK(source.getParameterStepCount(ParamId::Filter) == 6);
    CHECK(source.getRawStepValue(ParamId::Filter, 40) == Approx(0.40f));

    // Toggle lanes snap to 0/1 like any other edit.
    Sequencer gates;
    REQUIRE(lanecopy::paste(clip, gates, ParamId::Gate));
    for (uint8_t step = 0; step < kMax; ++step)
    {
        const float stored = gates.getRawStepValue(ParamId::Gate, step);
        CHECK((stored == 0.0f || stored == 1.0f));
        CHECK(stored == (clip.values[step] > 0.5f ? 1.0f : 0.0f));
    }
}
