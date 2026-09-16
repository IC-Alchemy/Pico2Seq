#include "ParameterManager.h"

#include <algorithm> // For std::max, std::min
#include <chrono>    // For std::chrono::high_resolution_clock (for seeding)
#include <cmath>     // For roundf
#include <cstdint>   // For uint32_t
#include <variant>   // For ParameterValueType (std::variant, via SequencerDefs.h)

// Encoder parameter bounds management functions moved to src/sensors/EncoderManager.cpp

// Internal utilities for randomization
namespace
{
  constexpr size_t kParamCount = static_cast<size_t>(ParamId::Count);
  static_assert(kParamCount == (sizeof(CORE_PARAMETERS) / sizeof(CORE_PARAMETERS[0])),
                "ParamId::Count must match CORE_PARAMETERS size");

  // Lightweight Linear Congruential Generator (LCG) to avoid TLS issues
  static uint32_t lcg_state = 0;

  void seed_lcg()
  {
    lcg_state = static_cast<uint32_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
  }

  void seed_lcg(uint64_t seed)
  {
    lcg_state = static_cast<uint32_t>(seed ^ (seed >> 32));
  }

  uint32_t lcg_rand()
  {
    lcg_state = 1664525u * lcg_state + 1013904223u;
    return lcg_state;
  }

  float lcg_rand_float(float min, float max)
  {
    uint32_t r = lcg_rand();
    float normalized = static_cast<float>(r) / static_cast<float>(UINT32_MAX);
    return min + normalized * (max - min);
  }

  int lcg_rand_int(int min, int max)
  {
    if (min == max)
      return min;
    uint32_t r = lcg_rand();
    return min + (r % (max - min + 1));
  }
} // namespace

void ParameterManager::init()
{
  for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i)
  {
    // Initialize each track with its default value and step count from
    // CORE_PARAMETERS. The step count must be passed explicitly now --
    // rpdsp::ParameterTrack::init() defaults to MaxSteps (64), not the old
    // hardcoded SequencerConstants::DEFAULT_STEPS_COUNT (16).
    _tracks[i].init(parameterValueAsFloat(CORE_PARAMETERS[i].defaultValue),
                     CORE_PARAMETERS[i].defaultSteps);
  }
}

void ParameterManager::setStepCount(ParamId id, uint8_t steps)
{
  if (static_cast<size_t>(id) >= kParamCount)
  {
    return;
  }
  _tracks[static_cast<size_t>(id)].resize(steps);
}

uint8_t ParameterManager::getStepCount(ParamId id) const
{
  if (static_cast<size_t>(id) >= kParamCount)
  {
    return 0;
  }
  uint8_t count = _tracks[static_cast<size_t>(id)].stepCount();
  return count;
}

float ParameterManager::getValue(ParamId id, uint8_t stepIdx) const
{
  if (static_cast<size_t>(id) >= kParamCount)
  {
    return 0.0f;
  }
  float value = _tracks[static_cast<size_t>(id)].getValue(stepIdx);
  return value;
}

void ParameterManager::setValue(ParamId id, uint8_t stepIdx, float value)
{
  if (static_cast<size_t>(id) >= kParamCount)
  {
    return;
  }

  // Apply clamping and rounding based on parameter definition
  const auto &paramDef = CORE_PARAMETERS[static_cast<size_t>(id)];
  float minVal = parameterValueAsFloat(paramDef.minValue);
  float maxVal = parameterValueAsFloat(paramDef.maxValue);

  float clampedValue = std::max(minVal, std::min(value, maxVal));

  if (paramDef.isBinary)
  { // For boolean parameters, round to 0 or 1
    clampedValue = (clampedValue > 0.5f) ? 1.0f : 0.0f;
  }
  else if (paramDef.minValue.index() == 0)
  { // If min value is int, assume integer parameter
    clampedValue = roundf(clampedValue);
  }

  _tracks[static_cast<size_t>(id)].setValue(stepIdx, clampedValue);
}

void ParameterManager::copyStep(uint8_t srcStep, uint8_t dstStep)
{
  if (srcStep >= SequencerConstants::MAX_STEPS_COUNT || dstStep >= SequencerConstants::MAX_STEPS_COUNT)
  {
    return;
  }
  for (size_t i = 0; i < kParamCount; ++i)
  {
    const float val = _tracks[i].getValue(srcStep);
    _tracks[i].setValue(dstStep, val);
  }
}

float ParameterManager::getRawValue(ParamId id, uint8_t stepIdx) const
{
  if (static_cast<size_t>(id) >= kParamCount || stepIdx >= SequencerConstants::MAX_STEPS_COUNT)
    return 0.0f;
  return _tracks[static_cast<size_t>(id)].rawValue(stepIdx);
}

void ParameterManager::setRawValue(ParamId id, uint8_t stepIdx, float value)
{
  if (static_cast<size_t>(id) >= kParamCount || stepIdx >= SequencerConstants::MAX_STEPS_COUNT)
    return;
  _tracks[static_cast<size_t>(id)].setValue(stepIdx, value);
}

void ParameterManager::setLaneAmount(ParamId id, uint8_t percent)
{
  if (static_cast<size_t>(id) >= kParamCount)
    return;
  _laneAmounts[static_cast<size_t>(id)] = std::min<uint8_t>(percent, 100);
}

uint8_t ParameterManager::getLaneAmount(ParamId id) const
{
  return static_cast<size_t>(id) < kParamCount ? _laneAmounts[static_cast<size_t>(id)] : 0;
}

void ParameterManager::randomizeParameters(uint8_t depthPercent, uint64_t seed)
{
  if (seed == 0)
    seed_lcg();
  else
    seed_lcg(seed);

  const float depth = std::min<uint8_t>(depthPercent, 100) / 100.0f;
  for (size_t i = 0; i < kParamCount; ++i)
  {
    const auto paramId = static_cast<ParamId>(i);
    // The rhythm stays the player's: gates and slides are never rewritten.
    if (paramId == ParamId::Gate || paramId == ParamId::Slide || _laneAmounts[i] == 0)
      continue;
    const float amount = _laneAmounts[i] / 100.0f;
    const uint8_t steps = _tracks[i].stepCount();
    for (uint8_t step = 0; step < steps; ++step)
    {
      switch (paramId)
      {
      case ParamId::Note:
        // A compact run of scale steps; playback quantizes them to the scale.
        setValue(paramId, step, static_cast<float>(lcg_rand_int(0, 12)));
        break;
      case ParamId::Octave:
      case ParamId::GateLength:
        setValue(paramId, step, mapNormalizedValueToParamRange(paramId, 0.5f));
        break;
      default:
      {
        // The sum of two uniform draws is triangular on [-1, 1]: most steps
        // stay near the base, a few reach the edge of the depth.
        const float spread = lcg_rand_float(0.0f, 1.0f) + lcg_rand_float(0.0f, 1.0f) - 1.0f;
        setValue(paramId, step, 0.5f + 0.5f * spread * depth * amount);
      }
      break;
      }
    }
  }
}
