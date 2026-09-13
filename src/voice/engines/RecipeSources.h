#pragma once

#include "RecipeEngine.h"
#include "../../rpdsp/src/rpdsp/DSPFunctions.h"

namespace VoiceRecipes {
inline float feedbackFm(float inc, const VoiceConfig &c, float *s) noexcept
{
  // Two feedback-FM operators: modulator state[0..2], carrier state[3..5].
  const float mod = rpdsp::osc_fbfm(inc * c.macro2, c.fmModFeedback, 0.0f, s);
  return rpdsp::osc_fbfm(inc, c.macro3, mod * c.macro1, s + 3);
}
inline float phaseMorph(float inc, const VoiceConfig &c, float *s) noexcept
{
  const float pd = rpdsp::osc_pdmorph(inc, c.macro1, s);
  const float tri = rpdsp::osc_morphtsq(inc, c.macro2, c.phaseTriangleFold, s + 1);
  return pd * (1.0f - c.macro3) + tri * c.macro3;
}
inline float spectralDsf(float inc, const VoiceConfig &c, float *s) noexcept
{
  const float dsf = rpdsp::osc_dsf(inc, c.macro2, c.macro1, s);
  const float sub = rpdsp::osc_pdmorph(inc * c.spectralSubRatio, c.spectralSubShape, s + 2);
  return dsf * (1.0f - c.macro3) + sub * c.macro3;
}
inline float prism(float inc, const VoiceConfig &c, float *s) noexcept
{
  const float partials = rpdsp::osc_prism(inc, c.macro1, c.macro2, s);
  const float drift = rpdsp::osc_chaosdrift(inc, c.prismDriftChaos, s + 1);
  return partials * (1.0f - c.macro3) + drift * c.macro3;
}

inline constexpr auto kFeedbackFm = makeVoiceRecipe<6>(feedbackFm);
inline constexpr auto kPhaseMorph = makeVoiceRecipe<2>(phaseMorph);
inline constexpr auto kSpectralDsf = makeVoiceRecipe<3>(spectralDsf);
inline constexpr auto kPrism = makeVoiceRecipe<3>(prism);

// Patch wiring only: oscillator algorithms and state updates belong to rpDSP.
inline float reedPipe(float inc, const VoiceConfig &c, float *s) noexcept
{
  // Formant[0..3], fundamental[4], configured decay retention[5].
  const float reed = rpdsp::osc_formant(inc, inc * c.macro1, s[5], s);
  const float body = rpdsp::osc_pdmorph(inc, 0.0f, s + 4);
  return reed * (1.0f - c.macro3) + body * c.macro3;
}
inline void configureReedPipe(float rate, const VoiceConfig &c, float *s) noexcept
{
  const float retention = 0.990f + 0.0095f * c.macro2;
  s[5] = 1.0f - rpdsp::recipe_rate_at_sample_rate(1.0f - retention, rate);
}
inline float silkPad(float inc, const VoiceConfig &c, float *s) noexcept
{
  // Two phase-distortion oscillators, symmetrically detuned around the note.
  const float ratio = 1.0f + 0.006f * c.macro2;
  const float a = rpdsp::osc_pdmorph(inc * ratio, c.macro1, s);
  const float b = rpdsp::osc_pdmorph(inc / ratio, c.macro1, s + 1);
  return a * (1.0f - c.macro3) + b * c.macro3;
}
inline float hollowBell(float inc, const VoiceConfig &c, float *s) noexcept
{
  // Multiplication connects two existing oscillators as a ring-modulator.
  const float body = rpdsp::osc_pdmorph(inc, c.macro2, s);
  const float ring = rpdsp::osc_pdmorph(inc * c.macro1, 0.0f, s + 1);
  return body * (1.0f - c.macro3) + body * ring * c.macro3;
}
inline float syncLead(float inc, const VoiceConfig &c, float *s) noexcept
{
  // Reversing sync[0..2], pitched phase-distortion body[3].
  const float sync = rpdsp::osc_revsync(inc, c.macro1, s);
  const float body = rpdsp::osc_pdmorph(inc, c.macro2, s + 3);
  return body * (1.0f - c.macro3) + sync * c.macro3;
}
inline float orbitPluck(float inc, const VoiceConfig &c, float *s) noexcept
{
  // Sine modulator[0], through-zero FM carrier[1..2], clean body[3].
  const float mod = rpdsp::osc_pdmorph(inc * c.macro2, 0.0f, s);
  const float fm = rpdsp::osc_tzfm(inc, mod * c.macro1, s + 1);
  const float body = rpdsp::osc_pdmorph(inc, 0.0f, s + 3);
  return fm * (1.0f - c.macro3) + body * c.macro3;
}
inline float airChime(float inc, const VoiceConfig &c, float *s) noexcept
{
  // Harmonic prism[0] with a clean octave[1], without chaotic drift.
  const float partials = rpdsp::osc_prism(inc, c.macro1, c.macro2, s);
  const float octave = rpdsp::osc_pdmorph(inc * 2.0f, 0.0f, s + 1);
  return partials * (1.0f - c.macro3) + octave * c.macro3;
}

inline constexpr auto kReedPipe = makeVoiceRecipe<6>(reedPipe, true, configureReedPipe);
inline constexpr auto kSilkPad = makeVoiceRecipe<2>(silkPad);
inline constexpr auto kHollowBell = makeVoiceRecipe<2>(hollowBell);
inline constexpr auto kSyncLead = makeVoiceRecipe<4>(syncLead);
inline constexpr auto kOrbitPluck = makeVoiceRecipe<4>(orbitPluck);
inline constexpr auto kAirChime = makeVoiceRecipe<2>(airChime);
} // namespace VoiceRecipes
