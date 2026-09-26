// VoiceEditParameters.h — patch editor model: stable Id per parameter (append
// only; cursors persist by Id), groups for the UI pages, units/ranges for
// display and clamping. available() hides engine-irrelevant rows (e.g. string
// model on oscillator voices). Pure control-thread logic; no DSP here.
#pragma once
#include "VoiceParameters.h"
#include <cstddef>

namespace VoiceEdit {
enum class Group : uint8_t {
  Sequenced,
  Source,
  Osc1,
  Osc2,
  Osc3,
  Envelope,
  Filter,
  HighPass,
  Drive,
  Engine,
  Output,
  Count
};
enum class Unit : uint8_t {
  Number,
  Percent,
  Seconds,
  Semitones,
  Cents,
  Hertz,
  Toggle,
  Choice
};
// Append new IDs; editor cursors persist by these stable IDs, not visible rows.
enum class Id : uint8_t {
  Note,
  Velocity,
  Cutoff,
  Attack,
  Decay,
  Octave,
  GateLength,
  Gate,
  Slide,
  Engine,
  Recipe,
  OscCount,
  Wave1,
  Level1,
  Detune1,
  Pulse1,
  Harmony1,
  Wave2,
  Level2,
  Detune2,
  Pulse2,
  Harmony2,
  Wave3,
  Level3,
  Detune3,
  Pulse3,
  Harmony3,
  EnvelopeOn,
  EnvAttack,
  EnvDecay,
  Sustain,
  Release,
  FilterOn,
  FilterType,
  FilterMode,
  StaticCutoff,
  Resonance,
  FilterDrive,
  Passband,
  HighPassFreq,
  HighPassRes,
  DriveOn,
  Drive,
  DriveGain,
  T60,
  Brightness,
  PickPosition,
  PickHardness,
  Stiffness,
  StringDetune,
  SawDetune,
  SawMix,
  DiffuseSize,
  DiffuseMix,
  SwarmColor,
  SwarmRegen,
  ChaosLevel,
  Macro1,
  Macro2,
  Macro3,
  SlideTime,
  Enabled,
  Output,
  FmModFeedback,
  PhaseFold,
  SubRatio,
  SubShape,
  DriftChaos,
  RecipeRetrigger,
  NoiseLevel,
  ChaosRate,
  FilterEnvAmount,
  FilterEnvFloor,
  Count
};
struct Parameter {
  Id id;
  const char *name;
  Group group;
  Unit unit;
  float minimum, maximum;
  bool logarithmic;
  float (*get)(const VoiceConfig &);
  void (*set)(VoiceConfig &, float);
};
const Parameter &parameter(Id id) noexcept;
const char *groupName(Group group) noexcept;
const char *name(Id id, const VoiceConfig &config) noexcept;
bool available(Id id, const VoiceConfig &config) noexcept;
float value(Id id, const VoiceConfig &config) noexcept;
void setValue(Id id, VoiceConfig &config, float value) noexcept;
// Stepped values move one step (an octave for Octave) per adjust() call,
// whatever the delta's size; everything else moves by the normalized delta.
bool stepped(Id id) noexcept;
void adjust(Id id, VoiceConfig &config, float normalizedDelta) noexcept;
void format(Id id, const VoiceConfig &config, char *text,
            size_t capacity) noexcept;
Id nextParameter(Id current, int direction, const VoiceConfig &config,
                 bool changeGroup) noexcept;
ParamId sequenceLane(Id id, const VoiceConfig &config) noexcept;
// Inverse mapping for the panel's encoder target. Sustain/Release were added
// after the original lane IDs and do not share the editor enum's indices.
Id baseParameterForLane(ParamId lane, const VoiceConfig &config) noexcept;

// Attack lanes stop at 2 s so encoder travel stays on playable pluck-to-swell
// attacks; decay keeps the full 1 ms..10 s envelope range.
inline constexpr float kAttackMaxSeconds = 2.0f;
inline constexpr float kReleaseMinSeconds = 0.01f;
inline constexpr float kReleaseMaxSeconds = 8.0f;

// A lane's patch value, normalized the way the lane stores it.
float laneBase(ParamId id, const VoiceConfig &config) noexcept;
float timeNormalize(float seconds) noexcept;
float attackNormalize(float seconds) noexcept;
// Playback transform. Absolute lanes (isPatchDefaultLane) play their stored
// value, or laneBase() for LANE_FOLLOWS_PATCH; Note and Octave transpose the
// patch; GateLength offsets it around 0.5; Gate/Slide combine with the patch.
float composeLane(ParamId id, float stored, const void *config) noexcept;
int8_t mapOctave(float normalized) noexcept;
// Neutral pattern: absolute lanes follow the patch, offsets rest at zero.
void seedModifiers(Sequencer &sequencer);
// Sessions saved before absolute lanes stored Velocity/Filter/Attack/Decay as
// offsets around the patch (0.5 = patch). Converts one saved lane in place:
// neutral steps follow the patch, every other step becomes the absolute
// value it played under config. Works on saved data, not a live Sequencer,
// whose raw writes wrap at the active length.
void convertOffsetValues(ParamId lane, float *values, size_t count, const VoiceConfig &config);
// Display name of a sequencer lane under this voice's layout.
const char *laneName(ParamId lane, const VoiceConfig &config) noexcept;
void enablePatch(VoiceConfig &config) noexcept;
} // namespace VoiceEdit
