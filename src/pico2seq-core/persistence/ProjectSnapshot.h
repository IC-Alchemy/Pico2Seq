#ifndef PICO2SEQ_PROJECT_SNAPSHOT_H
#define PICO2SEQ_PROJECT_SNAPSHOT_H

#include "../sequencer/SequencerDefs.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace persistence
{

// One parameter track. Full 64-step storage so patterns keep their tail when
// lengths shrink and grow again; defaultValue is NOT persisted (it only ever
// comes from the compile-time CORE_PARAMETERS table).
struct TrackSnapshot
{
    float values[SequencerConstants::MAX_STEPS_COUNT]; // 64 floats = 256 B
    uint8_t stepCount;
    uint8_t reserved[3]; // deterministic size/alignment, must stay zero
};

// Format 1 stored the nine lanes Note..Slide per voice; lanes added later
// travel in their own block so a format-1 payload stays a valid prefix.
constexpr uint8_t kPatternTrackCount = static_cast<uint8_t>(ParamId::Slide) + 1;
constexpr uint8_t kEnvelopeTrackCount = PARAM_ID_COUNT - kPatternTrackCount;
static_assert(kEnvelopeTrackCount == 2, "Sustain and Release follow Slide");

struct PatternSnapshot
{
    TrackSnapshot tracks[kPatternTrackCount]; // 9 tracks = 2,340 B
};

// Format 2: ParamId::Sustain and ParamId::Release, in ParamId order.
struct EnvelopeTracksSnapshot
{
    TrackSnapshot tracks[kEnvelopeTrackCount]; // 520 B
};

// How Velocity/Filter/Attack/Decay steps are stored.
enum : uint32_t
{
    LANE_MODEL_OFFSETS = 0,  // format 1: offsets around the patch, 0.5 = patch
    LANE_MODEL_ABSOLUTE = 1, // format 2: absolute or LANE_FOLLOWS_PATCH
};

// Mirrors the per-voice patch bases inside VoiceConfig; the former separate
// encoder-base layer was removed when patch bases moved into VoiceConfig, so
// PatchSnapshot already captures everything it held.

// VoiceConfig value fields, pointers excluded (parameters/recipe are
// flash-resident descriptors re-derived at load, see PatchCodec).
struct PatchSnapshot
{
    // 53 floats / int32s
    float baseNote, baseVelocity, baseOctave, baseGateLength, slideSeconds;
    float oscAmplitudes[3];
    float oscDetuning[3];
    float oscPulseWidth[3];
    int32_t harmony[3];
    float macro1, macro2, macro3;
    float fmModFeedback, phaseTriangleFold, spectralSubRatio, spectralSubShape, prismDriftChaos;
    float noiseSourceLevel, noiseChaosRate, filterEnvelopeOctaves, filterEnvelopeRest;
    float wgT60, wgBrightness, wgPickPosition, wgPickHardness, wgStiffness, wgDetune;
    float hypersawDetune, hypersawMix;
    float noiseDiffuseSize, noiseDiffuseMix, noiseSwarmColor, noiseSwarmRegen, noiseChaosLevel;
    float filterRes, filterDrive, filterPassbandGain, filterCutoffBase;
    float highPassFreq, highPassRes;
    float overdriveGain, overdriveDrive;
    float defaultAttack, defaultDecay, defaultSustain, defaultRelease;
    float outputLevel;
    // small fields last -> no interior padding. 55 4-byte words (220 B) + 10 u8
    // + explicit 2-byte tail = 232 B, the natural 4-byte aligned size — no
    // compiler-dependent implicit padding anywhere in the struct.
    uint8_t oscillatorCount, engine, paramSet, filterType, filterMode, presetIndex;
    uint8_t oscWaveforms[3];
    uint8_t flags; // bit0 usePatchBases, bit1 baseGate, bit2 baseSlide, bit3 recipeRetrigger,
                   // bit4 hasOverdrive, bit5 hasEnvelope, bit6 hasFilter, bit7 enabled
    uint8_t reserved[2];
};

struct SettingsSnapshot
{
    float tempoBpm;
    float masterVolume;
    int32_t themeIndex;
    uint8_t currentScale;
    uint8_t shuffleIndex;
    uint8_t selectedVoice;
    uint8_t presetIndices[4];
    uint8_t editorCursor[4]; // VoiceEdit::Id per voice (stable IDs)
    uint8_t changedFlags;    // bit N = voiceEditor.changed[N]; bit4 = slideMode
};

// Format 1 payload. Kept only to size and load old files.
struct ProjectSnapshotV1
{
    PatternSnapshot patterns[4]; // 9,360 B
    PatchSnapshot patches[4];    // 928 B
    SettingsSnapshot settings;   // 24 B
};

// Format 2: format 1 unchanged, then the envelope lanes and the lane model.
struct ProjectSnapshot
{
    PatternSnapshot patterns[4];         // 9,360 B
    PatchSnapshot patches[4];            // 928 B
    SettingsSnapshot settings;           // 24 B
    EnvelopeTracksSnapshot envelopes[4]; // 2,080 B
    uint32_t laneModel;                  // LANE_MODEL_*
    uint32_t reserved;                   // must stay zero
};
static_assert(sizeof(TrackSnapshot) == 260, "locked layout");
static_assert(sizeof(PatternSnapshot) == 2340, "locked layout");
static_assert(sizeof(PatchSnapshot) == 232, "locked layout"); // 220 B words + 10 u8 + 2 tail
static_assert(sizeof(SettingsSnapshot) == 24, "locked layout");
static_assert(sizeof(ProjectSnapshotV1) == 10312, "locked layout");
static_assert(sizeof(EnvelopeTracksSnapshot) == 520, "locked layout");
static_assert(sizeof(ProjectSnapshot) == 12400, "locked layout");
static_assert(offsetof(ProjectSnapshot, envelopes) == sizeof(ProjectSnapshotV1),
              "a format-1 payload must load as the prefix of format 2");

// Complete a snapshot whose first sizeof(ProjectSnapshotV1) bytes hold a
// format-1 payload: envelope lanes follow the patch on 16 steps and the lane
// model says offsets (Session converts them once the voices exist).
void upgradeFromV1(ProjectSnapshot &s) noexcept;

// Range checks only — structural validity, not musical sense. Bounds mirror
// the UI: tempo 45..200 BPM (UIEventHandler clamps at 45, fader tops at 200),
// 13 scales, NUM_SHUFFLE_TEMPLATES=16, 10 LED themes.
bool validateProjectSnapshot(const ProjectSnapshot &s) noexcept;

} // namespace persistence

#endif
