#pragma once

#include "RecipeEngine.h"
#include "../../utils/AudioRam.h"
#include "../../rpdsp/src/rpdsp/DSPFunctions.h"

namespace VoiceRecipes {
// Patch wiring only: oscillator algorithms and state updates live in rpdsp.
// Coefficients are precomputed into spare state slots on the audio core
// (configure()/trigger()); the control core never writes here.
inline void configurePhaseShape(float shape, float *s) noexcept
{
  const auto c = rpdsp::make_osc_pdmorph_coefficients(shape);
  s[0] = c.knee; s[1] = c.risingSlope; s[2] = c.fallingSlope;
}
inline rpdsp::PhaseDistortionCoefficients phaseShape(const float *s) noexcept
{
  return {s[0], s[1], s[2]};
}
inline void configurePrismWeights(const VoiceConfig &c, float *s) noexcept
{
  const auto weights = rpdsp::make_osc_prism_coefficients(c.macro1, c.macro2);
  for (int h = 0; h < 6; ++h) s[h] = weights.weights[h];
}
inline rpdsp::PrismCoefficients prismWeights(const float *s) noexcept
{
  return {{s[0], s[1], s[2], s[3], s[4], s[5]}};
}
inline float feedbackFm(float inc, const VoiceConfig &c, float *s) noexcept
{
// Two-operator feedback FM: glassy keys at low index, brassy bite as the
// index opens, noise past ~0.35 feedback (lanes stop before that).
  const float mod = rpdsp::osc_fbfm(inc * c.macro2, c.fmModFeedback, 0.0f, s);
  return rpdsp::osc_fbfm(inc, c.macro3, mod * c.macro1, s + 3);
}
inline float PICO2SEQ_AUDIO_FUNC(phaseMorph)(float inc, const VoiceConfig &c, float *s) noexcept
{
  const float pd = rpdsp::osc_pdmorph(inc, phaseShape(s + 2), s);
  const float tri = rpdsp::osc_morphtsq(inc, c.macro2, c.phaseTriangleFold, s + 1);
  return pd * (1.0f - c.macro3) + tri * c.macro3;
}
inline float PICO2SEQ_AUDIO_FUNC(spectralDsf)(float inc, const VoiceConfig &c, float *s) noexcept
{
  const float dsf = rpdsp::osc_dsf(inc, c.macro2, c.macro1, s);
  const float sub = c.spectralSubShape <= 0.0f
      ? rpdsp::osc_pdmorph(inc * c.spectralSubRatio, 0.0f, s + 2)
      : rpdsp::osc_pdmorph(inc * c.spectralSubRatio, phaseShape(s + 3), s + 2);
  return dsf * (1.0f - c.macro3) + sub * c.macro3;
}
inline float PICO2SEQ_AUDIO_FUNC(prism)(float inc, const VoiceConfig &c, float *s) noexcept
{
  const float partials = rpdsp::osc_prism(inc, prismWeights(s + 3), s);
  const float drift = rpdsp::osc_chaosdrift(inc, c.prismDriftChaos, s + 1);
  return partials * (1.0f - c.macro3) + drift * c.macro3;
}

inline void configurePhaseMorph(float, const VoiceConfig &c, float *s) noexcept
{ configurePhaseShape(c.macro1, s + 2); }
inline void configureSpectralDsf(float, const VoiceConfig &c, float *s) noexcept
{ configurePhaseShape(c.spectralSubShape, s + 3); }
inline void configurePrism(float, const VoiceConfig &c, float *s) noexcept
{ configurePrismWeights(c, s + 3); }
inline constexpr auto kFeedbackFm = makeVoiceRecipe<6>(feedbackFm);
inline constexpr auto kPhaseMorph = makeVoiceRecipe<5>(phaseMorph, true, configurePhaseMorph);
inline constexpr auto kSpectralDsf = makeVoiceRecipe<6>(spectralDsf, true, configureSpectralDsf);
inline constexpr auto kPrism = makeVoiceRecipe<9>(prism, true, configurePrism);

// Patch wiring only: oscillator algorithms and state updates belong to rpDSP.
inline float PICO2SEQ_AUDIO_FUNC(reedPipe)(float inc, const VoiceConfig &c, float *s) noexcept
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
inline float PICO2SEQ_AUDIO_FUNC(silkPad)(float inc, const VoiceConfig &c, float *s) noexcept
{
  // Two phase-distortion oscillators, symmetrically detuned around the note.
  const auto shape = phaseShape(s + 2);
  const float a = rpdsp::osc_pdmorph(inc * s[5], shape, s);
  const float b = rpdsp::osc_pdmorph(inc * s[6], shape, s + 1);
  return a * (1.0f - c.macro3) + b * c.macro3;
}
inline float PICO2SEQ_AUDIO_FUNC(hollowBell)(float inc, const VoiceConfig &c, float *s) noexcept
{
  // Ring-mod connection: body * ring adds hollow metallic edge with macro3.
  const float body = rpdsp::osc_pdmorph(inc, phaseShape(s + 2), s);
  const float ring = rpdsp::osc_pdmorph(inc * c.macro1, 0.0f, s + 1);
  return body * (1.0f - c.macro3) + body * ring * c.macro3;
}
inline float PICO2SEQ_AUDIO_FUNC(syncLead)(float inc, const VoiceConfig &c, float *s) noexcept
{
  // Reversing sync[0..2], pitched phase-distortion body[3].
  const float sync = rpdsp::osc_revsync(inc, c.macro1, s);
  const float body = rpdsp::osc_pdmorph(inc, phaseShape(s + 4), s + 3);
  return body * (1.0f - c.macro3) + sync * c.macro3;
}
inline float PICO2SEQ_AUDIO_FUNC(orbitPluck)(float inc, const VoiceConfig &c, float *s) noexcept
{
  // Through-zero FM pluck: bell-like attack melting into a clean body.
  const float mod = rpdsp::osc_pdmorph(inc * c.macro2, 0.0f, s);
  const float fm = rpdsp::osc_tzfm(inc, mod * c.macro1, s + 1);
  const float body = rpdsp::osc_pdmorph(inc, 0.0f, s + 3);
  return fm * (1.0f - c.macro3) + body * c.macro3;
}
inline float PICO2SEQ_AUDIO_FUNC(airChime)(float inc, const VoiceConfig &c, float *s) noexcept
{
  // Harmonic prism[0] with a clean octave[1], without chaotic drift.
  const float partials = rpdsp::osc_prism(inc, prismWeights(s + 2), s);
  const float octave = rpdsp::osc_pdmorph(inc * 2.0f, 0.0f, s + 1);
  return partials * (1.0f - c.macro3) + octave * c.macro3;
}

inline void configureSilkPad(float, const VoiceConfig &c, float *s) noexcept
{
  configurePhaseShape(c.macro1, s + 2);
  s[5] = 1.0f + 0.006f * c.macro2;
  s[6] = 1.0f / s[5];
}
inline void configureHollowBell(float, const VoiceConfig &c, float *s) noexcept
{ configurePhaseShape(c.macro2, s + 2); }
inline void configureSyncLead(float, const VoiceConfig &c, float *s) noexcept
{ configurePhaseShape(c.macro2, s + 4); }
inline void configureAirChime(float, const VoiceConfig &c, float *s) noexcept
{ configurePrismWeights(c, s + 2); }

inline constexpr auto kReedPipe = makeVoiceRecipe<6>(reedPipe, true, configureReedPipe);
inline constexpr auto kSilkPad = makeVoiceRecipe<7>(silkPad, true, configureSilkPad);
inline constexpr auto kHollowBell = makeVoiceRecipe<5>(hollowBell, true, configureHollowBell);
inline constexpr auto kSyncLead = makeVoiceRecipe<7>(syncLead, true, configureSyncLead);
inline constexpr auto kOrbitPluck = makeVoiceRecipe<4>(orbitPluck);
inline constexpr auto kAirChime = makeVoiceRecipe<8>(airChime, true, configureAirChime);
} // namespace VoiceRecipes
