#pragma once

#include "../VoiceConfig.h"
#include <array>
#include <cstddef>

// Small rpdsp patches share one bounded state buffer. The callback and state
// requirement are declared alongside each patch; no Voice switch is needed.
struct VoiceRecipe
{
  using Process = float (*)(float increment, const VoiceConfig &, float *state) noexcept;
  using Configure = void (*)(float sampleRate, const VoiceConfig &, float *state) noexcept;
  Process process;
  Configure configure;
  bool resetOnTrigger;
};

class RecipeEngine
{
public:
  static constexpr size_t kStateFloats = 16;

  void prepare(float sampleRate) noexcept { sampleRate_ = sampleRate; inverseSampleRate_ = 1.0f / sampleRate; reset(); }
  void select(const VoiceRecipe *recipe) noexcept
  {
    if (recipe_ != recipe) { recipe_ = recipe; reset(); }
  }
  void configure(const VoiceConfig &config) noexcept
  {
    if (recipe_ && recipe_->configure) recipe_->configure(sampleRate_, config, state_.data());
  }
  void reset() noexcept { state_.fill(0.0f); }
  void trigger(const VoiceConfig &config) noexcept
  {
    if (recipe_ && recipe_->resetOnTrigger) { reset(); configure(config); }
  }
  float process(float frequency, const VoiceConfig &config) noexcept
  {
    return recipe_ ? recipe_->process(frequency * inverseSampleRate_, config, state_.data()) : 0.0f;
  }

private:
  std::array<float, kStateFloats> state_{};
  const VoiceRecipe *recipe_ = nullptr;
  float sampleRate_ = 48000.0f;
  float inverseSampleRate_ = 1.0f / 48000.0f;
};

template <size_t StateFloats>
constexpr VoiceRecipe makeVoiceRecipe(VoiceRecipe::Process process, bool resetOnTrigger = true,
                                      VoiceRecipe::Configure configure = nullptr)
{
  static_assert(StateFloats <= RecipeEngine::kStateFloats, "Recipe exceeds fixed voice state storage");
  return {process, configure, resetOnTrigger};
}
