#include "VoiceParameters.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

float VoiceParameterBinding::map(float normalized) const noexcept
{
  return dspmap::fmap(std::clamp(normalized, 0.0f, 1.0f), minimum, maximum, curve);
}

float VoiceParameterBinding::normalize(float value) const noexcept
{
  if (maximum <= minimum)
    return 0.0f;
  value = std::clamp(value, minimum, maximum);
  const float linear = (value - minimum) / (maximum - minimum);
  if (curve == dspmap::Mapping::EXP)
    return std::sqrt(linear);
  if (curve == dspmap::Mapping::LOG)
    return std::log(value / minimum) / std::log(maximum / minimum);
  return linear;
}

namespace VoiceParameters {
namespace {
constexpr size_t slot(ParamId id) { return static_cast<size_t>(id); }
constexpr VoiceParameterBinding control(const char *name, float VoiceConfig::*target)
{
  return {name, target, 0.0f, 1.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true};
}
constexpr VoiceParameterLayout makeWaveguide()
{
  VoiceParameterLayout p{};
  p.envelopeFromTracks = false;
  p.slots[slot(ParamId::Filter)] = control("Bright", &VoiceConfig::wgBrightness);
  p.slots[slot(ParamId::Attack)] = control("Pick", &VoiceConfig::wgPickHardness);
  p.slots[slot(ParamId::Decay)] = {"T60", &VoiceConfig::wgT60, kWaveguideT60Min,
      kWaveguideT60Max, dspmap::Mapping::EXP, VoiceParameterUnit::Seconds, true};
  return p;
}
constexpr VoiceParameterLayout makeHypersaw()
{
  VoiceParameterLayout p{};
  p.envelopeFromTracks = false;
  p.cutoffMinimum = 150.0f;
  p.cutoffMaximum = 8000.0f;
  p.slots[slot(ParamId::Attack)] = control("Detune", &VoiceConfig::hypersawDetune);
  p.slots[slot(ParamId::Decay)] = control("Mix", &VoiceConfig::hypersawMix);
  return p;
}
constexpr VoiceParameterLayout makeNoiseStorm()
{
  VoiceParameterLayout p = makeHypersaw();
  p.slots[slot(ParamId::Filter)] = control("Color", &VoiceConfig::noiseSwarmColor);
  p.slots[slot(ParamId::Attack)] = control("Regen", &VoiceConfig::noiseSwarmRegen);
  p.slots[slot(ParamId::Decay)] = control("Chaos", &VoiceConfig::noiseChaosLevel);
  return p;
}
constexpr VoiceParameterLayout makeHardSync()
{
  VoiceParameterLayout p{};
  p.velocityToAmplitude = false;
  p.slots[slot(ParamId::Note)].name = "Master";
  p.slots[slot(ParamId::Velocity)] = {"Slave", nullptr, -24.0f, 24.0f,
      dspmap::Mapping::LINEAR, VoiceParameterUnit::Semitones, true, 0.5f};
  return p;
}
constexpr VoiceParameterLayout kStandard{};
constexpr VoiceParameterLayout kLegacyLayouts[] = {
    kStandard, makeWaveguide(), makeHypersaw(), makeNoiseStorm(), makeHardSync()};

float stateValue(const VoiceState &s, ParamId id) noexcept
{
  switch (id) {
  case ParamId::Velocity: return s.velocityLevel;
  case ParamId::Filter: return s.filterCutoff;
  case ParamId::Attack: return s.attackTimeSeconds;
  case ParamId::Decay: return s.decayTimeSeconds;
  default: return 0.0f;
  }
}
} // namespace

const VoiceParameterLayout &layout(const VoiceConfig &config) noexcept
{
  if (config.parameters)
    return *config.parameters;
  return config.paramSet < std::size(kLegacyLayouts) ? kLegacyLayouts[config.paramSet] : kStandard;
}

const VoiceParameterBinding &binding(const VoiceConfig &config, ParamId id) noexcept
{
  static constexpr VoiceParameterBinding empty{};
  return slot(id) < PARAM_ID_COUNT ? layout(config).slots[slot(id)] : empty;
}

void apply(VoiceConfig &config, const VoiceState &state) noexcept
{
  // Pitch, octave and timing retain their shared musical units. These four
  // normalized lanes can address any float setting in a recipe/config.
  for (ParamId id : {ParamId::Velocity, ParamId::Filter, ParamId::Attack, ParamId::Decay})
  {
    const auto &b = binding(config, id);
    if (b.target)
      config.*(b.target) = b.map(stateValue(state, id));
  }
}

void seedTracks(Sequencer &sequencer, const VoiceConfig &config)
{
  const auto &p = layout(config);
  for (size_t i = 0; i < p.slots.size(); ++i)
  {
    const auto &b = p.slots[i];
    if (!b.seed)
      continue;
    const float value = b.target ? b.normalize(config.*(b.target)) : b.defaultNormalized;
    const auto id = static_cast<ParamId>(i);
    for (uint8_t step = 0; step < sequencer.getParameterStepCount(id); ++step)
      sequencer.setStepParameterValue(id, step, value);
  }
}

bool formatValue(const VoiceConfig &config, ParamId id, float normalized,
                 char *output, size_t capacity) noexcept
{
  if (!output || capacity == 0)
    return false;
  const auto &b = binding(config, id);
  if (id == ParamId::Filter && !b.target) {
    const auto &p = layout(config);
    const float frequency = dspmap::fmap(std::clamp(normalized, 0.0f, 1.0f),
                                        p.cutoffMinimum, p.cutoffMaximum, dspmap::Mapping::EXP);
    std::snprintf(output, capacity, "%.0fHz", frequency);
    return true;
  }
  const float value = b.map(normalized);
  switch (b.unit) {
  case VoiceParameterUnit::Percent: std::snprintf(output, capacity, "%.0f%%", value * 100.0f); break;
  case VoiceParameterUnit::Seconds: std::snprintf(output, capacity, "%.2fs", value); break;
  case VoiceParameterUnit::Semitones: std::snprintf(output, capacity, "%+.0fst", value); break;
  case VoiceParameterUnit::Ratio: std::snprintf(output, capacity, "%.2fx", value); break;
  case VoiceParameterUnit::Hertz: std::snprintf(output, capacity, "%.0fHz", value); break;
  default: return false;
  }
  return true;
}
} // namespace VoiceParameters
