#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>
#include "persistence/SnapshotFormat.h"
#include "persistence/ProjectSnapshot.h"
#include "persistence/PatternCodec.h"
#include "persistence/RetainedSessionLogic.h"
#include "scales/scales.h"
#include "tuning/Tuning.h"
#include "sequencer/Sequencer.h"
#include "voice/EffectsCodec.h"
#include "voice/PatchCodec.h"
#include "voice/ReverbSettings.h"
#include "voice/VoiceManager.h"
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
    STATIC_REQUIRE(sizeof(ProjectSnapshotV2) == 12400u);
    STATIC_REQUIRE(sizeof(EffectsSnapshot) == 48u);
    STATIC_REQUIRE(sizeof(ProjectSnapshotV3) == 12448u);
    STATIC_REQUIRE(sizeof(TuningSnapshot) == 12u);
    STATIC_REQUIRE(sizeof(ProjectSnapshot) == 12460u);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ProjectSnapshot>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<TuningSnapshot>);
    STATIC_REQUIRE(SNAPSHOT_FORMAT_VERSION == 4);
    STATIC_REQUIRE(RETAINED_VERSION == SNAPSHOT_FORMAT_VERSION); // bumped together
    // Every older payload is the exact prefix of the newer layout.
    STATIC_REQUIRE(offsetof(ProjectSnapshotV2, envelopes) == sizeof(ProjectSnapshotV1));
    STATIC_REQUIRE(offsetof(ProjectSnapshot, effects) == sizeof(ProjectSnapshotV2));
    STATIC_REQUIRE(offsetof(ProjectSnapshot, tuning) == sizeof(ProjectSnapshotV3));
    CHECK(payloadSizeForVersion(SNAPSHOT_FORMAT_VERSION_V1) == sizeof(ProjectSnapshotV1));
    CHECK(payloadSizeForVersion(SNAPSHOT_FORMAT_VERSION_V2) == sizeof(ProjectSnapshotV2));
    CHECK(payloadSizeForVersion(SNAPSHOT_FORMAT_VERSION_V3) == sizeof(ProjectSnapshotV3));
    CHECK(payloadSizeForVersion(SNAPSHOT_FORMAT_VERSION) == sizeof(ProjectSnapshot));
    CHECK(payloadSizeForVersion(0) == 0u);
    CHECK(payloadSizeForVersion(SNAPSHOT_FORMAT_VERSION + 1) == 0u); // newer than this build
    CHECK(payloadSizeForVersion(0xFFFF) == 0u);
}

namespace
{
// Zero-initialized stepCounts are INVALID (validate requires 1..64); every
// test that wants a range-valid snapshot seeds lengths first.
void seedValidStepCounts(ProjectSnapshot &s)
{
    for (int v = 0; v < 4; ++v)
    {
        for (auto &track : s.patterns[v].tracks)
            track.stepCount = 16;
        for (auto &track : s.envelopes[v].tracks)
            track.stepCount = 16;
    }
    s.laneModel = LANE_MODEL_ABSOLUTE;
    applyEffectsDefaults(s.effects); // a zeroed effect record (decay 0 s) is invalid
    applyTuningDefaults(s.tuning);   // and so is a zeroed tuning record (A4 0 Hz)
}
} // namespace

TEST_CASE("frame header round-trips and rejects damage", "[persistence]")
{
    ProjectSnapshot snap{};
    seedValidStepCounts(snap);
    snap.settings.tempoBpm = 120.0f;
    snap.settings.currentScale = 5;
    REQUIRE(validateProjectSnapshot(snap));

    uint8_t frame[12 + sizeof(ProjectSnapshot)];
    writeFrameHeader(frame, sizeof(snap),
                     crc32(reinterpret_cast<const uint8_t *>(&snap), sizeof(snap)));
    std::memcpy(frame + 12, &snap, sizeof(snap));

    const uint8_t *payload = frame + 12;
    const size_t payloadCapacity = sizeof(frame) - 12;
    REQUIRE(readFrameHeader(frame, payload, payloadCapacity, sizeof(ProjectSnapshot)) ==
            FrameStatus::Ok);

    SECTION("too short")
    {
        REQUIRE(readFrameHeader(frame, payload, payloadCapacity - 1,
                                sizeof(ProjectSnapshot)) == FrameStatus::TooShort);
    }
    SECTION("bad magic")
    {
        frame[0] ^= 0xFF;
        REQUIRE(readFrameHeader(frame, payload, payloadCapacity, sizeof(ProjectSnapshot)) == FrameStatus::BadMagic);
    }
    SECTION("bad version")
    {
        frame[4] = 0x63; frame[5] = 0x00; // version 99
        REQUIRE(readFrameHeader(frame, payload, payloadCapacity, sizeof(ProjectSnapshot)) == FrameStatus::BadVersion);
    }
    SECTION("bad size")
    {
        frame[6] = 0x00; frame[7] = 0x00; // payloadSize 0
        REQUIRE(readFrameHeader(frame, payload, payloadCapacity, sizeof(ProjectSnapshot)) == FrameStatus::BadSize);
    }
    SECTION("bad crc")
    {
        frame[12] ^= 0xA5; // corrupt payload
        REQUIRE(readFrameHeader(frame, payload, payloadCapacity, sizeof(ProjectSnapshot)) == FrameStatus::BadCrc);
    }
}

TEST_CASE("project snapshot validation rejects out-of-range settings", "[persistence]")
{
    ProjectSnapshot s{};
    seedValidStepCounts(s);
    s.settings.tempoBpm = 120.0f; // zeroed tempo (0 BPM) is itself out of range
    REQUIRE(validateProjectSnapshot(s)); // seeded defaults in range
    s.settings.tempoBpm = 999.0f;
    REQUIRE_FALSE(validateProjectSnapshot(s));
    s.settings.tempoBpm = 120.0f;
    s.settings.currentScale = static_cast<uint8_t>(SCALES_COUNT - 1); // the last scale is valid
    REQUIRE(validateProjectSnapshot(s));
    s.settings.currentScale = static_cast<uint8_t>(SCALES_COUNT);
    REQUIRE_FALSE(validateProjectSnapshot(s));
    s.settings.currentScale = 255;
    REQUIRE_FALSE(validateProjectSnapshot(s));
    s.settings.currentScale = 0;
    s.settings.shuffleIndex = 16;
    REQUIRE_FALSE(validateProjectSnapshot(s));
    s.settings.shuffleIndex = 0;
    s.settings.themeIndex = -1;
    REQUIRE_FALSE(validateProjectSnapshot(s));
    s.settings.themeIndex = 10;
    REQUIRE_FALSE(validateProjectSnapshot(s));
    s.settings.themeIndex = 0;
    s.envelopes[2].tracks[1].stepCount = 0;
    REQUIRE_FALSE(validateProjectSnapshot(s));
    s.envelopes[2].tracks[1].stepCount = 16;
    s.laneModel = 7;
    REQUIRE_FALSE(validateProjectSnapshot(s));
}

TEST_CASE("a format-1 file loads as the prefix of format 2", "[persistence]")
{
    // Build a format-1 payload exactly as old firmware saved it.
    ProjectSnapshot old{};
    seedValidStepCounts(old);
    old.settings.tempoBpm = 101.0f;
    old.patterns[1].tracks[static_cast<uint8_t>(ParamId::Decay)].values[3] = 0.8f;
    uint8_t frame[12 + sizeof(ProjectSnapshotV1)];
    std::memcpy(frame + 12, &old, sizeof(ProjectSnapshotV1));
    writeFrameHeader(frame, sizeof(ProjectSnapshotV1), crc32(frame + 12, sizeof(ProjectSnapshotV1)));
    frame[4] = static_cast<uint8_t>(SNAPSHOT_FORMAT_VERSION_V1);
    frame[5] = 0;

    // The loader reads the version first, then the matching payload size.
    REQUIRE(frameVersion(frame) == SNAPSHOT_FORMAT_VERSION_V1);
    ProjectSnapshot loaded{};
    std::memset(&loaded, 0x5A, sizeof(loaded)); // garbage past the prefix
    std::memcpy(&loaded, frame + 12, sizeof(ProjectSnapshotV1));
    REQUIRE(readFrameHeader(frame, reinterpret_cast<const uint8_t *>(&loaded), sizeof(loaded),
                            sizeof(ProjectSnapshotV1), SNAPSHOT_FORMAT_VERSION_V1) == FrameStatus::Ok);
    // A format-1 frame is not a format-2 frame.
    REQUIRE(readFrameHeader(frame, reinterpret_cast<const uint8_t *>(&loaded), sizeof(loaded),
                            sizeof(ProjectSnapshotV1)) == FrameStatus::BadVersion);
    upgradeFromV1(loaded);
    REQUIRE(validateProjectSnapshot(loaded));
    CHECK(loaded.laneModel == LANE_MODEL_OFFSETS);
    CHECK(loaded.settings.tempoBpm == 101.0f);
    CHECK(loaded.patterns[1].tracks[static_cast<uint8_t>(ParamId::Decay)].values[3] == 0.8f);
    for (const auto &voice : loaded.envelopes)
        for (const auto &track : voice.tracks)
        {
            CHECK(track.stepCount == SequencerConstants::DEFAULT_STEPS_COUNT);
            CHECK(followsPatch(track.values[0]));
            CHECK(followsPatch(track.values[63]));
        }
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
    persistence::EnvelopeTracksSnapshot envelopes;
    persistence::capturePattern(seq, snap, envelopes);

    Sequencer restored(1);
    restored.initializeParameters();
    persistence::applyPattern(snap, envelopes, restored);

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

TEST_CASE("pattern codec round-trips lane loop starts; zero bytes load as step 0", "[persistence][loop_range]")
{
    Sequencer seq(1);
    seq.initializeParameters();
    seq.setParameterLoop(ParamId::Velocity, 3, 7);

    persistence::PatternSnapshot snap;
    persistence::EnvelopeTracksSnapshot envelopes;
    persistence::capturePattern(seq, snap, envelopes);
    CHECK(snap.tracks[static_cast<size_t>(ParamId::Velocity)].reserved[0] == 3);
    CHECK(snap.tracks[static_cast<size_t>(ParamId::Filter)].reserved[0] == 0);

    Sequencer restored(1);
    restored.initializeParameters();
    restored.setParameterLoop(ParamId::Filter, 2, 9); // must be replaced by the file
    persistence::applyPattern(snap, envelopes, restored);
    REQUIRE(restored.getParameterLoopStart(ParamId::Velocity) == 3);
    REQUIRE(restored.getParameterStepCount(ParamId::Velocity) == 8);
    REQUIRE(restored.getParameterLoopStart(ParamId::Filter) == 0);

    // A start that no longer fits the (capped) length loads as 0.
    snap.tracks[static_cast<size_t>(ParamId::Velocity)].reserved[0] = 7;
    persistence::applyPattern(snap, envelopes, restored);
    REQUIRE(restored.getParameterLoopStart(ParamId::Velocity) == 0);
}

TEST_CASE("pattern codec preserves every track of a random pattern", "[persistence]")
{
    Sequencer seq(2);
    seq.initializeParameters();
    seq.randomizeParameters();
    persistence::PatternSnapshot snap;
    persistence::EnvelopeTracksSnapshot envelopes;
    persistence::capturePattern(seq, snap, envelopes);

    Sequencer restored(2);
    restored.initializeParameters();
    persistence::applyPattern(snap, envelopes, restored);
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
    persistence::EnvelopeTracksSnapshot envelopes;
    persistence::capturePattern(seq, snap, envelopes);
    REQUIRE(snap.tracks[static_cast<uint8_t>(ParamId::Gate)].stepCount == 32);

    Sequencer restored(1);
    restored.initializeParameters();
    persistence::applyPattern(snap, envelopes, restored, 16);

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
    ProjectSnapshot snap{};
    Sequencer *seqs[4] = {&seq0, &seq1, &seq2, &seq3};
    for (int v = 0; v < 4; ++v)
    {
        Sequencer &seq = *seqs[v];
        seq.initializeParameters();
        // Note programming is gate-controlled: raise the gate step first.
        seq.setStepParameterValue(ParamId::Gate, static_cast<uint8_t>(v), 1.0f);
        seq.setStepParameterValue(ParamId::Note, static_cast<uint8_t>(v), 3.0f * v + 1.0f);
        seq.setParameterStepCount(ParamId::Gate, static_cast<uint8_t>(12 + v));
        seq.setStepParameterValue(ParamId::Release, static_cast<uint8_t>(v), 0.1f * v);
        capturePattern(seq, snap.patterns[v], snap.envelopes[v]);
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
    snap.laneModel = LANE_MODEL_ABSOLUTE;
    applyEffectsDefaults(snap.effects);
    applyTuningDefaults(snap.tuning);
    REQUIRE(validateProjectSnapshot(snap));

    // Frame to bytes and back, like the flash file does.
    uint8_t frame[12 + sizeof(ProjectSnapshot)];
    writeFrameHeader(frame, sizeof(snap), crc32(reinterpret_cast<const uint8_t *>(&snap), sizeof(snap)));
    std::memcpy(frame + 12, &snap, sizeof(snap));
    REQUIRE(readFrameHeader(frame, frame + 12, sizeof(snap), sizeof(ProjectSnapshot)) ==
            FrameStatus::Ok);

    ProjectSnapshot loaded{};
    std::memcpy(&loaded, frame + 12, sizeof(loaded));
    Sequencer rest0(1), rest1(2), rest2(3), rest3(4);
    Sequencer *restored[4] = {&rest0, &rest1, &rest2, &rest3};
    for (int v = 0; v < 4; ++v)
    {
        restored[v]->initializeParameters();
        applyPattern(loaded.patterns[v], loaded.envelopes[v], *restored[v]);
        REQUIRE(restored[v]->getStepParameterValue(ParamId::Release, static_cast<uint8_t>(v)) == 0.1f * v);
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
    persistence::ProjectSnapshot snap{};
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
    // RAM left by firmware with the previous layout (version 2, CRC over a
    // different-sized snapshot) must never be replayed as a format-4 song.
    persistence::retainedRefresh(store, snap);
    store.header.version = SNAPSHOT_FORMAT_VERSION_V2;
    REQUIRE_FALSE(persistence::retainedValid(store));
    persistence::retainedRefresh(store, snap);
    store.header.version = SNAPSHOT_FORMAT_VERSION_V3; // the last build before tunings
    REQUIRE_FALSE(persistence::retainedValid(store));
}

// ---- Format 3: effect settings, migration from v1/v2, validation --------------------
namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

// A frame for `payloadBytes` of `snap` stamped with `version`, as that firmware wrote it.
std::vector<uint8_t> makeFrame(const ProjectSnapshot &snap, uint16_t version, size_t payloadBytes)
{
    std::vector<uint8_t> frame(12 + payloadBytes);
    std::memcpy(frame.data() + 12, &snap, payloadBytes);
    writeFrameHeader(frame.data(), static_cast<uint32_t>(payloadBytes),
                     crc32(frame.data() + 12, payloadBytes));
    frame[4] = static_cast<uint8_t>(version);
    frame[5] = static_cast<uint8_t>(version >> 8);
    return frame;
}

bool decode(const std::vector<uint8_t> &frame, ProjectSnapshot &out)
{
    return decodeSnapshotFrame(frame.data(), frame.data() + 12, frame.size() - 12, out);
}

bool defaultsEqual(const EffectsSnapshot &e)
{
    EffectsSnapshot defaults;
    applyEffectsDefaults(defaults);
    return std::memcmp(&e, &defaults, sizeof e) == 0;
}

ProjectSnapshot validSnapshot()
{
    ProjectSnapshot snap{};
    seedValidStepCounts(snap);
    snap.settings.tempoBpm = 118.0f;
    snap.settings.masterVolume = 0.7f;
    snap.settings.currentScale = 3;
    snap.patterns[2].tracks[static_cast<uint8_t>(ParamId::Note)].values[5] = 7.0f;
    return snap;
}
} // namespace

TEST_CASE("effect defaults are the documented audition values with reverb mix at zero", "[persistence][effects]")
{
    EffectsSnapshot e;
    std::memset(&e, 0x5A, sizeof e);
    applyEffectsDefaults(e);
    CHECK(e.reverbMix == 0.0f); // an upgraded project sounds exactly as before
    CHECK(e.reverbDecaySeconds == 20.0f);
    CHECK(e.reverbDampingHz == 3000.0f);
    CHECK(e.reverbLowCutHz == 40.0f);
    CHECK(e.reverbDiffusion == 0.8f);
    CHECK(e.reverbModDepth == 0.5f);
    CHECK(e.reverbModRateHz == 0.5f);
    CHECK(e.reverbWidth == 1.0f);
    for (uint32_t word : e.reserved) CHECK(word == 0u);
    CHECK(validateEffects(e));

    // The flash-side limits and the audio-side ReverbParams are two copies of one
    // set of numbers: they must never drift apart.
    using namespace persistence::EffectsLimits;
    CHECK(kReverbMixMin == ReverbParams::kMixMin);
    CHECK(kReverbMixMax == ReverbParams::kMixMax);
    CHECK(kReverbMixDefault == ReverbParams::kMixDefault);
    CHECK(kReverbDecayMin == ReverbParams::kDecayMin);
    CHECK(kReverbDecayMax == ReverbParams::kDecayMax);
    CHECK(kReverbDecayDefault == ReverbParams::kDecayDefault);
    CHECK(kReverbDampingMin == ReverbParams::kDampingMin);
    CHECK(kReverbDampingMax == ReverbParams::kDampingMax);
    CHECK(kReverbDampingDefault == ReverbParams::kDampingDefault);
    CHECK(kReverbLowCutMin == ReverbParams::kLowCutMin);
    CHECK(kReverbLowCutMax == ReverbParams::kLowCutMax);
    CHECK(kReverbLowCutDefault == ReverbParams::kLowCutDefault);
    CHECK(kReverbDiffusionMin == ReverbParams::kDiffusionMin);
    CHECK(kReverbDiffusionMax == ReverbParams::kDiffusionMax);
    CHECK(kReverbDiffusionDefault == ReverbParams::kDiffusionDefault);
    CHECK(kReverbModDepthMin == ReverbParams::kModDepthMin);
    CHECK(kReverbModDepthMax == ReverbParams::kModDepthMax);
    CHECK(kReverbModDepthDefault == ReverbParams::kModDepthDefault);
    CHECK(kReverbModRateMin == ReverbParams::kModRateMin);
    CHECK(kReverbModRateMax == ReverbParams::kModRateMax);
    CHECK(kReverbModRateDefault == ReverbParams::kModRateDefault);
    CHECK(kReverbWidthMin == ReverbParams::kWidthMin);
    CHECK(kReverbWidthMax == ReverbParams::kWidthMax);
    CHECK(kReverbWidthDefault == ReverbParams::kWidthDefault);
    // ...and the defaults are what a fresh ReverbSettings holds.
    const ReverbSettings fresh;
    CHECK(fresh.mix == kReverbMixDefault);
    CHECK(fresh.decaySeconds == kReverbDecayDefault);
    CHECK(fresh.dampingHz == kReverbDampingDefault);
    CHECK(fresh.lowCutHz == kReverbLowCutDefault);
    CHECK(fresh.diffusion == kReverbDiffusionDefault);
    CHECK(fresh.modDepth == kReverbModDepthDefault);
    CHECK(fresh.modRateHz == kReverbModRateDefault);
    CHECK(fresh.width == kReverbWidthDefault);
    CHECK_FALSE(fresh.freeze);
}

TEST_CASE("a format-2 file loads as the prefix of format 3 with default effects", "[persistence][effects]")
{
    ProjectSnapshot old = validSnapshot();
    // Format-2 firmware never wrote the tail: whatever follows is not part of the file.
    const auto frame = makeFrame(old, SNAPSHOT_FORMAT_VERSION_V2, sizeof(ProjectSnapshotV2));
    REQUIRE(frameVersion(frame.data()) == SNAPSHOT_FORMAT_VERSION_V2);
    REQUIRE(frame.size() == 12 + sizeof(ProjectSnapshotV2));

    SECTION("into a separate buffer")
    {
        ProjectSnapshot loaded;
        std::memset(&loaded, 0x5A, sizeof loaded); // garbage everywhere, including the tail
        REQUIRE(decode(frame, loaded));
        CHECK(defaultsEqual(loaded.effects));
        CHECK(loaded.settings.tempoBpm == 118.0f);
        CHECK(loaded.laneModel == LANE_MODEL_ABSOLUTE); // v2 keeps its lane model
        CHECK(loaded.patterns[2].tracks[static_cast<uint8_t>(ParamId::Note)].values[5] == 7.0f);
        CHECK(validateProjectSnapshot(loaded));
    }
    SECTION("in place, the way the flash loader decodes into its static buffer")
    {
        ProjectSnapshot buffer;
        std::memset(&buffer, 0xC3, sizeof buffer);
        std::memcpy(&buffer, frame.data() + 12, sizeof(ProjectSnapshotV2)); // what f.read() left there
        REQUIRE(decodeSnapshotFrame(frame.data(), reinterpret_cast<const uint8_t *>(&buffer),
                                    sizeof buffer, buffer));
        CHECK(defaultsEqual(buffer.effects));
        CHECK(buffer.settings.masterVolume == 0.7f);
    }
    SECTION("a v2 frame is not a v3 frame")
    {
        ProjectSnapshot loaded{};
        CHECK(readFrameHeader(frame.data(), frame.data() + 12, frame.size() - 12, sizeof(ProjectSnapshotV2)) ==
              FrameStatus::BadVersion); // expected version defaults to the current one
        CHECK(readFrameHeader(frame.data(), frame.data() + 12, frame.size() - 12, sizeof(ProjectSnapshot),
                              SNAPSHOT_FORMAT_VERSION_V2) == FrameStatus::BadSize);
        (void)loaded;
    }
}

TEST_CASE("a format-1 file upgrades through decodeSnapshotFrame with default effects", "[persistence][effects]")
{
    ProjectSnapshot old = validSnapshot();
    old.laneModel = 0;
    const auto frame = makeFrame(old, SNAPSHOT_FORMAT_VERSION_V1, sizeof(ProjectSnapshotV1));
    ProjectSnapshot loaded;
    std::memset(&loaded, 0x5A, sizeof loaded);
    REQUIRE(decode(frame, loaded));
    CHECK(loaded.laneModel == LANE_MODEL_OFFSETS); // Session converts the lanes later
    CHECK(defaultsEqual(loaded.effects));
    CHECK(loaded.settings.tempoBpm == 118.0f);
    for (const auto &voice : loaded.envelopes)
        for (const auto &track : voice.tracks)
            CHECK(track.stepCount == SequencerConstants::DEFAULT_STEPS_COUNT);
}

TEST_CASE("a format-3 file round-trips its effect settings bit for bit", "[persistence][effects]")
{
    ProjectSnapshot snap = validSnapshot();
    ReverbSettings live;
    live.mix = 0.37f;
    live.decaySeconds = 4.5f;
    live.dampingHz = 1750.0f;
    live.lowCutHz = 120.0f;
    live.diffusion = 0.35f;
    live.modDepth = 0.9f;
    live.modRateHz = 2.25f;
    live.width = 1.4f;
    live.freeze = true; // performance state: must not be stored
    effectscodec::captureEffects(live, snap.effects);

    const auto frame = makeFrame(snap, SNAPSHOT_FORMAT_VERSION, sizeof(ProjectSnapshot));
    REQUIRE(frameVersion(frame.data()) == SNAPSHOT_FORMAT_VERSION);
    ProjectSnapshot loaded{};
    REQUIRE(decode(frame, loaded));
    CHECK(std::memcmp(&loaded.effects, &snap.effects, sizeof(EffectsSnapshot)) == 0);

    ReverbSettings restored;
    restored.freeze = true;
    REQUIRE(effectscodec::applyEffects(loaded.effects, restored));
    CHECK(restored.mix == 0.37f);
    CHECK(restored.decaySeconds == 4.5f);
    CHECK(restored.dampingHz == 1750.0f);
    CHECK(restored.lowCutHz == 120.0f);
    CHECK(restored.diffusion == 0.35f);
    CHECK(restored.modDepth == 0.9f);
    CHECK(restored.modRateHz == 2.25f);
    CHECK(restored.width == 1.4f);
    CHECK_FALSE(restored.freeze); // restored unfrozen, whatever the live state was
}

TEST_CASE("nonfinite or out-of-range effect fields reject the file", "[persistence][effects]")
{
    struct Field
    {
        const char *name;
        float EffectsSnapshot::*member;
        float low, high;
    };
    const Field fields[] = {
        {"mix", &EffectsSnapshot::reverbMix, 0.0f, 1.0f},
        {"decay", &EffectsSnapshot::reverbDecaySeconds, 0.1f, 1000.0f},
        {"damping", &EffectsSnapshot::reverbDampingHz, 100.0f, 10800.0f},
        {"low cut", &EffectsSnapshot::reverbLowCutHz, 10.0f, 1000.0f},
        {"diffusion", &EffectsSnapshot::reverbDiffusion, 0.0f, 1.0f},
        {"mod depth", &EffectsSnapshot::reverbModDepth, 0.0f, 1.0f},
        {"mod rate", &EffectsSnapshot::reverbModRateHz, 0.01f, 5.0f},
        {"width", &EffectsSnapshot::reverbWidth, 0.0f, 2.0f},
    };
    for (const Field &field : fields)
    {
        CAPTURE(field.name);
        // One float beyond a limit must be refused too. A zero limit is skipped: the
        // next float below it is a denormal, which -ffast-math (flush-to-zero) reads
        // as zero, i.e. in range. Nearby normal values cover that side instead.
        std::vector<float> badValues = {kNaN, kInf, -kInf, field.low - 1.0f, field.high + 1.0f,
                                        field.low - 1.0e-3f, field.high + 1.0e-3f};
        if (field.low != 0.0f)
            badValues.push_back(std::nextafter(field.low, -1.0e9f));
        badValues.push_back(std::nextafter(field.high, 1.0e9f));
        for (const float bad : badValues)
        {
            CAPTURE(bad);
            ProjectSnapshot snap = validSnapshot();
            snap.effects.*(field.member) = bad;
            CHECK_FALSE(validateEffects(snap.effects));
            CHECK_FALSE(validateProjectSnapshot(snap));
            const auto frame = makeFrame(snap, SNAPSHOT_FORMAT_VERSION, sizeof(ProjectSnapshot));
            ProjectSnapshot loaded{};
            CHECK_FALSE(decode(frame, loaded)); // CRC is fine; the values are not
            ReverbSettings untouched;
            untouched.mix = 0.5f;
            CHECK_FALSE(effectscodec::applyEffects(snap.effects, untouched));
            CHECK(untouched.mix == 0.5f); // nothing published from a bad record
        }
        // The exact limits are valid.
        for (const float good : {field.low, field.high})
        {
            ProjectSnapshot snap = validSnapshot();
            snap.effects.*(field.member) = good;
            CHECK(validateEffects(snap.effects));
        }
    }
}

TEST_CASE("damaged and unknown frames are refused before anything is applied", "[persistence][effects]")
{
    const ProjectSnapshot snap = validSnapshot();
    const auto good = makeFrame(snap, SNAPSHOT_FORMAT_VERSION, sizeof(ProjectSnapshot));
    ProjectSnapshot out{};
    REQUIRE(decode(good, out));

    auto bad = good;
    bad[0] ^= 0xFF; // magic
    CHECK_FALSE(decode(bad, out));
    bad = good;
    bad[12 + 5000] ^= 0x01; // payload bit rot
    CHECK_FALSE(decode(bad, out));
    bad = good;
    bad[12 + sizeof(ProjectSnapshot) - 1] ^= 0x80; // inside the tuning record
    CHECK_FALSE(decode(bad, out));
    bad = good;
    bad[6] = 0x00; bad[7] = 0x00; // payload size 0
    CHECK_FALSE(decode(bad, out));
    for (const uint16_t version : {uint16_t{0}, static_cast<uint16_t>(SNAPSHOT_FORMAT_VERSION + 1), uint16_t{99}})
    {
        bad = good;
        bad[4] = static_cast<uint8_t>(version);
        bad[5] = static_cast<uint8_t>(version >> 8);
        CHECK_FALSE(decode(bad, out)); // a newer or unknown format is not guessed at
    }
    // A truncated capacity is refused rather than read past.
    CHECK_FALSE(decodeSnapshotFrame(good.data(), good.data() + 12, sizeof(ProjectSnapshot) - 1, out));
    // A v3-sized payload under a v2 header is a size mismatch.
    bad = good;
    bad[4] = static_cast<uint8_t>(SNAPSHOT_FORMAT_VERSION_V2);
    CHECK_FALSE(decode(bad, out));
}

TEST_CASE("effects codec captures live settings and never stores freeze", "[persistence][effects]")
{
    EffectsSnapshot e;
    std::memset(&e, 0xFF, sizeof e);
    ReverbSettings hostile;
    hostile.mix = 5.0f;          // out of range
    hostile.decaySeconds = kNaN; // nonfinite
    hostile.dampingHz = -3.0f;
    hostile.freeze = true;
    effectscodec::captureEffects(hostile, e);
    // Whatever the live state, the saved record is valid and its reserved words are zero.
    CHECK(validateEffects(e));
    CHECK(e.reverbMix == 1.0f);
    CHECK(e.reverbDecaySeconds == ReverbParams::kDecayDefault);
    CHECK(e.reverbDampingHz == ReverbParams::kDampingMin);
    for (uint32_t word : e.reserved) CHECK(word == 0u);

    // A whole project through captureEffects/apply reproduces the live settings.
    auto manager = std::make_unique<VoiceManager>(1);
    ReverbSettings live;
    live.mix = 0.6f;
    live.decaySeconds = 12.0f;
    live.freeze = true;
    manager->applyReverbSettings(live);
    ProjectSnapshot snap = validSnapshot();
    effectscodec::captureEffects(manager->getReverbSettings(), snap.effects);
    ReverbSettings back;
    REQUIRE(effectscodec::applyEffects(snap.effects, back));
    manager->applyReverbSettings(back); // what Session::applyAfterVoices does
    const ReverbSettings now = manager->getReverbSettings();
    CHECK(now.mix == 0.6f);
    CHECK(now.decaySeconds == 12.0f);
    CHECK_FALSE(now.freeze); // load turns a live freeze off
}

// ---- Format 4: the global tuning and its favourites ---------------------------------
namespace
{
bool sameTuning(const TuningSnapshot &a, const TuningSnapshot &b)
{
    return std::memcmp(&a, &b, sizeof a) == 0;
}

TuningSnapshot defaultTuningRecord()
{
    TuningSnapshot record;
    applyTuningDefaults(record);
    return record;
}
} // namespace

TEST_CASE("the default tuning record is 12-EDO on C at 440 with the default favourites", "[persistence][tuning]")
{
    const TuningSnapshot record = defaultTuningRecord();
    CHECK(record.tuningId == tuning::kDefaultTuningId);
    CHECK(record.tonic == 0);
    CHECK(record.a4Tenths == 4400);
    CHECK(validateTuning(record));
    for (const uint8_t byte : record.reserved)
        CHECK(byte == 0);

    tuning::Bank bank;
    tuning::defaultBank(bank);
    for (uint8_t slot = 0; slot < tuning::kFavoriteSlots; ++slot)
        CHECK(record.favorites[slot] == bank.favorites[slot]);

    tuning::Selection selection;
    tuning::Bank restoredBank;
    std::memset(&restoredBank, 0, sizeof restoredBank);
    REQUIRE(restoreTuning(record, selection, restoredBank));
    CHECK(tuning::isStandard(selection)); // the legacy MIDI-table pitch path
}

TEST_CASE("a format-3 file loads as the prefix of format 4 with the default tuning", "[persistence][tuning]")
{
    ProjectSnapshot old = validSnapshot();
    // Format-3 firmware never wrote the tail: whatever follows is not part of the file.
    old.tuning.tuningId = 96;
    old.tuning.tonic = 7;
    const auto frame = makeFrame(old, SNAPSHOT_FORMAT_VERSION_V3, sizeof(ProjectSnapshotV3));
    REQUIRE(frameVersion(frame.data()) == SNAPSHOT_FORMAT_VERSION_V3);
    REQUIRE(frame.size() == 12 + sizeof(ProjectSnapshotV3));

    SECTION("into a separate buffer")
    {
        ProjectSnapshot loaded;
        std::memset(&loaded, 0x5A, sizeof loaded);
        REQUIRE(decode(frame, loaded));
        CHECK(sameTuning(loaded.tuning, defaultTuningRecord())); // not the 96/7 beyond the file
        CHECK(loaded.settings.tempoBpm == 118.0f);
        CHECK(loaded.settings.currentScale == 3);
        CHECK(std::memcmp(&loaded.effects, &old.effects, sizeof(EffectsSnapshot)) == 0);
        CHECK(validateProjectSnapshot(loaded));
    }
    SECTION("in place, the way the flash loader decodes into its static buffer")
    {
        ProjectSnapshot buffer;
        std::memset(&buffer, 0xC3, sizeof buffer);
        std::memcpy(&buffer, frame.data() + 12, sizeof(ProjectSnapshotV3));
        REQUIRE(decodeSnapshotFrame(frame.data(), reinterpret_cast<const uint8_t *>(&buffer), sizeof buffer, buffer));
        CHECK(sameTuning(buffer.tuning, defaultTuningRecord()));
    }
    SECTION("a v3 frame is not a v4 frame")
    {
        CHECK(readFrameHeader(frame.data(), frame.data() + 12, frame.size() - 12, sizeof(ProjectSnapshotV3)) ==
              FrameStatus::BadVersion);
        CHECK(readFrameHeader(frame.data(), frame.data() + 12, frame.size() - 12, sizeof(ProjectSnapshot),
                              SNAPSHOT_FORMAT_VERSION_V3) == FrameStatus::BadSize);
    }
}

TEST_CASE("older files upgrade to the default tuning", "[persistence][tuning]")
{
    for (const auto &[version, bytes] : {std::pair<uint16_t, size_t>{SNAPSHOT_FORMAT_VERSION_V1, sizeof(ProjectSnapshotV1)},
                                         std::pair<uint16_t, size_t>{SNAPSHOT_FORMAT_VERSION_V2, sizeof(ProjectSnapshotV2)}})
    {
        CAPTURE(version);
        const ProjectSnapshot old = validSnapshot();
        const auto frame = makeFrame(old, version, bytes);
        ProjectSnapshot loaded;
        std::memset(&loaded, 0x5A, sizeof loaded);
        REQUIRE(decode(frame, loaded));
        CHECK(sameTuning(loaded.tuning, defaultTuningRecord()));
    }
}

TEST_CASE("a format-4 file round-trips the tuning, tonic, reference and favourites", "[persistence][tuning]")
{
    tuning::Selection selection;
    tuning::Bank bank;
    tuning::defaultBank(bank);
    REQUIRE(tuning::applyTuning(selection, bank, 96)); // 22 Shruti; the old tuning becomes the A/B partner
    selection.tonic = 2;
    selection.a4Hz = 432.0f;
    REQUIRE(tuning::storeFavorite(bank, 3, 67));
    REQUIRE(tuning::storeFavorite(bank, 2, 131));
    REQUIRE(tuning::clearFavorite(bank, 1));

    ProjectSnapshot snap = validSnapshot();
    captureTuning(selection, bank, snap.tuning);
    const auto frame = makeFrame(snap, SNAPSHOT_FORMAT_VERSION, sizeof(ProjectSnapshot));
    REQUIRE(frameVersion(frame.data()) == SNAPSHOT_FORMAT_VERSION);
    ProjectSnapshot loaded{};
    REQUIRE(decode(frame, loaded));
    CHECK(sameTuning(loaded.tuning, snap.tuning));

    tuning::Selection restored;
    tuning::Bank restoredBank;
    tuning::defaultBank(restoredBank);
    REQUIRE(restoreTuning(loaded.tuning, restored, restoredBank));
    CHECK(restored == selection);
    CHECK(restoredBank.previousId == bank.previousId);
    for (uint8_t slot = 0; slot < tuning::kFavoriteSlots; ++slot)
        CHECK(restoredBank.favorites[slot] == bank.favorites[slot]);
    CHECK(tuning::bankValid(restoredBank));
}

TEST_CASE("every library tuning, tonic and half-hertz reference survives a save", "[persistence][tuning]")
{
    for (size_t i = 0; i < tuning::libraryCount(); ++i)
    {
        const uint8_t id = tuning::libraryAt(i).id;
        CAPTURE(int(id));
        for (uint8_t tonic = 0; tonic < 12; ++tonic)
        {
            tuning::Selection selection;
            selection.tuningId = id;
            selection.tonic = tonic;
            selection.a4Hz = 415.0f + 0.5f * static_cast<float>((id + tonic) % 103); // 415..466 in halves
            tuning::Bank bank;
            tuning::defaultBank(bank);
            bank.previousId = id;

            TuningSnapshot record;
            captureTuning(selection, bank, record);
            REQUIRE(validateTuning(record));
            tuning::Selection restored;
            tuning::Bank restoredBank;
            tuning::defaultBank(restoredBank);
            REQUIRE(restoreTuning(record, restored, restoredBank));
            REQUIRE(restored == selection);
            REQUIRE(restoredBank.previousId == id);
        }
    }
}

TEST_CASE("an unknown tuning, tonic, reference or favourite rejects the file", "[persistence][tuning]")
{
    const auto rejected = [](auto mutate)
    {
        ProjectSnapshot snap = validSnapshot();
        mutate(snap.tuning);
        CHECK_FALSE(validateTuning(snap.tuning));
        CHECK_FALSE(validateProjectSnapshot(snap));
        const auto frame = makeFrame(snap, SNAPSHOT_FORMAT_VERSION, sizeof(ProjectSnapshot));
        ProjectSnapshot loaded{};
        CHECK_FALSE(decode(frame, loaded)); // the CRC is fine; the record is not
    };
    rejected([](TuningSnapshot &t) { t.tuningId = 7 * 13; });           // a hole in the id space
    rejected([](TuningSnapshot &t) { t.tuningId = tuning::kEmptySlot; }); // "empty" is not a tuning
    rejected([](TuningSnapshot &t) { t.previousId = 250; });
    rejected([](TuningSnapshot &t) { t.tonic = 12; });
    rejected([](TuningSnapshot &t) { t.tonic = 255; });
    rejected([](TuningSnapshot &t) { t.a4Tenths = 0; });
    rejected([](TuningSnapshot &t) { t.a4Tenths = 4149; });
    rejected([](TuningSnapshot &t) { t.a4Tenths = 4661; });
    rejected([](TuningSnapshot &t) { t.a4Tenths = 65535; });
    rejected([](TuningSnapshot &t) { t.favorites[3] = 200; });

    // The exact limits are valid, and reserved bytes are ignored on load.
    for (const uint16_t tenths : {uint16_t{4150}, uint16_t{4660}})
    {
        TuningSnapshot record = defaultTuningRecord();
        record.a4Tenths = tenths;
        CHECK(validateTuning(record));
    }
    TuningSnapshot record = defaultTuningRecord();
    record.reserved[1] = 0x7F;
    CHECK(validateTuning(record));
}

TEST_CASE("restoring a bad tuning record changes nothing", "[persistence][tuning]")
{
    TuningSnapshot bad = defaultTuningRecord();
    bad.tonic = 99;

    tuning::Selection selection;
    selection.tuningId = 34;
    selection.tonic = 5;
    selection.a4Hz = 442.0f;
    tuning::Bank bank;
    tuning::defaultBank(bank);
    bank.previousId = 1;
    const tuning::Selection selectionBefore = selection;
    const tuning::Bank bankBefore = bank;

    CHECK_FALSE(restoreTuning(bad, selection, bank));
    CHECK(selection == selectionBefore);
    CHECK(std::memcmp(&bank, &bankBefore, sizeof bank) == 0);
}

TEST_CASE("restoring a tuning forgets the scales remembered for the song that was playing", "[persistence][tuning]")
{
    tuning::Selection selection;
    tuning::Bank bank;
    tuning::defaultBank(bank);
    for (uint8_t i = 0; i < tuning::kMaxLibrary; ++i)
        bank.lastScale[i] = 3; // working memory from the song being left
    REQUIRE(restoreTuning(defaultTuningRecord(), selection, bank));
    for (uint8_t i = 0; i < tuning::kMaxLibrary; ++i)
        CHECK(bank.lastScale[i] == tuning::kNoScale);
}

TEST_CASE("the scales a tuning remembers are not saved in the file", "[persistence][tuning]")
{
    tuning::Selection selection;
    tuning::Bank bank;
    tuning::defaultBank(bank);
    TuningSnapshot plain;
    captureTuning(selection, bank, plain);
    for (uint8_t i = 0; i < tuning::kMaxLibrary; ++i)
        bank.lastScale[i] = 7;
    TuningSnapshot withMemory;
    captureTuning(selection, bank, withMemory);
    CHECK(std::memcmp(&plain, &withMemory, sizeof plain) == 0); // working memory never reaches flash
}

TEST_CASE("capture writes a valid record whatever the live state holds", "[persistence][tuning]")
{
    tuning::Selection selection;
    selection.a4Hz = 440.0f;
    tuning::Bank bank;
    tuning::defaultBank(bank);

    TuningSnapshot record;
    std::memset(&record, 0xFF, sizeof record); // stale bytes must not leak into the file
    captureTuning(selection, bank, record);
    CHECK(validateTuning(record));
    for (const uint8_t byte : record.reserved)
        CHECK(byte == 0);
}
