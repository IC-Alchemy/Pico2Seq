#pragma once

#include "VoiceConfig.h"
#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include "../utils/DspMapping.h"
#include <array>
#include <limits>

class Sequencer;

enum class VoiceParameterUnit : uint8_t { Standard, Percent, Seconds, Semitones, Ratio, Hertz };

// The same binding drives DSP values, preset seeding, and OLED formatting.
// A null target keeps the shared pitch/gate/envelope behavior for that lane.
struct VoiceParameterBinding
{
  // Marks "no center". Finite on purpose: the firmware compiles with
  // -ffast-math, which lets the compiler assume a NaN marker never occurs.
  static constexpr float kUncentered = std::numeric_limits<float>::lowest();

  const char *name = nullptr; // null keeps the standard lane label
  float VoiceConfig::*target = nullptr;
  float minimum = 0.0f;
  float maximum = 1.0f;
  dspmap::Mapping curve = dspmap::Mapping::LINEAR;
  VoiceParameterUnit unit = VoiceParameterUnit::Standard;
  bool seed = false;
  float defaultNormalized = 0.5f; // used when seed=true and target=null
  // Value at lane 0.5 (the sweet spot). Each half of the lane then follows
  // the curve between that center and one range end.
  float center = kUncentered;

  constexpr bool isCentered() const noexcept { return center != kUncentered; }
  float map(float normalized) const noexcept;
  float normalize(float value) const noexcept;
};

struct VoiceParameterLayout
{
  // Eight controls plus Gate. Indexed by ParamId so every engine receives
  // the same sequencer contract, including Octave, GateLength, and Slide.
  std::array<VoiceParameterBinding, PARAM_ID_COUNT> slots{};
  bool envelopeFromTracks = true;
  bool velocityToAmplitude = true;
  // Main-filter cutoff in Hz for a target-less Filter lane, or for
  // filterCutoffBase when that lane is re-purposed. With a center,
  // filterCutoffBase 0.5 rests on it.
  float cutoffMinimum = 120.0f;
  float cutoffMaximum = 5000.0f;
  dspmap::Mapping cutoffCurve = dspmap::Mapping::EXP;
  float cutoffCenter = VoiceParameterBinding::kUncentered;

  constexpr bool cutoffCentered() const noexcept
  {
    return cutoffCenter != VoiceParameterBinding::kUncentered;
  }
};

namespace VoiceParameters {
inline constexpr float kWaveguideT60Min = 0.05f;
inline constexpr float kWaveguideT60Max = 10.0f;

// A lane's musical span and the resting value its midpoint lands on.
struct Span { float minimum, center, maximum; };
// Retunes a binding's range, center and curve, keeping its name, target and unit.
constexpr VoiceParameterBinding spanned(VoiceParameterBinding b, Span span, dspmap::Mapping curve)
{
  b.minimum = span.minimum;
  b.center = span.center;
  b.maximum = span.maximum;
  b.curve = curve;
  return b;
}

// Layouts selected by paramSet when a config owns no layout (for example
// after an engine change). Preset headers start their owned layouts here.
constexpr VoiceParameterBinding control(const char *name, float VoiceConfig::*target)
{
  return {name, target, 0.0f, 1.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true};
}
constexpr VoiceParameterLayout waveguideLayout()
{
  VoiceParameterLayout p{};
  p.envelopeFromTracks = false;
  // Velocity lives in the pluck excitation (Voice::processWaveguide_). Scaling
  // the raw output too would double-apply it and zipper the ringing string
  // every time a later step pushes a new velocity value.
  p.velocityToAmplitude = false;
  p.slots[static_cast<size_t>(ParamId::Filter)] = control("Bright", &VoiceConfig::wgBrightness);
  p.slots[static_cast<size_t>(ParamId::Attack)] = control("Pick", &VoiceConfig::wgPickHardness);
  p.slots[static_cast<size_t>(ParamId::Decay)] = {"T60", &VoiceConfig::wgT60, kWaveguideT60Min,
      kWaveguideT60Max, dspmap::Mapping::EXP, VoiceParameterUnit::Seconds, true};
  return p;
}
constexpr VoiceParameterLayout hypersawLayout()
{
  VoiceParameterLayout p{};
  p.envelopeFromTracks = false;
  p.cutoffMinimum = 150.0f;
  p.cutoffMaximum = 8000.0f;
  p.slots[static_cast<size_t>(ParamId::Attack)] = control("Detune", &VoiceConfig::hypersawDetune);
  p.slots[static_cast<size_t>(ParamId::Decay)] = control("Mix", &VoiceConfig::hypersawMix);
  return p;
}
constexpr VoiceParameterLayout noiseStormLayout()
{
  VoiceParameterLayout p = hypersawLayout();
  p.slots[static_cast<size_t>(ParamId::Filter)] = control("Color", &VoiceConfig::noiseSwarmColor);
  p.slots[static_cast<size_t>(ParamId::Attack)] = control("Regen", &VoiceConfig::noiseSwarmRegen);
  p.slots[static_cast<size_t>(ParamId::Decay)] = control("Chaos", &VoiceConfig::noiseChaosLevel);
  return p;
}
constexpr VoiceParameterLayout hardSyncLayout()
{
  VoiceParameterLayout p{};
  p.velocityToAmplitude = false;
  p.slots[static_cast<size_t>(ParamId::Note)].name = "Master";
  p.slots[static_cast<size_t>(ParamId::Velocity)] = {"Slave", nullptr, -24.0f, 24.0f,
      dspmap::Mapping::LINEAR, VoiceParameterUnit::Semitones, true, 0.5f};
  return p;
}

const VoiceParameterLayout &layout(const VoiceConfig &config) noexcept;
// Hard sync is a property of the oscillator bank: PARAMSET_HARDSYNC supplies
// the Master/Slave lanes and disables velocity-to-amplitude on top of any
// owned layout, so a waveform edit keeps the preset's cutoff lane.
const VoiceParameterBinding &binding(const VoiceConfig &config, ParamId id) noexcept;
bool velocityToAmplitude(const VoiceConfig &config) noexcept;
// Cutoff in Hz for a normalized Filter lane value under this layout.
float mapCutoff(const VoiceParameterLayout &layout, float normalized) noexcept;
void apply(VoiceConfig &config, const VoiceState &state) noexcept;
void seedTracks(Sequencer &sequencer, const VoiceConfig &config);
bool formatValue(const VoiceConfig &config, ParamId id, float normalized,
                 char *output, size_t capacity) noexcept;
} // namespace VoiceParameters
