#pragma once

#include "../VoiceParameters.h"
#include "../engines/RecipeSources.h"

namespace VoicePresets {
// Recipe patches: three timbre lanes (Filter/Attack/Decay slots) with the
// envelope from patch defaults — sequenced lanes play timbre, not ADSR.
// Note/Velocity/Octave/GateLength/Slide and Gate stay shared.
constexpr VoiceParameterLayout recipeLayout(VoiceParameterBinding color,
                                            VoiceParameterBinding shape,
                                            VoiceParameterBinding character)
{
  VoiceParameterLayout p{};
  p.envelopeFromTracks = false;
  p.slots[static_cast<size_t>(ParamId::Filter)] = color;
  p.slots[static_cast<size_t>(ParamId::Attack)] = shape;
  p.slots[static_cast<size_t>(ParamId::Decay)] = character;
  return p;
}
// A macro lane: its span's center is the musical operating point at lane 0.5.
constexpr VoiceParameterBinding macroLane(const char *name, float VoiceConfig::*macro,
                                          VoiceParameters::Span span, dspmap::Mapping curve,
                                          VoiceParameterUnit unit)
{
  return {name, macro, span.minimum, span.maximum, curve, unit, true, 0.5f, span.center};
}

// FM glass: breathy keys (high ratio, low feedback); FM bass: round sub with
// a short pluck transient. PhaseMorph blends phase-distortion edge into
// triangle fold; Spectral stacks DSF brightness over a sub; Prism/ChaosPrism
// trade focused harmonics for drifting chorus.

// FM: Bessel index stays vocal below ~0.5 and saw-like near 1; integer ratios
// stay tonal, and 4.77 is Yamaha's documented metallic extreme; feedback past
// ~0.35 turns to noise, so the lane stops there.
constexpr VoiceParameterLayout fmLayout()
{
  using M = dspmap::Mapping;
  using U = VoiceParameterUnit;
  return recipeLayout(macroLane("Index", &VoiceConfig::macro1, {0.0f, 0.30f, 1.0f}, M::EXP, U::Percent),
                      macroLane("Ratio", &VoiceConfig::macro2, {0.5f, 2.0f, 4.77f}, M::OCTAVE, U::Ratio),
                      macroLane("Feedback", &VoiceConfig::macro3, {0.0f, 0.10f, 0.35f}, M::EXP, U::Percent));
}
constexpr VoiceParameterLayout phaseLayout()
{
  using M = dspmap::Mapping;
  using U = VoiceParameterUnit;
  return recipeLayout(macroLane("Shape", &VoiceConfig::macro1, {0.0f, 0.5f, 1.0f}, M::LINEAR, U::Percent),
                      macroLane("Skew", &VoiceConfig::macro2, {-1.0f, 0.0f, 1.0f}, M::LINEAR, U::Percent),
                      macroLane("Blend", &VoiceConfig::macro3, {0.0f, 0.5f, 1.0f}, M::LINEAR, U::Percent));
}
// DSF: spacing on an octave taper reaches both the odd and even harmonic zones.
constexpr VoiceParameterLayout dsfLayout()
{
  using M = dspmap::Mapping;
  using U = VoiceParameterUnit;
  return recipeLayout(macroLane("Bright", &VoiceConfig::macro1, {0.0f, 0.45f, 0.9f}, M::LINEAR, U::Percent),
                      macroLane("Spacing", &VoiceConfig::macro2, {0.5f, 2.0f, 5.07f}, M::OCTAVE, U::Ratio),
                      macroLane("Sub", &VoiceConfig::macro3, {0.0f, 0.3f, 1.0f}, M::LINEAR, U::Percent));
}
// Prism: drift past ~0.65 is seasick, so the lane stops at 0.85 (ChaosPrism
// rests at 0.65).
constexpr VoiceParameterLayout prismLayout()
{
  using M = dspmap::Mapping;
  using U = VoiceParameterUnit;
  return recipeLayout(macroLane("Focus", &VoiceConfig::macro1, {0.0f, 0.45f, 1.0f}, M::LINEAR, U::Percent),
                      macroLane("Spread", &VoiceConfig::macro2, {0.0f, 0.55f, 1.0f}, M::LINEAR, U::Percent),
                      macroLane("Drift", &VoiceConfig::macro3, {0.0f, 0.30f, 0.85f}, M::LINEAR, U::Percent));
}
inline constexpr auto kFmGlassLayout = fmLayout();
inline constexpr auto kFmBassLayout = fmLayout();
inline constexpr auto kPhaseMorphLayout = phaseLayout();
inline constexpr auto kSpectralLayout = dsfLayout();
inline constexpr auto kPrismLayout = prismLayout();
inline constexpr auto kChaosPrismLayout = prismLayout();

constexpr VoiceConfig recipeVoice(const VoiceRecipe &recipe, const VoiceParameterLayout &parameters,
                                 float color, float shape, float character)
{
  VoiceConfig c{};
  c.engine = ENGINE_RECIPE;
  c.oscillatorCount = 0;
  c.recipe = &recipe;
  c.parameters = &parameters;
  c.macro1 = color;
  c.macro2 = shape;
  c.macro3 = character;
  c.filterType = FILTER_SVF;
  c.hasFilter = false;
  c.highPassFreq = 30.0f;
  c.highPassRes = 0.0f;
  c.defaultAttack = 0.003f;
  c.defaultDecay = 0.4f;
  c.defaultSustain = 0.45f;
  c.defaultRelease = 0.2f;
  c.outputLevel = 0.65f;
  return c;
}
constexpr VoiceConfig makeFmGlass() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kFeedbackFm, kFmGlassLayout, 0.32f, 3.5f, 0.08f);
  c.defaultSustain = 0.12f;
  c.defaultRelease = 0.5f;
  return c;
}
constexpr VoiceConfig makeFmBass() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kFeedbackFm, kFmBassLayout, 0.18f, 1.0f, 0.2f);
  c.defaultDecay = 0.16f;
  c.defaultRelease = 0.08f;
  return c;
}
constexpr VoiceConfig makePhaseMorph() noexcept
{
  return recipeVoice(VoiceRecipes::kPhaseMorph, kPhaseMorphLayout, 0.6f, 0.2f, 0.25f);
}
constexpr VoiceConfig makeSpectral() noexcept
{
  return recipeVoice(VoiceRecipes::kSpectralDsf, kSpectralLayout, 0.65f, 2.0f, 0.15f);
}
constexpr VoiceConfig makePrism() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kPrism, kPrismLayout, 0.3f, 0.6f, 0.12f);
  c.defaultAttack = 0.04f;
  c.defaultSustain = 0.7f;
  return c;
}
constexpr VoiceConfig makeChaosPrism() noexcept
{
  return recipeVoice(VoiceRecipes::kPrism, kChaosPrismLayout, 0.7f, 0.3f, 0.65f);
}
} // namespace VoicePresets
