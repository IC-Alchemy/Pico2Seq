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
// Append new IDs; editor cursors refer to these stable IDs, never visible rows.
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
void adjust(Id id, VoiceConfig &config, float normalizedDelta) noexcept;
void format(Id id, const VoiceConfig &config, char *text,
            size_t capacity) noexcept;
Id nextParameter(Id current, int direction, const VoiceConfig &config,
                 bool changeGroup) noexcept;
ParamId sequenceLane(Id id, const VoiceConfig &config) noexcept;

// Note stores melody scale steps, with baseNote as an additive transpose.
// Other continuous lanes use their midpoint as zero modulation around a base.
float composeLane(ParamId id, float stored, const void *config) noexcept;
int8_t mapOctave(float normalized) noexcept;
void seedModifiers(Sequencer &sequencer);
void enablePatch(VoiceConfig &config) noexcept;
} // namespace VoiceEdit
