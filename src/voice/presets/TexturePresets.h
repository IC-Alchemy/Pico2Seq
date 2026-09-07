#pragma once

#include "../VoiceConfig.h"

namespace VoicePresets {
  constexpr VoiceConfig makeHypersaw() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_HYPERSAW;
    c.paramSet = PARAMSET_HYPERSAW;
    c.hypersawDetune = 0.2f;
    c.hypersawMix = 0.5f;

    c.filterRes = 0.35f;
    c.filterType = FILTER_SVF; // wide-open clean low-pass
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
