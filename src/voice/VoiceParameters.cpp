#include "VoiceParameters.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

float VoiceParameterBinding::map(float normalized) const noexcept
{
  if (isCentered())
    return dspmap::fmapCentered(normalized, minimum, maximum, center, curve);
  return dspmap::fmap(std::clamp(normalized, 0.0f, 1.0f), minimum, maximum, curve);
}

float VoiceParameterBinding::normalize(float value) const noexcept
{
  if (maximum <= minimum)
    return 0.0f;
  if (isCentered())
    return dspmap::normalizeCentered(value, minimum, maximum, center, curve);
  value = std::clamp(value, minimum, maximum);
  const float linear = (value - minimum) / (maximum - minimum);
  if (curve == dspmap::Mapping::EXP)
    return std::sqrt(linear);
  if (curve == dspmap::Mapping::LOG || curve == dspmap::Mapping::OCTAVE)
    return std::log(value / minimum) / std::log(maximum / minimum);
  return linear;
}

namespace VoiceParameters {
namespace {
constexpr size_t slot(ParamId id) { return static_cast<size_t>(id); }
constexpr VoiceParameterLayout kStandard{};
constexpr VoiceParameterLayout kHardSync = hardSyncLayout();
constexpr VoiceParameterLayout kLegacyLayouts[] = {
    kStandard, waveguideLayout(), hypersawLayout(), noiseStormLayout(), kHardSync};

float stateValue(const VoiceState &s, ParamId id) noexcept
{
  switch (id) {
  case ParamId::Velocity: return s.velocityLevel;
  case ParamId::Filter: return s.filterCutoff;
  case ParamId::Attack: return s.attackTimeSeconds;
  case ParamId::Decay: return s.decayTimeSeconds;
  case ParamId::Sustain: return s.sustainLevel;
  case ParamId::Release: return s.releaseTimeSeconds;
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
  if (slot(id) >= PARAM_ID_COUNT)
    return empty;
  if (config.paramSet == PARAMSET_HARDSYNC && (id == ParamId::Note || id == ParamId::Velocity))
    return kHardSync.slots[slot(id)];
  return layout(config).slots[slot(id)];
}

bool velocityToAmplitude(const VoiceConfig &config) noexcept
{
  return config.paramSet != PARAMSET_HARDSYNC && layout(config).velocityToAmplitude;
}

float mapCutoff(const VoiceParameterLayout &p, float normalized) noexcept
{
  if (p.cutoffCentered())
    return dspmap::fmapCentered(normalized, p.cutoffMinimum, p.cutoffMaximum, p.cutoffCenter,
                                p.cutoffCurve);
  return dspmap::fmap(std::clamp(normalized, 0.0f, 1.0f), p.cutoffMinimum, p.cutoffMaximum,
                      p.cutoffCurve);
}

void apply(VoiceConfig &config, const VoiceState &state) noexcept
{
  // Pitch, octave and timing retain their shared musical units. These six
  // normalized lanes can address any float setting in a recipe/config.
  for (ParamId id : {ParamId::Velocity, ParamId::Filter, ParamId::Attack, ParamId::Decay,
                     ParamId::Sustain, ParamId::Release})
  {
    const auto &b = binding(config, id);
    if (b.target)
      config.*(b.target) = b.map(stateValue(state, id));
  }
}

void seedTracks(Sequencer &sequencer, const VoiceConfig &config)
{
  for (size_t i = 0; i < PARAM_ID_COUNT; ++i)
  {
    const auto id = static_cast<ParamId>(i);
    const auto &b = binding(config, id);
    if (!b.seed)
      continue;
    const float value = b.target ? b.normalize(config.*(b.target)) : b.defaultNormalized;
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
    // Envelope amount, and the cutoff it opens to at the envelope's peak. The
    // lane no longer sets the frequency, so a bare Hz readout would lie.
    const auto &p = layout(config);
    const float octaves = std::max(0.0f, config.filterEnvelopeOctaves) *
                          std::clamp(normalized, 0.0f, 1.0f);
    const float base = mapCutoff(p, config.filterCutoffBase);
    const float peak = base * std::exp2(octaves * (1.0f - std::clamp(config.filterEnvelopeRest, 0.0f, 1.0f)));
    std::snprintf(output, capacity, "%.0f%% %.0fHz", normalized * 100.0f, peak);
    // "<amount>% <peak>": how far the contour opens, and the cutoff it reaches.
    return true;
  }
  const float value = b.map(normalized);
  switch (b.unit) {
  case VoiceParameterUnit::Percent: std::snprintf(output, capacity, "%.0f%%", value * 100.0f); break;
  case VoiceParameterUnit::Seconds: std::snprintf(output, capacity, "%.2fs", value); break;
  case VoiceParameterUnit::Semitones: std::snprintf(output, capacity, "%+.1fst", value); break;
  case VoiceParameterUnit::Ratio: std::snprintf(output, capacity, "%.2fx", value); break;
  case VoiceParameterUnit::Hertz: std::snprintf(output, capacity, "%.0fHz", value); break;
  default:
    if (b.target) {
      std::snprintf(output, capacity, "%.0f%%", value * 100.0f);
      return true;
    }
    return false;
  }
  return true;
}
} // namespace VoiceParameters
