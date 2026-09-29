#pragma once

#include "RecipePresets.h"

namespace VoicePresets {
// Pitched recipe voices: reed pipe (vowel-like formant over a breathy body),
// silk pad (slow-blooming detuned pair, phases left running across gates),
// hollow bell (fast strike, ring-mod edge, no sustain), sync lead (aggressive
// synced bite for mono lines), orbit pluck (FM snap into clean body),
// air chime (harmonic shimmer + octave), velvet keys / copper bass (FM/DSF
// low-end with soft attacks).
// Reed pipe: the formant ratio walks the vowel set on an octave taper.
constexpr VoiceParameterLayout reedPipeLayout()
{
  using M = dspmap::Mapping;
  using U = VoiceParameterUnit;
  return recipeLayout(macroLane("Formant", &VoiceConfig::macro1, {1.0f, 3.0f, 6.0f}, M::OCTAVE, U::Ratio),
                      macroLane("Bloom", &VoiceConfig::macro2, {0.0f, 0.7f, 1.0f}, M::LINEAR, U::Percent),
                      macroLane("Body", &VoiceConfig::macro3, {0.1f, 0.4f, 0.8f}, M::LINEAR, U::Percent));
}
// Silk pad: Blend reaches 0.6 so the resting 0.5 is no longer glued to the top.
constexpr VoiceParameterLayout silkPadLayout()
{
  using M = dspmap::Mapping;
  using U = VoiceParameterUnit;
  return recipeLayout(macroLane("Silk", &VoiceConfig::macro1, {0.0f, 0.25f, 0.65f}, M::LINEAR, U::Percent),
                      macroLane("Detune", &VoiceConfig::macro2, {0.0f, 0.35f, 1.0f}, M::LINEAR, U::Percent),
                      macroLane("Blend", &VoiceConfig::macro3, {0.1f, 0.35f, 0.6f}, M::LINEAR, U::Percent));
}
// Hollow bell: the 0.15 Edge center lifts the resting 0.06 off the lane bottom.
constexpr VoiceParameterLayout hollowBellLayout()
{
  using M = dspmap::Mapping;
  using U = VoiceParameterUnit;
  return recipeLayout(macroLane("Ratio", &VoiceConfig::macro1, {0.5f, 2.0f, 6.0f}, M::OCTAVE, U::Ratio),
                      macroLane("Edge", &VoiceConfig::macro2, {0.0f, 0.15f, 0.45f}, M::LINEAR, U::Percent),
                      macroLane("Ring", &VoiceConfig::macro3, {0.0f, 0.5f, 0.85f}, M::LINEAR, U::Percent));
}
constexpr VoiceParameterLayout syncLeadLayout()
{
  using M = dspmap::Mapping;
  using U = VoiceParameterUnit;
  return recipeLayout(macroLane("Sync", &VoiceConfig::macro1, {1.0f, 2.0f, 5.0f}, M::OCTAVE, U::Ratio),
                      macroLane("Edge", &VoiceConfig::macro2, {0.0f, 0.25f, 0.65f}, M::LINEAR, U::Percent),
                      macroLane("Bite", &VoiceConfig::macro3, {0.1f, 0.45f, 0.8f}, M::LINEAR, U::Percent));
}
constexpr VoiceParameterLayout orbitPluckLayout()
{
  using M = dspmap::Mapping;
  using U = VoiceParameterUnit;
  return recipeLayout(macroLane("Index", &VoiceConfig::macro1, {0.0f, 1.2f, 3.0f}, M::EXP, U::Percent),
                      macroLane("Ratio", &VoiceConfig::macro2, {0.5f, 2.0f, 4.0f}, M::LINEAR, U::Ratio),
                      macroLane("Body", &VoiceConfig::macro3, {0.1f, 0.4f, 0.8f}, M::LINEAR, U::Percent));
}
constexpr VoiceParameterLayout airChimeLayout()
{
  using M = dspmap::Mapping;
  using U = VoiceParameterUnit;
  return recipeLayout(macroLane("Focus", &VoiceConfig::macro1, {0.0f, 0.3f, 0.8f}, M::LINEAR, U::Percent),
                      macroLane("Spread", &VoiceConfig::macro2, {0.1f, 0.55f, 1.0f}, M::LINEAR, U::Percent),
                      macroLane("OctMix", &VoiceConfig::macro3, {0.0f, 0.25f, 0.65f}, M::LINEAR, U::Percent));
}
inline constexpr auto kVelvetKeysLayout = fmLayout();
inline constexpr auto kCopperBassLayout = dsfLayout();
inline constexpr auto kReedPipeLayout = reedPipeLayout();
inline constexpr auto kSilkPadLayout = silkPadLayout();
inline constexpr auto kHollowBellLayout = hollowBellLayout();
inline constexpr auto kSyncLeadLayout = syncLeadLayout();
inline constexpr auto kOrbitPluckLayout = orbitPluckLayout();
inline constexpr auto kAirChimeLayout = airChimeLayout();

constexpr VoiceConfig makeVelvetKeys() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kFeedbackFm, kVelvetKeysLayout, 0.10f, 2.0f, 0.025f);
  c.defaultAttack = 0.006f;
  c.defaultDecay = 0.85f;
  c.defaultSustain = 0.16f;
  c.defaultRelease = 0.45f;
  c.outputLevel = 0.62f;
  return c;
}
constexpr VoiceConfig makeCopperBass() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kSpectralDsf, kCopperBassLayout, 0.42f, 1.0f, 0.32f);
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
  auto c = recipeVoice(VoiceRecipes::kReedPipe, kReedPipeLayout, 3.0f, 0.74f, 0.32f);
  c.defaultAttack = 0.025f;
  c.defaultDecay = 0.20f;
  c.defaultSustain = 0.75f;
  c.defaultRelease = 0.16f;
  c.outputLevel = 0.72f;
  return c;
}
constexpr VoiceConfig makeSilkPad() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kSilkPad, kSilkPadLayout, 0.22f, 0.33f, 0.5f);
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
  auto c = recipeVoice(VoiceRecipes::kHollowBell, kHollowBellLayout, 2.0f, 0.06f, 0.68f);
  c.defaultAttack = 0.002f;
  c.defaultDecay = 1.4f;
  c.defaultSustain = 0.0f;
  c.defaultRelease = 0.8f;
  c.outputLevel = 0.72f;
  return c;
}
constexpr VoiceConfig makeSyncLead() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kSyncLead, kSyncLeadLayout, 2.0f, 0.30f, 0.45f);
  c.defaultAttack = 0.009f;
  c.defaultDecay = 0.18f;
  c.defaultSustain = 0.68f;
  c.defaultRelease = 0.12f;
  c.outputLevel = 0.56f;
  return c;
}
constexpr VoiceConfig makeOrbitPluck() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kOrbitPluck, kOrbitPluckLayout, 1.4f, 2.0f, 0.28f);
  c.defaultAttack = 0.003f;
  c.defaultDecay = 0.38f;
  c.defaultSustain = 0.0f;
  c.defaultRelease = 0.24f;
  c.outputLevel = 0.64f;
  return c;
}
constexpr VoiceConfig makeAirChime() noexcept
{
  auto c = recipeVoice(VoiceRecipes::kAirChime, kAirChimeLayout, 0.25f, 0.65f, 0.24f);
  c.defaultAttack = 0.012f;
  c.defaultDecay = 1.1f;
  c.defaultSustain = 0.08f;
  c.defaultRelease = 0.85f;
  c.outputLevel = 0.68f;
  return c;
}
} // namespace VoicePresets
