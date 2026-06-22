#include "ParameterManager.h"

#include <algorithm> // For std::max, std::min
#include <chrono>    // For std::chrono::high_resolution_clock (for seeding)
#include <cmath>     // For roundf
#include <cstdint>   // For uint32_t
#include <variant>   // For std::visit

// AS5600 parameter bounds management functions moved to src/sensors/AS5600Manager.cpp

// Helper function to safely get float from ParameterValueType variant
// This function is internal to ParameterManager.cpp
static float getFloatFromParameterValueType(const ParameterValueType &v)
{
  return std::visit([](auto &&arg) -> float
                    {
                      using T = std::decay_t<decltype(arg)>;
                      if constexpr (std::is_same_v<T, float>)
                        return arg;
                      if constexpr (std::is_same_v<T, int>)
                        return static_cast<float>(arg);
                      if constexpr (std::is_same_v<T, bool>)
                        return arg ? 1.0f : 0.0f;
                      return 0.0f; // Fallback for unexpected types
                    },
                    v);
}

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
    // Initialize each track with its default value from CORE_PARAMETERS
    _tracks[i].init(getFloatFromParameterValueType(CORE_PARAMETERS[i].defaultValue));
  }
}

void ParameterManager::setStepCount(ParamId id, uint8_t steps)
{
  _tracks[static_cast<size_t>(id)].resize(steps);
}

uint8_t ParameterManager::getStepCount(ParamId id) const
{
  uint8_t count = _tracks[static_cast<size_t>(id)].currentStepCount;
  return count;
}

float ParameterManager::getValue(ParamId id, uint8_t stepIdx) const
{
  float value = _tracks[static_cast<size_t>(id)].getValue(stepIdx);
  return value;
}

void ParameterManager::setValue(ParamId id, uint8_t stepIdx, float value)
{

  // Apply clamping and rounding based on parameter definition
  const auto &paramDef = CORE_PARAMETERS[static_cast<size_t>(id)];
  float minVal = getFloatFromParameterValueType(paramDef.minValue);
  float maxVal = getFloatFromParameterValueType(paramDef.maxValue);

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

void ParameterManager::randomizeParameters()
{
  // Seed the LCG with current time
  seed_lcg();

  for (size_t i = 0; i < kParamCount; ++i)
  {
    const auto paramId = static_cast<ParamId>(i);

    // Ensure Slide parameter track always uses maximum length (extents-safe)
    if (paramId == ParamId::Slide)
    {
      _tracks[i].currentStepCount = SequencerConstants::MAX_STEPS_COUNT;
    }

    const auto &def = CORE_PARAMETERS[i];
    const float minVal = getFloatFromParameterValueType(def.minValue);
    const float maxVal = getFloatFromParameterValueType(def.maxValue);

    auto &track = _tracks[i];
    const uint8_t steps = track.currentStepCount;

    // Safety: bounds check on step count
    if (steps == 0 || steps > SequencerConstants::MAX_STEPS_COUNT)
    {
      continue;
    }

    for (uint8_t step = 0; step < steps; ++step)
    {
      switch (paramId)
      {
      case ParamId::Slide:
      {
        const bool on = (lcg_rand_int(0, 12) == 0); // 1/13 chance
        track.setValue(step, on ? 1.0f : 0.0f);
      }
      break;

      case ParamId::Gate:
      {
        if ((step % 2u) == 0u) [[likely]]
        {
          // Even steps: 75% chance of being 1
          const bool isZero = (lcg_rand_int(0, 3) == 0); // 1/4 zeros
          track.setValue(step, isZero ? 0.0f : 1.0f);
        }
        else
        {
          // Odd steps: ~33% chance of being 1
          const bool one = (lcg_rand_int(0, 2) == 0); // 1/3 ones
          track.setValue(step, one ? 1.0f : 0.0f);
        }
      }
      break;

      case ParamId::GateLength:
      {
        // Draw once for consistent branching probability
        const int draw = lcg_rand_int(0, 8);
        if (draw == 0)
        {
          track.setValue(step, lcg_rand_float(0.25f, 0.8f));
        }
        else if (draw < 3)
        {
          track.setValue(step, lcg_rand_float(0.01f, 0.5f));
        }
        else
        {
          track.setValue(step, lcg_rand_float(0.1f, 0.5f));
        }
      }
      break;

      case ParamId::Filter:
      {
        static constexpr float kFilterMin = 0.2f;
        static constexpr float kFilterMax = 0.95f;
        track.setValue(step, lcg_rand_float(kFilterMin, kFilterMax));
      }
      break;

      case ParamId::Attack:
      {
        if ((step % 2u) == 0u)
        {
          // Even steps: rare long attacks
          if (lcg_rand_int(0, 8) == 0)
          { // 1/9 chance
            track.setValue(step, lcg_rand_float(0.05f, 0.5f));
          }
          else
          {
            track.setValue(step, lcg_rand_float(0.0f, 0.03f));
          }
        }
        else
        {
          // Odd steps
          if (lcg_rand_int(0, 12) == 0)
          { // 1/13 chance
            track.setValue(step, lcg_rand_float(0.0f, 0.2f));
          }
          else
          {
            track.setValue(step, lcg_rand_float(0.0f, 0.03f));
          }
        }
      }
      break;

      case ParamId::Decay:
      {
        if ((step % 2u) == 0u)
        {
          // Even steps: 25% chance long decay
          if (lcg_rand_int(0, 3) == 0)
          { // 1/4 chance
            track.setValue(step, lcg_rand_float(0.2f, 0.9f));
          }
          else
          {
            track.setValue(step, lcg_rand_float(0.001f, 0.3f));
          }
        }
        else
        {
          track.setValue(step, lcg_rand_float(0.05f, 0.2f));
        }
      }
      break;

      case ParamId::Note:
      case ParamId::Velocity:
      case ParamId::Octave:
      default:
      {
        // Default rule: uniform across the parameter's defined range
        track.setValue(step, lcg_rand_float(minVal, maxVal));
      }
      break;
      }
    }
  }
}