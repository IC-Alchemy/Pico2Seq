#pragma once

#include "../VoiceConfig.h"
#include "../VoiceParameters.h"

namespace VoicePresets {
  // Owned string lanes, centered on each preset's resting string. T60 is a
  // seconds control on an octave taper (banjo..harp character per span);
  // brightness and pick hardness stay linear and keep off the dead ends.
  constexpr VoiceParameterLayout stringLayout(VoiceParameters::Span t60, VoiceParameters::Span bright,
                                              VoiceParameters::Span pick) noexcept
  {
    using VoiceParameters::spanned;
    VoiceParameterLayout p = VoiceParameters::waveguideLayout();
    auto &decay = p.slots[static_cast<size_t>(ParamId::Decay)];
    auto &filter = p.slots[static_cast<size_t>(ParamId::Filter)];
    auto &attack = p.slots[static_cast<size_t>(ParamId::Attack)];
    decay = spanned(decay, t60, dspmap::Mapping::OCTAVE);
    filter = spanned(filter, bright, dspmap::Mapping::LINEAR);
    attack = spanned(attack, pick, dspmap::Mapping::LINEAR);
    return p;
  }
  //                                               T60 (s)              Bright               Pick
  inline constexpr auto kWgPluckLayout = stringLayout({0.15f, 1.8f, 4.0f}, {0.2f, 0.78f, 0.95f}, {0.2f, 0.85f, 1.0f});
  inline constexpr auto kWgNylonLayout = stringLayout({0.6f, 3.2f, 8.0f}, {0.05f, 0.28f, 0.6f}, {0.05f, 0.22f, 0.6f});
  inline constexpr auto kWgBellLayout = stringLayout({0.25f, 1.4f, 3.0f}, {0.55f, 0.9f, 0.98f}, {0.7f, 1.0f, 1.0f});
  inline constexpr auto kWgShimmerLayout = stringLayout({1.5f, 6.5f, 10.0f}, {0.3f, 0.55f, 0.8f}, {0.3f, 0.6f, 0.9f});

  constexpr VoiceConfig makeWaveguidePluck() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_WAVEGUIDE;
    c.paramSet = PARAMSET_WAVEGUIDE;
    c.parameters = &kWgPluckLayout;
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
    c.parameters = &kWgNylonLayout;
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
    c.parameters = &kWgBellLayout;
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
    c.parameters = &kWgShimmerLayout;
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
