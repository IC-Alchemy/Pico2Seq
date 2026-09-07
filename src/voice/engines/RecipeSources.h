#pragma once

#include "RecipeEngine.h"
#include "../../rpdsp/src/rpdsp/DSPFunctions.h"

namespace VoiceRecipes {
inline float feedbackFm(float inc, const VoiceConfig &c, float *s) noexcept
{
  // Two feedback-FM operators: modulator state[0..2], carrier state[3..5].
  const float mod = rpdsp::osc_fbfm(inc * c.macro2, 0.0f, 0.0f, s);
  return rpdsp::osc_fbfm(inc, c.macro3, mod * c.macro1, s + 3);
}
inline float phaseMorph(float inc, const VoiceConfig &c, float *s) noexcept
{
  const float pd = rpdsp::osc_pdmorph(inc, c.macro1, s);
  const float tri = rpdsp::osc_morphtsq(inc, c.macro2, 0.0f, s + 1);
  return pd * (1.0f - c.macro3) + tri * c.macro3;
}
inline float spectralDsf(float inc, const VoiceConfig &c, float *s) noexcept
{
  const float dsf = rpdsp::osc_dsf(inc, c.macro2, c.macro1, s);
  const float sub = rpdsp::osc_pdmorph(inc * 0.5f, 0.0f, s + 2);
  return dsf * (1.0f - c.macro3) + sub * c.macro3;
}
inline float prism(float inc, const VoiceConfig &c, float *s) noexcept
{
  const float partials = rpdsp::osc_prism(inc, c.macro1, c.macro2, s);
  const float drift = rpdsp::osc_chaosdrift(inc, 0.65f, s + 1);
  return partials * (1.0f - c.macro3) + drift * c.macro3;
}

inline constexpr auto kFeedbackFm = makeVoiceRecipe<6>(feedbackFm);
inline constexpr auto kPhaseMorph = makeVoiceRecipe<2>(phaseMorph);
inline constexpr auto kSpectralDsf = makeVoiceRecipe<3>(spectralDsf);
inline constexpr auto kPrism = makeVoiceRecipe<3>(prism);
} // namespace VoiceRecipes
