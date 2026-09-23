// ProjectSnapshot: the whole song (4 patterns + patches + settings) as plain data.
// Layout is flash-stable and versioned — old files must keep loading.
// Portable C++ — no Arduino/hardware includes here.
#ifndef PICO2SEQ_PROJECT_SNAPSHOT_H
#define PICO2SEQ_PROJECT_SNAPSHOT_H

#include "../sequencer/SequencerDefs.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace persistence
{

// One lane's 64 stored steps plus its loop length. Full storage keeps the tail
// when a loop shortens and grows back. Defaults live in CORE_PARAMETERS, so
// they are intentionally NOT persisted.
struct TrackSnapshot
{
    float values[SequencerConstants::MAX_STEPS_COUNT]; // 64 floats = 256 B
    uint8_t stepCount; // Active loop length, 1..64
    uint8_t reserved[3]; // Zero; keeps size/alignment deterministic
};

// Format 1 held Note..Slide per voice; Sustain/Release appended later so a
// format-1 payload stays a valid prefix of format 2.
constexpr uint8_t kPatternTrackCount = static_cast<uint8_t>(ParamId::Slide) + 1;
constexpr uint8_t kEnvelopeTrackCount = PARAM_ID_COUNT - kPatternTrackCount;
static_assert(kEnvelopeTrackCount == 2, "Sustain and Release follow Slide");

struct PatternSnapshot
{
    TrackSnapshot tracks[kPatternTrackCount]; // 9 tracks = 2,340 B
};

// Format 2 tail: Sustain, Release in ParamId order.
struct EnvelopeTracksSnapshot
{
    TrackSnapshot tracks[kEnvelopeTrackCount]; // 520 B
};

// How Velocity/Filter/ADSR steps are stored: offsets around the patch (v1)
// vs. absolute values with follow-patch (v2).
enum : uint32_t
{
    LANE_MODEL_OFFSETS = 0, // v1: 0.5 means "patch value"
    LANE_MODEL_ABSOLUTE = 1, // v2: absolute value or LANE_FOLLOWS_PATCH
};

// Patch bases moved into VoiceConfig, absorbing the old encoder-base layer —
// PatchSnapshot already captures everything it held.

// Value fields of one voice's patch; flash-resident descriptors (waveforms,
// recipe) are re-derived at load — see PatchCodec — so pointers stay out.
struct PatchSnapshot
{
    // 55 floats / int32s
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
    // 10 packed bytes + 2 reserved: keep zero so snapshots compare/hash stable.
    uint8_t oscillatorCount, engine, paramSet, filterType, filterMode, presetIndex;
    uint8_t oscWaveforms[3];
    uint8_t flags; // bit0 usePatchBases, bit1 baseGate, bit2 baseSlide, bit3 recipeRetrigger,
                   // bit4 hasOverdrive, bit5 hasEnvelope, bit6 hasFilter, bit7 enabled
    uint8_t reserved[2];
};

// Global song state: tempo, mix, scale/groove, per-voice preset and UI cursor.
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

// Format 1 payload, kept only to size and load old files — do not extend.
struct ProjectSnapshotV1
{
    PatternSnapshot patterns[4]; // 9,360 B
    PatchSnapshot patches[4];    // 928 B
    SettingsSnapshot settings;   // 24 B
};

// Format 3 tail: per-voice ENGINE_SITAR patch fields. v1/v2 files carry no
// sitar data — the loader leaves this tail zeroed, and those files' patches
// predate the sitar engine (engine byte ≤ ENGINE_RECIPE), so the zeros are
// never interpreted as sitar tuning.
struct SitarPatchSnapshot
{
    float decay, brightness, pickPosition, pickHardness;
    float jawari, jawariThreshold;
    float tarafAmount, tarafDecay, bodyAmount, bodyFrequency; // 10 floats = 40 B
};

// Format 3: format 2 plus the per-voice sitar tails.
struct ProjectSnapshot
{
    PatternSnapshot patterns[4];         // 9,360 B
    PatchSnapshot patches[4];            // 928 B
    SettingsSnapshot settings;           // 24 B
    EnvelopeTracksSnapshot envelopes[4]; // 2,080 B
    uint32_t laneModel;                  // LANE_MODEL_*
    uint32_t reserved;                   // must stay zero
    SitarPatchSnapshot sitar[4];         // 160 B — format 3 tail
};
// Locked flash layout: the static_asserts below are the contract. A format-1
// payload must load as the prefix of format 2; a format-2 payload as the
// prefix of format 3.
static_assert(sizeof(TrackSnapshot) == 260, "locked layout");
static_assert(sizeof(PatternSnapshot) == 2340, "locked layout");
static_assert(sizeof(PatchSnapshot) == 232, "locked layout"); // 220 B words + 10 u8 + 2 tail
static_assert(sizeof(SettingsSnapshot) == 24, "locked layout");
static_assert(sizeof(ProjectSnapshotV1) == 10312, "locked layout");
static_assert(sizeof(EnvelopeTracksSnapshot) == 520, "locked layout");
static_assert(sizeof(SitarPatchSnapshot) == 40, "locked layout");
static_assert(sizeof(ProjectSnapshot) == 12560, "locked layout");
static_assert(offsetof(ProjectSnapshot, envelopes) == sizeof(ProjectSnapshotV1),
              "a format-1 payload must load as the prefix of format 2");
static_assert(offsetof(ProjectSnapshot, sitar) == 12400,
              "a format-2 payload must load as the prefix of format 3");

// Fill a v1-loaded snapshot's missing tail: new lanes follow the patch on 16
// steps, lane model reads as offsets (Session converts once voices exist).
void upgradeFromV1(ProjectSnapshot &s) noexcept;

// Fill a v2-loaded snapshot's missing tail: the sitar tails read as zero
// (v2 patches predate ENGINE_SITAR, so the values are never used as tuning).
void upgradeFromV2(ProjectSnapshot &s) noexcept;

// Structural sanity only (ranges, not musical taste); mirrors UI limits:
// tempo 45..200 BPM, 13 scales, 16 grooves, 10 LED themes.
bool validateProjectSnapshot(const ProjectSnapshot &s) noexcept;

} // namespace persistence

#endif
