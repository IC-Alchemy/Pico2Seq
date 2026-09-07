#pragma once

#include "../VoiceConfig.h"

namespace VoicePresets {
  constexpr VoiceConfig makeWaveguidePluck() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_WAVEGUIDE;
    c.paramSet = PARAMSET_WAVEGUIDE;
    c.wgT60 = 1.8f;
    c.wgBrightness = 0.78f;
    c.wgPickPosition = 0.26f;
    c.wgPickHardness = 0.85f;
    c.wgStiffness = 0.0f;
    c.wgDetune = 4.0f;

    c.hasOverdrive = false;
    c.hasFilter = false;    // raw string; velocity scales the output directly
    c.hasEnvelope = false;  // natural T60 decay instead of a gated VCA
    c.highPassFreq = 55.0f;  // sub-shedding HPF: Karplus tails collect rumble
    c.highPassRes = 0.0f;
    c.outputLevel = 0.85f;  // two-string course sums hot
    return c;
  }

  constexpr VoiceConfig makeWaveguideNylon() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_WAVEGUIDE;
    c.paramSet = PARAMSET_WAVEGUIDE;
    c.wgT60 = 3.2f;
    c.wgBrightness = 0.28f;
    c.wgPickPosition = 0.42f;
    c.wgPickHardness = 0.22f;
    c.wgStiffness = 0.05f;
    c.wgDetune = 9.0f;

    c.hasOverdrive = false;
    c.hasFilter = false;
    c.hasEnvelope = false;
    c.highPassFreq = 66.0f;
    c.highPassRes = 0.0f;
    c.outputLevel = 0.9f;
    return c;
  }

  constexpr VoiceConfig makeWaveguideBell() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_WAVEGUIDE;
    c.paramSet = PARAMSET_WAVEGUIDE;
    c.wgT60 = 1.4f;
    c.wgBrightness = 0.9f;
    c.wgPickPosition = 0.08f; // bridge: thin and nasal
    c.wgPickHardness = 1.0f;
    c.wgStiffness = 0.88f;    // inharmonic dispersion
    c.wgDetune = 0.0f;

    c.hasOverdrive = false;
    c.hasFilter = false;
    c.hasEnvelope = false;
    c.highPassFreq = 0.0f;
    c.highPassRes = 0.0f;
    c.outputLevel = 0.75f;    // bright partials run hot
    return c;
  }

  constexpr VoiceConfig makeWaveguideShimmer() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_WAVEGUIDE;
    c.paramSet = PARAMSET_WAVEGUIDE;
    c.wgT60 = 6.5f;
    c.wgBrightness = 0.55f;
    c.wgPickPosition = 0.35f;
    c.wgPickHardness = 0.6f;
    c.wgStiffness = 0.15f;
    c.wgDetune = 26.0f;       // wide course: slow shimmer

    c.hasOverdrive = false;
    c.hasFilter = false;
    c.hasEnvelope = false;
    c.highPassFreq = 0.0f;
    c.highPassRes = 0.0f;
    c.outputLevel = 0.8f;
    return c;
  }
} // namespace VoicePresets
