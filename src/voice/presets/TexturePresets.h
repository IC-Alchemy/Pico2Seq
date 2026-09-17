#pragma once

#include "../VoiceConfig.h"
#include "../VoiceParameters.h"

namespace VoicePresets {
  // Supersaw: detune's x^4 response puts 0.30 at ~8 cent thickening and 0.75
  // at ~43 cent strings; full smear (~185 cents) stays out of lane reach.
  // Mix clips the dry-saw and center-vanish extremes.
  constexpr VoiceParameterLayout hypersawSpotLayout() noexcept
  {
    using VoiceParameters::spanned;
    VoiceParameterLayout p = VoiceParameters::hypersawLayout();
    auto &detune = p.slots[static_cast<size_t>(ParamId::Attack)];
    auto &mix = p.slots[static_cast<size_t>(ParamId::Decay)];
    detune = spanned(detune, {0.0f, 0.30f, 0.75f}, dspmap::Mapping::LINEAR);
    mix = spanned(mix, {0.15f, 0.50f, 0.95f}, dspmap::Mapping::LINEAR);
    return p;
  }
  // Swarm: regen's top ~25% (1.0..1.2) is the deliberate howl/bloom zone the
  // governor keeps safe; chaos above 0.8 is already guttural.
  constexpr VoiceParameterLayout noiseStormSpotLayout() noexcept
  {
    using VoiceParameters::spanned;
    VoiceParameterLayout p = VoiceParameters::noiseStormLayout();
    auto &color = p.slots[static_cast<size_t>(ParamId::Filter)];
    auto &regen = p.slots[static_cast<size_t>(ParamId::Attack)];
    auto &chaos = p.slots[static_cast<size_t>(ParamId::Decay)];
    color = spanned(color, {0.0f, 0.60f, 1.0f}, dspmap::Mapping::LINEAR);
    regen = spanned(regen, {0.0f, 0.90f, 1.2f}, dspmap::Mapping::LINEAR);
    chaos = spanned(chaos, {0.0f, 0.40f, 0.8f}, dspmap::Mapping::LINEAR);
    return p;
  }
  inline constexpr auto kHypersawLayout = hypersawSpotLayout();
  inline constexpr auto kNoiseStormLayout = noiseStormSpotLayout();

  constexpr VoiceConfig makeHypersaw() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_HYPERSAW;
    c.paramSet = PARAMSET_HYPERSAW;
    c.parameters = &kHypersawLayout;
    c.hypersawDetune = 0.2f;
    c.hypersawMix = 0.5f;

    c.hasOverdrive = false;

    c.outputLevel = 0.5f;
    return c;
  }

  constexpr VoiceConfig makeNoiseStorm() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_NOISEFX;
    c.paramSet = PARAMSET_NOISESTORM;
    c.parameters = &kNoiseStormLayout;
    c.noiseDiffuseSize = 0.85f;
    c.noiseDiffuseMix = 0.65f;
    c.noiseSwarmColor = 0.6f;
    c.noiseSwarmRegen = 0.95f;
    c.noiseChaosLevel = 0.4f;

    c.hasOverdrive = true;
    c.overdriveGain = 0.8f;
    c.overdriveDrive = 0.4f;

    c.outputLevel = 0.45f;    // diffuse + swarm can sum hot
    return c;
  }
} // namespace VoicePresets
