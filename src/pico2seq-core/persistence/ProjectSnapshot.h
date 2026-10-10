// ProjectSnapshot: the whole song (4 patterns + patches + settings) as plain data.
// Layout is flash-stable and versioned — old files must keep loading.
// Portable C++ — no Arduino/hardware includes here.
#ifndef PICO2SEQ_PROJECT_SNAPSHOT_H
#define PICO2SEQ_PROJECT_SNAPSHOT_H

#include "../sequencer/SequencerDefs.h"
#include "../tuning/Tuning.h"
#include "SnapshotFormat.h"
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
    // [0] = loop start (first looped step; 0 in files written before it
    // existed, which is also the old behaviour). [1..2] zero.
    uint8_t reserved[3];
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

// Format 3 tail: master-bus effect settings (the reverb today), as the values the
// performer sets. Settings only, never tank contents, and never the freeze switch:
// a restored project always starts unfrozen. Fixed size, fully written on save.
struct EffectsSnapshot
{
    float reverbMix;
    float reverbDecaySeconds;
    float reverbDampingHz;
    float reverbLowCutHz;
    float reverbDiffusion;
    float reverbModDepth;
    float reverbModRateHz;
    float reverbWidth;
    uint32_t reserved[4]; // written as zero; ignored on load
};

// Format 2 payload, kept only to size and load old files — do not extend.
struct ProjectSnapshotV2
{
    PatternSnapshot patterns[4];         // 9,360 B
    PatchSnapshot patches[4];            // 928 B
    SettingsSnapshot settings;           // 24 B
    EnvelopeTracksSnapshot envelopes[4]; // 2,080 B
    uint32_t laneModel;                  // LANE_MODEL_*
    uint32_t reserved;                   // must stay zero
};

// Format 3: format 2 plus the effect-settings record, kept only to size and load
// old files - do not extend.
struct ProjectSnapshotV3
{
    PatternSnapshot patterns[4];         // 9,360 B
    PatchSnapshot patches[4];            // 928 B
    SettingsSnapshot settings;           // 24 B
    EnvelopeTracksSnapshot envelopes[4]; // 2,080 B
    uint32_t laneModel;                  // LANE_MODEL_*
    uint32_t reserved;                   // must stay zero
    EffectsSnapshot effects;             // 48 B
};

// Format 4 tail: the one global tuning (tuning/Tuning.h) - which tuning, the tonic
// (Sa) and the A4 reference - plus the four hot favourites the performer organised. Ids are
// the tuning library's permanent ids, never indices, so a song keeps its tuning when
// the library grows. A4 is stored in tenths of a hertz so no float sits in the file.
struct TuningSnapshot
{
    uint8_t tuningId;
    uint8_t tonic;       // 0..11 semitones above C
    uint16_t a4Tenths;   // 4150..4660
    uint8_t previousId;  // the A/B partner
    uint8_t reserved[3]; // written as zero; ignored on load
    uint8_t favorites[tuning::kFavoriteSlots]; // tuning id, or 0xFF for an empty slot
};

// Format 4: format 3 plus the tuning record. Fields are only ever appended, so an
// older payload is always a prefix of the newer one.
struct ProjectSnapshot
{
    PatternSnapshot patterns[4];         // 9,360 B
    PatchSnapshot patches[4];            // 928 B
    SettingsSnapshot settings;           // 24 B
    EnvelopeTracksSnapshot envelopes[4]; // 2,080 B
    uint32_t laneModel;                  // LANE_MODEL_*
    uint32_t reserved;                   // must stay zero
    EffectsSnapshot effects;             // 48 B, format 3
    TuningSnapshot tuning;               // 12 B, format 4
};
// Locked flash layout: the static_asserts below are the contract. A format-1
// payload must load as the prefix of format 2, format 2 as the prefix of format 3
// and format 3 as the prefix of format 4.
static_assert(sizeof(TrackSnapshot) == 260, "locked layout");
static_assert(sizeof(PatternSnapshot) == 2340, "locked layout");
static_assert(sizeof(PatchSnapshot) == 232, "locked layout"); // 220 B words + 10 u8 + 2 tail
static_assert(sizeof(SettingsSnapshot) == 24, "locked layout");
static_assert(sizeof(ProjectSnapshotV1) == 10312, "locked layout");
static_assert(sizeof(EnvelopeTracksSnapshot) == 520, "locked layout");
static_assert(sizeof(ProjectSnapshotV2) == 12400, "locked layout");
static_assert(sizeof(EffectsSnapshot) == 48, "locked layout");
static_assert(sizeof(ProjectSnapshotV3) == 12448, "locked layout");
static_assert(sizeof(TuningSnapshot) == 12, "locked layout");
static_assert(sizeof(ProjectSnapshot) == 12460, "locked layout");
static_assert(offsetof(ProjectSnapshotV2, envelopes) == sizeof(ProjectSnapshotV1),
              "a format-1 payload must load as the prefix of format 2");
static_assert(offsetof(ProjectSnapshotV3, laneModel) == offsetof(ProjectSnapshotV2, laneModel) &&
                  offsetof(ProjectSnapshotV3, reserved) == offsetof(ProjectSnapshotV2, reserved),
              "format 3 keeps every format-2 field where it was");
static_assert(offsetof(ProjectSnapshotV3, effects) == sizeof(ProjectSnapshotV2),
              "a format-2 payload must load as the prefix of format 3");
static_assert(offsetof(ProjectSnapshot, laneModel) == offsetof(ProjectSnapshotV3, laneModel) &&
                  offsetof(ProjectSnapshot, reserved) == offsetof(ProjectSnapshotV3, reserved) &&
                  offsetof(ProjectSnapshot, effects) == offsetof(ProjectSnapshotV3, effects),
              "format 4 keeps every format-3 field where it was");
static_assert(offsetof(ProjectSnapshot, tuning) == sizeof(ProjectSnapshotV3),
              "a format-3 payload must load as the prefix of format 4");

// Effect-settings limits and defaults as stored on flash. src/voice/ReverbSettings.h
// holds the same numbers for the audio side (this folder must not depend on it);
// tests/unit/test_persistence.cpp asserts the two stay equal.
namespace EffectsLimits
{
inline constexpr float kReverbMixMin = 0.0f, kReverbMixMax = 1.0f, kReverbMixDefault = 0.0f;
inline constexpr float kReverbDecayMin = 0.1f, kReverbDecayMax = 1000.0f, kReverbDecayDefault = 20.0f;
inline constexpr float kReverbDampingMin = 100.0f, kReverbDampingMax = 10800.0f, kReverbDampingDefault = 3000.0f;
inline constexpr float kReverbLowCutMin = 10.0f, kReverbLowCutMax = 1000.0f, kReverbLowCutDefault = 40.0f;
inline constexpr float kReverbDiffusionMin = 0.0f, kReverbDiffusionMax = 1.0f, kReverbDiffusionDefault = 0.8f;
inline constexpr float kReverbModDepthMin = 0.0f, kReverbModDepthMax = 1.0f, kReverbModDepthDefault = 0.5f;
inline constexpr float kReverbModRateMin = 0.01f, kReverbModRateMax = 5.0f, kReverbModRateDefault = 0.5f;
inline constexpr float kReverbWidthMin = 0.0f, kReverbWidthMax = 2.0f, kReverbWidthDefault = 1.0f;
} // namespace EffectsLimits

// Fill an effect record with the defaults an upgraded (v1/v2) project starts from:
// reverb mix 0, so the project sounds exactly as it did before.
void applyEffectsDefaults(EffectsSnapshot &effects) noexcept;

// Every field finite (bit-pattern test: the firmware is built with -ffast-math)
// and inside its limits. Nothing is clamped here: a bad record rejects the file.
bool validateEffects(const EffectsSnapshot &effects) noexcept;

// The tuning a project starts from when the file predates it: 12-EDO, tonic C, A4 440
// and the default favourites, which sounds exactly as the song always did.
void applyTuningDefaults(TuningSnapshot &tuning) noexcept;

// Known tuning and favourite ids, tonic 0..11, A4 inside the reference range. Nothing
// is clamped: a bad record rejects the file.
bool validateTuning(const TuningSnapshot &tuning) noexcept;

// The live selection and favourites as a record, and back. restoreTuning() changes
// nothing and returns false for a record that fails validateTuning().
void captureTuning(const tuning::Selection &selection, const tuning::Bank &bank,
                   TuningSnapshot &out) noexcept;
bool restoreTuning(const TuningSnapshot &in, tuning::Selection &selection,
                   tuning::Bank &bank) noexcept;

// Fill a v1-loaded snapshot's missing tail: new lanes follow the patch on 16
// steps, lane model reads as offsets (Session converts once voices exist), and
// the effect and tuning records take their defaults.
void upgradeFromV1(ProjectSnapshot &s) noexcept;

// Fill a v2-loaded snapshot's missing tail: the effect and tuning records take their
// defaults.
void upgradeFromV2(ProjectSnapshot &s) noexcept;

// Fill a v3-loaded snapshot's missing tail: the tuning record takes its defaults.
void upgradeFromV3(ProjectSnapshot &s) noexcept;

// Structural sanity only (ranges, not musical taste); mirrors UI limits:
// tempo 45..200 BPM, SCALES_COUNT scales, 16 grooves, 10 LED themes, effect and
// tuning fields in range.
bool validateProjectSnapshot(const ProjectSnapshot &s) noexcept;

// Payload bytes a frame of this format version carries; 0 for a version this
// build does not know (older files load, newer ones are rejected).
size_t payloadSizeForVersion(uint16_t version) noexcept;

// Verify a frame (magic, version, size, CRC), bring its payload up to the newest
// layout in `out` (older versions are upgraded, missing fields defaulted) and
// validate the result. `payload` may be the very buffer `out` occupies, so a
// loader can read the file straight into it; the bytes past an older payload are
// then overwritten by the upgrade. `out` is unspecified when this returns false.
bool decodeSnapshotFrame(const uint8_t header[12], const uint8_t *payload, size_t payloadCapacity,
                         ProjectSnapshot &out) noexcept;

} // namespace persistence

#endif
