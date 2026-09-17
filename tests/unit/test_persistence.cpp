#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <type_traits>
#include "persistence/SnapshotFormat.h"
#include "persistence/ProjectSnapshot.h"
#include "persistence/PatternCodec.h"
#include "persistence/RetainedSessionLogic.h"
#include "sequencer/Sequencer.h"
#include "voice/PatchCodec.h"
#include "voice/VoiceConfig.h"
#include "voice/VoicePresets.h"

using namespace persistence;

TEST_CASE("crc32 matches the ISO-HDLC check vector", "[persistence]")
{
    const char *v = "123456789";
    REQUIRE(crc32(reinterpret_cast<const uint8_t *>(v), 9) == 0xCBF43926u);
    REQUIRE(crc32(nullptr, 0) == 0u);
}

TEST_CASE("project snapshot size is locked", "[persistence]")
{
    STATIC_REQUIRE(sizeof(ProjectSnapshotV1) == 10312u);
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

TEST_CASE("pattern codec caps restored lengths without losing stored steps", "[persistence]")
{
    Sequencer seq(1);
    seq.initializeParameters();
    seq.setParameterStepCount(ParamId::Gate, 32);
    seq.setStepParameterValue(ParamId::Gate, 20, 1.0f);
    seq.setParameterStepCount(ParamId::Note, 12);

    persistence::PatternSnapshot snap;
    persistence::capturePattern(seq, snap);
    REQUIRE(snap.tracks[static_cast<uint8_t>(ParamId::Gate)].stepCount == 32);

    Sequencer restored(1);
    restored.initializeParameters();
    persistence::applyPattern(snap, restored, 16);

    REQUIRE(restored.getParameterStepCount(ParamId::Gate) == 16);
    REQUIRE(restored.getParameterStepCount(ParamId::Note) == 12); // shorter lanes keep their length
    REQUIRE(restored.getRawStepValue(ParamId::Gate, 20) == 1.0f);  // the capped tail stays stored
}

TEST_CASE("patch codec round-trips a preset untouched", "[persistence]")
{
    const VoiceConfig original = VoicePresets::getPresetConfig(4); // default voice-0 preset (Square)
    persistence::PatchSnapshot snap;
    voicecodec::capturePatch(original, snap);
    snap.presetIndex = 4; // stamped by the caller (Session), not by capturePatch

    VoiceConfig restored;
    REQUIRE(voicecodec::applyPatch(4, snap, restored));
    REQUIRE(restored.engine == original.engine);
    REQUIRE(restored.paramSet == original.paramSet);
    REQUIRE(restored.baseNote == original.baseNote);
    REQUIRE(restored.oscWaveforms[0] == original.oscWaveforms[0]);
    REQUIRE(restored.filterRes == original.filterRes);
    REQUIRE(restored.defaultAttack == original.defaultAttack);
    REQUIRE(restored.enabled == original.enabled);
}

TEST_CASE("patch codec round-trips an edited patch", "[persistence]")
{
    VoiceConfig original = VoicePresets::getPresetConfig(2); // Bass
    original.baseNote = 7.0f;
    original.oscDetuning[1] = -5.5f;
    original.harmony[2] = 7;
    original.wgT60 = 4.25f;
    original.filterMode = VoiceFilterMode::BP12;
    original.hasOverdrive = true;
    original.overdriveDrive = 0.9f;
    original.outputLevel = 0.31f;

    persistence::PatchSnapshot snap;
    voicecodec::capturePatch(original, snap);
    VoiceConfig restored;
    REQUIRE(voicecodec::applyPatch(2, snap, restored));
    REQUIRE(restored.baseNote == 7.0f);
    REQUIRE(restored.oscDetuning[1] == -5.5f);
    REQUIRE(restored.harmony[2] == 7);
    REQUIRE(restored.wgT60 == 4.25f);
    REQUIRE(restored.filterMode == VoiceFilterMode::BP12);
    REQUIRE(restored.hasOverdrive);
    REQUIRE(restored.overdriveDrive == 0.9f);
    REQUIRE(restored.outputLevel == 0.31f);
}

TEST_CASE("paramSet change clears the preset layout pointer", "[persistence]")
{
    const VoiceConfig preset = VoicePresets::getPresetConfig(2);
    VoiceConfig edited = preset;
    edited.paramSet = PARAMSET_WAVEGUIDE; // user re-purposed the slots
    persistence::PatchSnapshot snap;
    voicecodec::capturePatch(edited, snap);
    VoiceConfig restored;
    REQUIRE(voicecodec::applyPatch(2, snap, restored));
    REQUIRE(restored.paramSet == PARAMSET_WAVEGUIDE);
    REQUIRE(restored.parameters == nullptr); // layout() now derives from paramSet
}

TEST_CASE("hard-sync waveform edits keep an oscillator preset's cutoff layout", "[persistence]")
{
    const uint8_t digital = static_cast<uint8_t>(VoicePresets::findPreset("Digital"));
    const VoiceConfig preset = VoicePresets::getPresetConfig(digital);
    REQUIRE(preset.parameters != nullptr);
    VoiceConfig edited = preset;
    edited.oscWaveforms[0] = WAVE_HARDSYNC_SAW;
    edited.paramSet = PARAMSET_HARDSYNC; // what the editor derives from the bank
    persistence::PatchSnapshot snap;
    voicecodec::capturePatch(edited, snap);
    VoiceConfig restored;
    REQUIRE(voicecodec::applyPatch(digital, snap, restored));
    REQUIRE(restored.paramSet == PARAMSET_HARDSYNC);
    REQUIRE(restored.parameters == preset.parameters); // cutoff lane survives reload
}

TEST_CASE("recipe engine without a recipe source is rejected", "[persistence]")
{
    const VoiceConfig preset = VoicePresets::getPresetConfig(2); // non-recipe preset
    VoiceConfig edited = preset;
    edited.engine = ENGINE_RECIPE;
    persistence::PatchSnapshot snap;
    voicecodec::capturePatch(edited, snap);
    VoiceConfig restored;
    REQUIRE_FALSE(voicecodec::applyPatch(2, snap, restored));
    REQUIRE(restored.engine == preset.engine); // fell back to factory preset
}

TEST_CASE("out-of-range preset index is rejected", "[persistence]")
{
    persistence::PatchSnapshot snap;
    VoiceConfig restored;
    REQUIRE_FALSE(voicecodec::applyPatch(VoicePresets::getPresetCount(), snap, restored));
}

TEST_CASE("golden full-project round-trip through frame bytes", "[persistence]")
{
    Sequencer seq0(1), seq1(2), seq2(3), seq3(4);
    ProjectSnapshotV1 snap{};
    Sequencer *seqs[4] = {&seq0, &seq1, &seq2, &seq3};
    for (int v = 0; v < 4; ++v)
    {
        Sequencer &seq = *seqs[v];
        seq.initializeParameters();
        // Note programming is gate-controlled: raise the gate step first.
        seq.setStepParameterValue(ParamId::Gate, static_cast<uint8_t>(v), 1.0f);
        seq.setStepParameterValue(ParamId::Note, static_cast<uint8_t>(v), 3.0f * v + 1.0f);
        seq.setParameterStepCount(ParamId::Gate, static_cast<uint8_t>(12 + v));
        capturePattern(seq, snap.patterns[v]);
        snap.settings.presetIndices[v] = static_cast<uint8_t>(v);
    }
    snap.settings.tempoBpm = 137.0f;
    snap.settings.masterVolume = 0.66f;
    snap.settings.currentScale = 7;
    snap.settings.shuffleIndex = 3;
    snap.settings.themeIndex = 4;
    snap.settings.selectedVoice = 2;
    snap.settings.editorCursor[0] = 11; // VoiceEdit::Id::T60
    snap.settings.changedFlags = 0x05;
    REQUIRE(validateProjectSnapshot(snap));

    // Frame to bytes and back, like the flash file does.
    uint8_t frame[12 + sizeof(ProjectSnapshotV1)];
    writeFrameHeader(frame, sizeof(snap), crc32(reinterpret_cast<const uint8_t *>(&snap), sizeof(snap)));
    std::memcpy(frame + 12, &snap, sizeof(snap));
    REQUIRE(readFrameHeader(frame, frame + 12, sizeof(snap), sizeof(ProjectSnapshotV1)) ==
            FrameStatus::Ok);

    ProjectSnapshotV1 loaded{};
    std::memcpy(&loaded, frame + 12, sizeof(loaded));
    Sequencer rest0(1), rest1(2), rest2(3), rest3(4);
    Sequencer *restored[4] = {&rest0, &rest1, &rest2, &rest3};
    for (int v = 0; v < 4; ++v)
    {
        restored[v]->initializeParameters();
        applyPattern(loaded.patterns[v], *restored[v]);
        REQUIRE(restored[v]->getStepParameterValue(ParamId::Note, static_cast<uint8_t>(v)) == 3.0f * v + 1.0f);
        REQUIRE(restored[v]->getParameterStepCount(ParamId::Gate) == 12u + v);
    }
    REQUIRE(loaded.settings.tempoBpm == 137.0f);
    REQUIRE(loaded.settings.changedFlags == 0x05);
}

TEST_CASE("resume decision table", "[persistence]")
{
    using persistence::ResumeDecision;
    REQUIRE(persistence::decideResume(false, false, 0) == ResumeDecision::NormalBoot);
    REQUIRE(persistence::decideResume(false, true, 2) == ResumeDecision::NormalBoot);
    REQUIRE(persistence::decideResume(true, false, 0) == ResumeDecision::HaltRecovery);
    REQUIRE(persistence::decideResume(true, true, 0) == ResumeDecision::ResumeRetained);
    REQUIRE(persistence::decideResume(true, true, 2) == ResumeDecision::ResumeRetained);
    REQUIRE(persistence::decideResume(true, true, 3) == ResumeDecision::HaltRecovery);
}

TEST_CASE("retained validity and refresh", "[persistence]")
{
    persistence::RetainedStore store{};
    REQUIRE_FALSE(persistence::retainedValid(store)); // zeroed = no magic
    persistence::ProjectSnapshotV1 snap{};
    seedValidStepCounts(snap);
    snap.settings.tempoBpm = 111.0f;
    persistence::retainedRefresh(store, snap);
    REQUIRE(persistence::retainedValid(store));
    REQUIRE(store.header.generation == 1);
    persistence::retainedRefresh(store, snap);
    REQUIRE(store.header.generation == 2);
    // CRC damage invalidates.
    store.snapshot.settings.tempoBpm = 222.0f; // edited without refresh
    REQUIRE_FALSE(persistence::retainedValid(store));
    // Wrong version invalidates.
    persistence::retainedRefresh(store, snap);
    REQUIRE(persistence::retainedValid(store));
    store.header.version = 99;
    REQUIRE_FALSE(persistence::retainedValid(store));
}
