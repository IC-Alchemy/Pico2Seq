#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sequencer/ParameterManager.h"
#include "sequencer/Sequencer.h"

#include <algorithm>
#include <cmath>

// Depth-based randomizer: continuous lanes get triangular offsets around the
// neutral modifier 0.5; depth sets the reach, per-lane amounts scale it, and
// a fixed seed makes every draw reproducible.

namespace
{
constexpr ParamId kShaped[] = {ParamId::Velocity, ParamId::Filter, ParamId::Attack, ParamId::Decay};

ParameterManager freshTracks()
{
    ParameterManager pm;
    pm.init();
    for (uint8_t i = 0; i < PARAM_ID_COUNT; ++i)
        pm.setStepCount(static_cast<ParamId>(i), SequencerConstants::MAX_STEPS_COUNT);
    return pm;
}

float largestOffset(const ParameterManager &pm, ParamId id)
{
    float largest = 0.0f;
    for (uint8_t step = 0; step < pm.getStepCount(id); ++step)
        largest = std::max(largest, std::abs(pm.getValue(id, step) - 0.5f));
    return largest;
}
} // namespace

TEST_CASE("Randomized lane offsets stay inside the depth radius", "[randomize][sequencer]")
{
    for (uint8_t depth : {10, 35, 60, 100}) {
        for (uint64_t seed : {1ull, 12345ull, 0xDEADBEEFCAFEull}) {
            INFO("depth " << int(depth) << " seed " << seed);
            auto pm = freshTracks();
            pm.randomizeParameters(depth, seed);
            for (ParamId id : kShaped) {
                for (uint8_t step = 0; step < pm.getStepCount(id); ++step) {
                    const float value = pm.getValue(id, step);
                    REQUIRE(value >= 0.0f);
                    REQUIRE(value <= 1.0f);
                    // Depth 100 spans the whole lane; depth 10 is the old +/-0.05.
                    REQUIRE(std::abs(value - 0.5f) <= depth / 200.0f + 1e-5f);
                }
            }
        }
    }
}

TEST_CASE("A fixed seed reproduces the same randomization", "[randomize][sequencer]")
{
    auto a = freshTracks();
    auto b = freshTracks();
    auto c = freshTracks();
    a.randomizeParameters(35, 777);
    b.randomizeParameters(35, 777);
    c.randomizeParameters(35, 778);
    bool differs = false;
    for (uint8_t i = 0; i < PARAM_ID_COUNT; ++i) {
        const auto id = static_cast<ParamId>(i);
        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step) {
            REQUIRE(a.getRawValue(id, step) == b.getRawValue(id, step));
            differs = differs || a.getRawValue(id, step) != c.getRawValue(id, step);
        }
    }
    REQUIRE(differs);
}

TEST_CASE("Deeper randomization reaches further on the same seed", "[randomize][sequencer]")
{
    auto shallow = freshTracks();
    auto deep = freshTracks();
    shallow.randomizeParameters(10, 4242);
    deep.randomizeParameters(60, 4242);
    for (ParamId id : kShaped) {
        INFO("lane " << int(id));
        REQUIRE(largestOffset(deep, id) > largestOffset(shallow, id));
        REQUIRE(largestOffset(deep, id) > 0.05f);
    }
    // Same seed, same draws: every offset scales with depth.
    for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step) {
        const float a = shallow.getValue(ParamId::Filter, step) - 0.5f;
        const float b = deep.getValue(ParamId::Filter, step) - 0.5f;
        REQUIRE(b == Catch::Approx(a * 6.0f).margin(1e-5));
    }
}

TEST_CASE("Draws cluster around the base rather than spreading uniformly", "[randomize][sequencer]")
{
    auto pm = freshTracks();
    pm.randomizeParameters(100, 2026);
    double sum = 0.0;
    int count = 0, central = 0;
    for (ParamId id : kShaped) {
        for (uint8_t step = 0; step < pm.getStepCount(id); ++step) {
            const float value = pm.getValue(id, step);
            sum += value;
            central += std::abs(value - 0.5f) < 0.25f;
            ++count;
        }
    }
    REQUIRE(sum / count == Catch::Approx(0.5).margin(0.05));
    // Triangular: 75% of draws land in the central half; uniform would give 50%.
    REQUIRE(static_cast<double>(central) / count > 0.65);
}

TEST_CASE("Randomize leaves gates and slides untouched", "[randomize][sequencer]")
{
    auto pm = freshTracks();
    pm.setStepCount(ParamId::Gate, 12);
    pm.setStepCount(ParamId::Slide, 7);
    for (uint8_t step = 0; step < 12; ++step)
        pm.setValue(ParamId::Gate, step, step % 3 == 0 ? 1.0f : 0.0f);
    pm.setValue(ParamId::Slide, 2, 1.0f);
    pm.randomizeParameters(100, 99);
    REQUIRE(pm.getStepCount(ParamId::Gate) == 12);
    REQUIRE(pm.getStepCount(ParamId::Slide) == 7);
    for (uint8_t step = 0; step < 12; ++step)
        REQUIRE(pm.getValue(ParamId::Gate, step) == (step % 3 == 0 ? 1.0f : 0.0f));
    for (uint8_t step = 0; step < 7; ++step)
        REQUIRE(pm.getValue(ParamId::Slide, step) == (step == 2 ? 1.0f : 0.0f));
}

TEST_CASE("Notes stay compact integers and octave/gate length return to neutral", "[randomize][sequencer]")
{
    auto pm = freshTracks();
    for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step) {
        pm.setValue(ParamId::Octave, step, 0.9f);
        pm.setValue(ParamId::GateLength, step, 0.2f);
    }
    pm.randomizeParameters(35, 31337);
    for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step) {
        const float note = pm.getValue(ParamId::Note, step);
        REQUIRE(note == std::floor(note));
        REQUIRE(note >= 0.0f);
        REQUIRE(note <= 12.0f);
        REQUIRE(pm.getValue(ParamId::Octave, step) == 0.5f);
        REQUIRE(pm.getValue(ParamId::GateLength, step) ==
                Catch::Approx(mapNormalizedValueToParamRange(ParamId::GateLength, 0.5f)));
    }
}

TEST_CASE("Per-lane amounts scale or exclude a lane", "[randomize][sequencer]")
{
    auto full = freshTracks();
    auto scaled = freshTracks();
    for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step) {
        scaled.setValue(ParamId::Decay, step, 0.8f);
        scaled.setValue(ParamId::Octave, step, 1.0f);
    }
    REQUIRE(scaled.getLaneAmount(ParamId::Filter) == 100);
    scaled.setLaneAmount(ParamId::Filter, 50);
    scaled.setLaneAmount(ParamId::Decay, 0);
    scaled.setLaneAmount(ParamId::Octave, 0);
    scaled.setLaneAmount(ParamId::Velocity, 250); // clamps to 100
    REQUIRE(scaled.getLaneAmount(ParamId::Velocity) == 100);
    full.setLaneAmount(ParamId::Decay, 0);
    full.setLaneAmount(ParamId::Octave, 0);
    full.randomizeParameters(80, 555);
    scaled.randomizeParameters(80, 555);
    for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step) {
        REQUIRE(scaled.getValue(ParamId::Decay, step) == 0.8f);  // excluded lane unchanged
        REQUIRE(scaled.getValue(ParamId::Octave, step) == 1.0f); // excluded lane unchanged
        REQUIRE(scaled.getValue(ParamId::Filter, step) - 0.5f ==
                Catch::Approx((full.getValue(ParamId::Filter, step) - 0.5f) * 0.5f).margin(1e-5));
    }
    REQUIRE(largestOffset(scaled, ParamId::Filter) <= 0.2f + 1e-5f);
}

TEST_CASE("Sequencer randomize forwards depth and seed", "[randomize][sequencer]")
{
    Sequencer a, b;
    a.randomizeParameters(20, 8080);
    b.randomizeParameters(20, 8080);
    for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step) {
        REQUIRE(a.getRawStepValue(ParamId::Filter, step) == b.getRawStepValue(ParamId::Filter, step));
        REQUIRE(std::abs(a.getStepParameterValue(ParamId::Filter, step) - 0.5f) <= 0.1f + 1e-5f);
        REQUIRE(a.getRawStepValue(ParamId::Octave, step) == 0.5f); // whole capacity stays neutral
    }
}
