#include "display/DisplayModel.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <cstring>

TEST_CASE("Display plots use composed patch values and real lane cursors", "[display]") {
    Sequencer sequence;
    VoiceConfig config;
    config.usePatchBases = true;
    config.baseVelocity = 0.75f;
    sequence.setPlaybackTransform(VoiceEdit::composeLane, &config, VoiceEdit::mapOctave);
    sequence.fillModulationTrack(ParamId::Velocity, SequencerConstants::LANE_FOLLOWS_PATCH);
    sequence.setParameterLoop(ParamId::Velocity, 4, 10);
    sequence.setStepParameterValue(ParamId::Velocity, 5, 0.25f);
    sequence.start();
    VoiceState played;
    sequence.advanceStep(7, -1, {}, -1, &played);
    const auto cursor = sequence.getCurrentStepForParameter(ParamId::Velocity);
    LaneDisplay::Lane lane;
    DisplayModel::captureLane(lane, sequence, ParamId::Velocity, &config, 90);
    CHECK(lane.start == 4);
    CHECK(lane.count == 11);
    CHECK(lane.cursor == cursor);
    CHECK(lane.values[4] == 191);
    CHECK(lane.values[5] == 64);
    CHECK(sequence.getRawStepValue(ParamId::Velocity, 4) == SequencerConstants::LANE_FOLLOWS_PATCH);
    CHECK(sequence.isRunning());
    CHECK(sequence.getCurrentStepForParameter(ParamId::Velocity) == cursor);
    CHECK(std::strlen(lane.valueText) > 0);
    CHECK(std::strcmp(lane.label, "Velocity") == 0);
    for (int i = lane.count; i < 64; ++i) CHECK(lane.values[i] == 0);
}

TEST_CASE("Display quantization clamps data and respects lane ranges", "[display]") {
    CHECK(DisplayModel::graphValue(ParamId::Note, 0) == 0);
    CHECK(DisplayModel::graphValue(ParamId::Note, 18) == 128);
    CHECK(DisplayModel::graphValue(ParamId::Note, 36) == 255);
    CHECK(DisplayModel::graphValue(ParamId::GateLength, 0.1f) == 0);
    CHECK(DisplayModel::graphValue(ParamId::GateLength, 1) == 255);
    CHECK(DisplayModel::graphValue(ParamId::Velocity, -1) == 0);
    CHECK(DisplayModel::graphValue(ParamId::Velocity, 100) == 255);
    CHECK(DisplayModel::graphValue(ParamId::Gate, 1) == 255);
    CHECK(DisplayModel::graphValue(ParamId::Count, 1) == 0);
    CHECK(DisplayModel::graphValue(ParamId::Filter, std::numeric_limits<float>::quiet_NaN()) == 0);
    CHECK(DisplayModel::graphValue(ParamId::Filter, std::numeric_limits<float>::infinity()) == 0);
}
