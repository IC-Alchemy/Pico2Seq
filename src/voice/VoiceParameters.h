// VoiceParameters.h — how sequencer lanes (0..1) become sound: each binding maps
// one lane to a VoiceConfig field with range, curve, and display unit. A null
// target keeps shared pitch/gate/envelope behavior. Layouts are flash-resident
// descriptors owned by preset headers; Voice never allocates them.
#pragma once

#include "VoiceConfig.h"
#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include "../utils/DspMapping.h"
#include <array>
#include <limits>

class Sequencer;

enum class VoiceParameterUnit : uint8_t { Standard, Percent, Seconds, Semitones, Ratio, Hertz };

// One binding drives DSP value, preset seeding, and OLED readout together.
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
  // Lane midpoint's resting value; each lane half then follows the curve to
  // one range end. kUncentered (lowest float, not NaN: -ffast-math assumes NaN
  // never occurs) marks "no center".
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
  // A string has no envelope, so its Sustain/Release lanes (ENV faders 3-4)
  // shape the next pluck instead.
  p.slots[static_cast<size_t>(ParamId::Sustain)] = {"Position", &VoiceConfig::wgPickPosition, 0.02f,
      0.5f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true};
  p.slots[static_cast<size_t>(ParamId::Release)] = control("Stiffness", &VoiceConfig::wgStiffness);
  return p;
}
constexpr VoiceParameterLayout sitarLayout()
{
  VoiceParameterLayout p{};
  p.envelopeFromTracks = false;
  // Velocity lives in the pluck excitation (Voice::processSitar_). Scaling the
  // raw output too would double-apply it (VCA and pluck amplitude) and zipper
  // the ringing string on every velocity step.
  p.velocityToAmplitude = false;
  p.slots[static_cast<size_t>(ParamId::Filter)] = control("JAWARI", &VoiceConfig::sitarJawari);
  p.slots[static_cast<size_t>(ParamId::Attack)] = control("PICK", &VoiceConfig::sitarPickHardness);
  p.slots[static_cast<size_t>(ParamId::Decay)] = control("TARAF", &VoiceConfig::sitarTarafAmount);
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
