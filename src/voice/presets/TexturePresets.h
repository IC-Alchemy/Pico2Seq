#pragma once

#include "../VoiceConfig.h"
#include "../VoiceParameters.h"

namespace VoicePresets {
// Hypersaw = seven detuned saws for wide trance stacks (detune thickens,
// mix balances width against center); NoiseStorm = diffused noise through a
// resonant swarm for hats-to-wind textures (regen howls near the top, kept
// safe by the loop governor).
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
    p.cutoffMinimum = 80.0f; // supersaw register
    p.cutoffMaximum = 4000.0f;
    p.cutoffCurve = dspmap::Mapping::OCTAVE;
    p.cutoffCenter = 400.0f;
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

    c.filterRes = 0.35f;
    c.filterType = FILTER_SVF; // wide-open clean low-pass
    c.filterCutoffBase = 0.5f; // rests on the 3200 Hz lane center
    c.highPassFreq = 180.0f;
    c.filterMode = VoiceFilterMode::LP24; // SVF response: low-pass

    c.hasOverdrive = false;

    c.defaultAttack = 0.012f;
    c.defaultDecay = 0.3f;
    c.defaultSustain = 0.8f;
    c.defaultRelease = 0.25f;
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

    c.filterRes = 0.72f;      // resonant filter pings with the env
    c.filterType = FILTER_SVF;
    c.highPassFreq = 220.0f;
    c.filterMode = VoiceFilterMode::LP24; // SVF response: low-pass

    c.hasOverdrive = true;
    c.overdriveGain = 0.8f;
    c.overdriveDrive = 0.4f;

    c.defaultAttack = 0.003f;
    c.defaultDecay = 0.5f;
    c.defaultSustain = 0.55f;
    c.defaultRelease = 0.45f;
    c.outputLevel = 0.45f;    // diffuse + swarm can sum hot
    return c;
  }
} // namespace VoicePresets
