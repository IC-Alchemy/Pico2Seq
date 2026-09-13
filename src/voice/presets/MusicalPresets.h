#pragma once

#include "RecipePresets.h"

namespace VoicePresets {
inline constexpr auto kReedPipeParameters = recipeLayout(
    {"Formant", &VoiceConfig::macro1, 1.0f, 6.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Ratio, true},
    {"Bloom", &VoiceConfig::macro2, 0.0f, 1.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true},
    {"Body", &VoiceConfig::macro3, 0.1f, 0.8f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true});
inline constexpr auto kSilkPadParameters = recipeLayout(
    {"Silk", &VoiceConfig::macro1, 0.0f, 0.65f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true},
    {"Detune", &VoiceConfig::macro2, 0.0f, 1.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true},
    {"Blend", &VoiceConfig::macro3, 0.1f, 0.5f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true});
inline constexpr auto kHollowBellParameters = recipeLayout(
    {"Ratio", &VoiceConfig::macro1, 0.5f, 6.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Ratio, true},
    {"Edge", &VoiceConfig::macro2, 0.0f, 0.45f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true},
    {"Ring", &VoiceConfig::macro3, 0.0f, 0.85f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true});
inline constexpr auto kSyncLeadParameters = recipeLayout(
    {"Sync", &VoiceConfig::macro1, 1.0f, 5.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Ratio, true},
    {"Edge", &VoiceConfig::macro2, 0.0f, 0.65f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true},
    {"Bite", &VoiceConfig::macro3, 0.1f, 0.8f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true});
inline constexpr auto kOrbitPluckParameters = recipeLayout(
    {"Index", &VoiceConfig::macro1, 0.0f, 3.0f, dspmap::Mapping::EXP, VoiceParameterUnit::Percent, true},
    {"Ratio", &VoiceConfig::macro2, 0.5f, 4.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Ratio, true},
    {"Body", &VoiceConfig::macro3, 0.1f, 0.8f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true});
inline constexpr auto kAirChimeParameters = recipeLayout(
    {"Focus", &VoiceConfig::macro1, 0.0f, 0.8f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true},
    {"Spread", &VoiceConfig::macro2, 0.1f, 1.0f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true},
    {"OctMix", &VoiceConfig::macro3, 0.0f, 0.65f, dspmap::Mapping::LINEAR, VoiceParameterUnit::Percent, true});

constexpr VoiceConfig makeVelvetKeys() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kFeedbackFm, kFmParameters, 0.10f, 2.0f, 0.025f);
  c.defaultAttack = 0.006f;
  c.defaultDecay = 0.85f;
  c.defaultSustain = 0.16f;
  c.defaultRelease = 0.45f;
  c.outputLevel = 0.62f;
  return c;
}
constexpr VoiceConfig makeCopperBass() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kSpectralDsf, kDsfParameters, 0.42f, 1.0f, 0.32f);
  c.highPassFreq = 20.0f;
  c.defaultAttack = 0.004f;
  c.defaultDecay = 0.22f;
  c.defaultSustain = 0.38f;
  c.defaultRelease = 0.10f;
  c.outputLevel = 0.72f;
  return c;
}
constexpr VoiceConfig makeReedPipe() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kReedPipe, kReedPipeParameters, 3.0f, 0.74f, 0.32f);
  c.defaultAttack = 0.025f;
  c.defaultDecay = 0.20f;
  c.defaultSustain = 0.75f;
  c.defaultRelease = 0.16f;
  c.outputLevel = 0.72f;
  return c;
}
constexpr VoiceConfig makeSilkPad() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kSilkPad, kSilkPadParameters, 0.22f, 0.33f, 0.5f);
  c.defaultAttack = 0.35f;
  c.defaultDecay = 0.75f;
  c.defaultSustain = 0.82f;
  c.defaultRelease = 1.25f;
  c.recipeRetrigger = false; // Leave oscillator phases running across pad gates.
  c.outputLevel = 0.55f;
  return c;
}
constexpr VoiceConfig makeHollowBell() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kHollowBell, kHollowBellParameters, 2.0f, 0.06f, 0.68f);
  c.defaultAttack = 0.002f;
  c.defaultDecay = 1.4f;
  c.defaultSustain = 0.0f;
  c.defaultRelease = 0.8f;
  c.outputLevel = 0.72f;
  return c;
}
constexpr VoiceConfig makeSyncLead() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kSyncLead, kSyncLeadParameters, 2.0f, 0.30f, 0.45f);
  c.defaultAttack = 0.009f;
  c.defaultDecay = 0.18f;
  c.defaultSustain = 0.68f;
  c.defaultRelease = 0.12f;
  c.outputLevel = 0.56f;
  return c;
}
constexpr VoiceConfig makeOrbitPluck() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kOrbitPluck, kOrbitPluckParameters, 1.4f, 2.0f, 0.28f);
  c.defaultAttack = 0.003f;
  c.defaultDecay = 0.38f;
  c.defaultSustain = 0.0f;
  c.defaultRelease = 0.24f;
  c.outputLevel = 0.64f;
  return c;
}
constexpr VoiceConfig makeAirChime() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kAirChime, kAirChimeParameters, 0.25f, 0.65f, 0.24f);
  c.defaultAttack = 0.012f;
  c.defaultDecay = 1.1f;
  c.defaultSustain = 0.08f;
  c.defaultRelease = 0.85f;
  c.outputLevel = 0.68f;
  return c;
}
} // namespace VoicePresets
