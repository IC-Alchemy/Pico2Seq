#pragma once

#include "../VoiceParameters.h"
#include "../engines/RecipeSources.h"

namespace VoicePresets {
// Each patch owns the meaning, scaling, label and starting value of its three
// timbre lanes. Note/Velocity/Octave/GateLength/Slide and Gate stay shared.
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
inline constexpr auto kFmParameters = recipeLayout(
    {"Index", &VoiceConfig::macro1, 0.0f, 1.0f, dspmap::Mapping::EXP, VoiceParameterUnit::Percent, true},
    {"Ratio", &VoiceConfig::macro2, 0.5f, 8.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Ratio, true},
    {"Feedback", &VoiceConfig::macro3, 0.0f, 0.5f, dspmap::Mapping::EXP, VoiceParameterUnit::Percent, true});
inline constexpr auto kPhaseParameters = recipeLayout(
    {"Shape", &VoiceConfig::macro1, 0.0f, 1.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true},
    {"Skew", &VoiceConfig::macro2, -1.0f, 1.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true},
    {"Blend", &VoiceConfig::macro3, 0.0f, 1.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true});
inline constexpr auto kDsfParameters = recipeLayout(
    {"Bright", &VoiceConfig::macro1, 0.0f, 0.9f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true},
    {"Spacing", &VoiceConfig::macro2, 0.5f, 8.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Ratio, true},
    {"Sub", &VoiceConfig::macro3, 0.0f, 1.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true});
inline constexpr auto kPrismParameters = recipeLayout(
    {"Focus", &VoiceConfig::macro1, 0.0f, 1.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true},
    {"Spread", &VoiceConfig::macro2, 0.0f, 1.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true},
    {"Drift", &VoiceConfig::macro3, 0.0f, 1.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true});

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
  auto c = recipeVoice(VoiceRecipes::kFeedbackFm, kFmParameters, 0.32f, 3.5f, 0.08f);
  c.defaultSustain = 0.12f;
  c.defaultRelease = 0.5f;
  return c;
}
constexpr VoiceConfig makeFmBass() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kFeedbackFm, kFmParameters, 0.18f, 1.0f, 0.2f);
  c.defaultDecay = 0.16f;
  c.defaultRelease = 0.08f;
  return c;
}
constexpr VoiceConfig makePhaseMorph() noexcept
{
  return recipeVoice(VoiceRecipes::kPhaseMorph, kPhaseParameters, 0.6f, 0.2f, 0.25f);
}
constexpr VoiceConfig makeSpectral() noexcept
{
  return recipeVoice(VoiceRecipes::kSpectralDsf, kDsfParameters, 0.65f, 2.0f, 0.15f);
}
constexpr VoiceConfig makePrism() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kPrism, kPrismParameters, 0.3f, 0.6f, 0.12f);
  c.defaultAttack = 0.04f;
  c.defaultSustain = 0.7f;
  return c;
}
constexpr VoiceConfig makeChaosPrism() noexcept
{
  return recipeVoice(VoiceRecipes::kPrism, kPrismParameters, 0.7f, 0.3f, 0.65f);
}
} // namespace VoicePresets
