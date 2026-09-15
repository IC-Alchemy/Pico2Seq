#ifndef PICO2SEQ_PROJECT_SNAPSHOT_H
#define PICO2SEQ_PROJECT_SNAPSHOT_H

#include "../sequencer/SequencerDefs.h"
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

struct PatternSnapshot
{
    TrackSnapshot tracks[PARAM_ID_COUNT]; // 9 tracks = 2,340 B
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
    float noiseSourceLevel, noiseChaosRate, filterEnvelopeAmount, filterEnvelopeFloor;
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

struct ProjectSnapshotV1
{
    PatternSnapshot patterns[4]; // 9,360 B
    PatchSnapshot patches[4];    // 928 B
    SettingsSnapshot settings;   // 24 B
};
static_assert(sizeof(TrackSnapshot) == 260, "locked layout");
static_assert(sizeof(PatternSnapshot) == 2340, "locked layout");
static_assert(sizeof(PatchSnapshot) == 232, "locked layout"); // 220 B words + 10 u8 + 2 tail
static_assert(sizeof(SettingsSnapshot) == 24, "locked layout");
static_assert(sizeof(ProjectSnapshotV1) == 10312, "locked layout");

// Range checks only — structural validity, not musical sense. Bounds mirror
// the UI: tempo 45..200 BPM (UIEventHandler clamps at 45, fader tops at 200),
// 13 scales, NUM_SHUFFLE_TEMPLATES=16, 10 LED themes.
bool validateProjectSnapshot(const ProjectSnapshotV1 &s) noexcept;

} // namespace persistence

#endif
