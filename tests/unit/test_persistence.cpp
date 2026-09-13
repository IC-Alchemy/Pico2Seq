#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <type_traits>
#include "persistence/SnapshotFormat.h"
#include "persistence/ProjectSnapshot.h"
#include "persistence/PatternCodec.h"
#include "sequencer/Sequencer.h"

using namespace persistence;

TEST_CASE("crc32 matches the ISO-HDLC check vector", "[persistence]")
{
    const char *v = "123456789";
    REQUIRE(crc32(reinterpret_cast<const uint8_t *>(v), 9) == 0xCBF43926u);
    REQUIRE(crc32(nullptr, 0) == 0u);
}

TEST_CASE("project snapshot size is locked", "[persistence]")
{
    STATIC_REQUIRE(sizeof(ProjectSnapshotV1) == 10424u);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ProjectSnapshotV1>);
}

namespace
{
// Zero-initialized stepCounts are INVALID (validate requires 1..64); every
// test that wants a range-valid snapshot seeds lengths first.
void seedValidStepCounts(ProjectSnapshotV1 &s)
{
    for (int v = 0; v < 4; ++v)
        for (int t = 0; t < PARAM_ID_COUNT; ++t)
            s.patterns[v].tracks[t].stepCount = 16;
}
} // namespace

TEST_CASE("frame header round-trips and rejects damage", "[persistence]")
{
    ProjectSnapshotV1 snap{};
    seedValidStepCounts(snap);
    snap.settings.tempoBpm = 120.0f;
    snap.settings.currentScale = 5;
    REQUIRE(validateProjectSnapshot(snap));

    uint8_t frame[12 + sizeof(ProjectSnapshotV1)];
    writeFrameHeader(frame, sizeof(snap),
                     crc32(reinterpret_cast<const uint8_t *>(&snap), sizeof(snap)));
    std::memcpy(frame + 12, &snap, sizeof(snap));

    const uint8_t *payload = frame + 12;
    const size_t payloadCapacity = sizeof(frame) - 12;
    REQUIRE(readFrameHeader(frame, payload, payloadCapacity, sizeof(ProjectSnapshotV1)) ==
            FrameStatus::Ok);

    SECTION("too short")
    {
        REQUIRE(readFrameHeader(frame, payload, payloadCapacity - 1,
                                sizeof(ProjectSnapshotV1)) == FrameStatus::TooShort);
    }
    SECTION("bad magic")
    {
        frame[0] ^= 0xFF;
        REQUIRE(readFrameHeader(frame, payload, payloadCapacity, sizeof(ProjectSnapshotV1)) == FrameStatus::BadMagic);
    }
    SECTION("bad version")
    {
        frame[4] = 0x63; frame[5] = 0x00; // version 99
        REQUIRE(readFrameHeader(frame, payload, payloadCapacity, sizeof(ProjectSnapshotV1)) == FrameStatus::BadVersion);
    }
    SECTION("bad size")
    {
        frame[6] = 0x00; frame[7] = 0x00; // payloadSize 0
        REQUIRE(readFrameHeader(frame, payload, payloadCapacity, sizeof(ProjectSnapshotV1)) == FrameStatus::BadSize);
    }
    SECTION("bad crc")
    {
        frame[12] ^= 0xA5; // corrupt payload
        REQUIRE(readFrameHeader(frame, payload, payloadCapacity, sizeof(ProjectSnapshotV1)) == FrameStatus::BadCrc);
    }
}

TEST_CASE("project snapshot validation rejects out-of-range settings", "[persistence]")
{
    ProjectSnapshotV1 s{};
    seedValidStepCounts(s);
    s.settings.tempoBpm = 120.0f; // zeroed tempo (0 BPM) is itself out of range
    REQUIRE(validateProjectSnapshot(s)); // seeded defaults in range
    s.settings.tempoBpm = 999.0f;
    REQUIRE_FALSE(validateProjectSnapshot(s));
    s.settings.tempoBpm = 120.0f;
    s.settings.currentScale = 13;
    REQUIRE_FALSE(validateProjectSnapshot(s));
    s.settings.currentScale = 0;
    s.settings.shuffleIndex = 16;
    REQUIRE_FALSE(validateProjectSnapshot(s));
    s.settings.shuffleIndex = 0;
    s.settings.themeIndex = -1;
    REQUIRE_FALSE(validateProjectSnapshot(s));
    s.settings.themeIndex = 10;
    REQUIRE_FALSE(validateProjectSnapshot(s));
}

TEST_CASE("pattern codec round-trips values, lengths, and shrink-grown tails", "[persistence]")
{
    Sequencer seq(1);
    seq.initializeParameters();
    // Note programming is gate-controlled: raise the gate steps first, as the
    // UI flow does.
    seq.setStepParameterValue(ParamId::Gate, 0, 1.0f);
    seq.setStepParameterValue(ParamId::Gate, 1, 1.0f);
    seq.setStepParameterValue(ParamId::Note, 0, 12.0f);
    seq.setStepParameterValue(ParamId::Note, 1, 19.0f);

    // Program the tail while the Gate track is long, then shrink: the tail
    // survives in raw storage (a later grow re-fills it with the default,
    // matching live ParameterTrack semantics).
    seq.setParameterStepCount(ParamId::Gate, 32);
    seq.setStepParameterValue(ParamId::Gate, 20, 1.0f);
    seq.setParameterStepCount(ParamId::Gate, 16);

    persistence::PatternSnapshot snap;
    persistence::capturePattern(seq, snap);

    Sequencer restored(1);
    restored.initializeParameters();
    persistence::applyPattern(snap, restored);

    REQUIRE(restored.getStepParameterValue(ParamId::Note, 0) == 12.0f);
    REQUIRE(restored.getStepParameterValue(ParamId::Note, 1) == 19.0f);
    REQUIRE(restored.getParameterStepCount(ParamId::Note) == 16);
    REQUIRE(restored.getParameterStepCount(ParamId::Gate) == 16);
    // Tail beyond the Gate length survived the round-trip (raw view).
    REQUIRE(restored.getRawStepValue(ParamId::Gate, 20) == 1.0f);
    // Growing re-fills from the previous length with the default — identical
    // to the live track's shrink-then-grow behavior (lossy by design).
    restored.setParameterStepCount(ParamId::Gate, 32);
    REQUIRE(restored.getStepParameterValue(ParamId::Gate, 20) == 0.0f);
}

TEST_CASE("pattern codec preserves every track of a random pattern", "[persistence]")
{
    Sequencer seq(2);
    seq.initializeParameters();
    seq.randomizeParameters();
    persistence::PatternSnapshot snap;
    persistence::capturePattern(seq, snap);

    Sequencer restored(2);
    restored.initializeParameters();
    persistence::applyPattern(snap, restored);
    for (uint8_t t = 0; t < PARAM_ID_COUNT; ++t)
    {
        const ParamId id = static_cast<ParamId>(t);
        REQUIRE(restored.getParameterStepCount(id) == seq.getParameterStepCount(id));
        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
            REQUIRE(restored.getRawStepValue(id, step) == seq.getRawStepValue(id, step));
    }
}
