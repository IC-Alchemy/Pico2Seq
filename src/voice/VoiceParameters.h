#pragma once

#include "VoiceConfig.h"
#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include "../utils/DspMapping.h"
#include <array>

class Sequencer;

enum class VoiceParameterUnit : uint8_t { Standard, Percent, Seconds, Semitones, Ratio, Hertz };

// The same binding drives DSP values, preset seeding, and OLED formatting.
// A null target keeps the shared pitch/gate/envelope behavior for that lane.
struct VoiceParameterBinding
{
  const char *name = nullptr; // null keeps the standard lane label
  float VoiceConfig::*target = nullptr;
  float minimum = 0.0f;
  float maximum = 1.0f;
  dspmap::Mapping curve = dspmap::Mapping::LINEAR;
  VoiceParameterUnit unit = VoiceParameterUnit::Standard;
  bool seed = false;
  float defaultNormalized = 0.5f; // used when seed=true and target=null

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
  float cutoffMinimum = 120.0f;
  float cutoffMaximum = 5000.0f;
};

namespace VoiceParameters {
inline constexpr float kWaveguideT60Min = 0.05f;
inline constexpr float kWaveguideT60Max = 10.0f;

const VoiceParameterLayout &layout(const VoiceConfig &config) noexcept;
const VoiceParameterBinding &binding(const VoiceConfig &config, ParamId id) noexcept;
void apply(VoiceConfig &config, const VoiceState &state) noexcept;
void seedTracks(Sequencer &sequencer, const VoiceConfig &config);
bool formatValue(const VoiceConfig &config, ParamId id, float normalized,
                 char *output, size_t capacity) noexcept;
} // namespace VoiceParameters
