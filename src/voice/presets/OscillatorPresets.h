#pragma once

#include "../VoiceConfig.h"

namespace VoicePresets {
  constexpr VoiceConfig makeAnalog() noexcept
  {
    VoiceConfig c{};
    // A single hard-sync pair supplies both the analog-style saw body and a
    // dedicated slave pitch that can be sequenced independently.
    c.oscillatorCount = 1;
    c.oscWaveforms[0] = WAVE_HARDSYNC_SAW;
    c.oscAmplitudes[0] = 1.0f;
    c.oscDetuning[0] = 0.0f;
    c.harmony[0] = 0;          // Root note
    c.paramSet = PARAMSET_HARDSYNC;

    c.filterRes = 0.33f;
    c.filterDrive = 2.1f;
    // Ladder on purpose: this is one of only two presets still using it
    // (with Lead); the test suite pins that count.
    c.filterMode = VoiceFilterMode::LP24;
    c.filterPassbandGain = 0.23f;
    c.highPassFreq = 120.0f;

    c.hasOverdrive = false;
    c.overdriveGain = 0.8f;
    c.overdriveDrive = 0.25f;

    c.defaultAttack = 0.07f;
    c.defaultDecay = 0.24f;
    c.defaultSustain = 0.5f;
    c.defaultRelease = 0.16f;
    c.outputLevel = 0.5f;
    return c;
  }

  constexpr VoiceConfig makeDigital() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 2;
    c.oscWaveforms[0] = WAVE_BSP_SQUARE;
    c.oscWaveforms[1] = // Naive triangle: continuous waveform, band-limited enough without splines
        WAVE_BSP_SQUARE;

    c.oscAmplitudes[0] = .75f;
    c.oscAmplitudes[1] = .65f;
    c.oscDetuning[0] = 0.0f; // Fixed duplicate assignment
    c.oscDetuning[1] = 0.01f;  // Fixed duplicate assignment

    c.harmony[0] = 0; // Root note
    c.filterRes = 0.4f;
    c.filterType = FILTER_SVF; // clean resonant low-pass replaces the ladder
    c.highPassFreq = 111.0f;
    c.highPassRes = 0.15f;
    c.filterMode = VoiceFilterMode::LP12; // SVF response: low-pass

    c.hasOverdrive = false;
    c.overdriveGain = 0.7f;
    c.overdriveDrive = 0.51f;
    c.defaultAttack = 0.015f;
    c.defaultDecay = 0.1f;
    c.defaultSustain = 0.5f;
    c.defaultRelease = 0.15f;
    c.outputLevel = 0.5f;
    return c;
  }

  constexpr VoiceConfig makeBass() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 2;
    c.oscWaveforms[0] = WAVE_SIN;
    c.oscWaveforms[1] = // Naive triangle: continuous waveform, band-limited enough without splines
        WAVE_TRI;
    c.oscAmplitudes[0] = 1.f;
    c.oscAmplitudes[1] = 1.f;
    c.oscDetuning[0] = -12.0f;
    c.oscDetuning[1] = 0.0f;
    c.harmony[0] = 0; // Root note
    c.harmony[1] = 0; // Unison (bass typically monophonic)
    c.highPassRes = 0.4f;
    c.filterRes = 0.45f; // SVF resonance carries the growl (no ladder drive)
    c.filterType = FILTER_SVF; // TPT state-variable low-pass: tight, stable bass
    c.highPassFreq = 45.0f; // Lower for bass
    c.filterMode = VoiceFilterMode::LP12; // SVF response: low-pass
    c.hasOverdrive = true;
    c.overdriveGain = 0.95f;
    c.overdriveDrive = 0.16f; // Subtle overdrive
    c.defaultAttack = 0.01f;
    c.defaultDecay = 0.3f;
    c.defaultSustain = 0.85f;
    c.defaultRelease = 0.2f;
    c.outputLevel = .85f;
    return c;
  }

  constexpr VoiceConfig makeLead() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 2;
    c.oscWaveforms[0] = WAVE_BSP_SAW;
    c.oscWaveforms[1] = WAVE_BSP_SAW;
    c.oscAmplitudes[0] = .6f;
    c.oscAmplitudes[1] = .4f;
    c.oscDetuning[0] = 0.0f;
    c.oscDetuning[1] = 0.00f;
    c.harmony[0] = 0; // Root note
    c.harmony[1] = 3; 

    c.filterRes = 0.4f;
    c.filterDrive = 3.f;
    c.filterPassbandGain = 0.23f;
    c.highPassFreq = 160.0f;
    // Ladder on purpose: this is one of only two presets still using it
    // (with Analog); the test suite pins that count.
    c.filterType = FILTER_LADDER; // Use ladder filter for lead
    c.filterMode = VoiceFilterMode::LP12;
    c.hasOverdrive = false;
    c.overdriveGain = 0.7f;
    c.overdriveDrive = 0.45f;

    c.defaultAttack = 0.02f;
    c.defaultDecay = 0.2f;
    c.defaultSustain = 0.5f;
    c.defaultRelease = 0.15f;
    c.outputLevel = 0.5f;
    return c;
  }

  constexpr VoiceConfig makeSquare() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 1;
    c.oscWaveforms[0] = WAVE_BSP_SQUARE;
    c.oscAmplitudes[0] = 1.f;
    c.harmony[0] = 0; // Root note
    c.oscPulseWidth[0] = 0.2f;

    c.filterRes = 0.6f; // 
    c.filterType = FILTER_SVF;
    c.highPassFreq = 150.0f;
    c.filterMode = VoiceFilterMode::BP24; // SVF response: band-pass
    c.hasOverdrive = false;
    c.overdriveGain = 0.75f;
    c.overdriveDrive = 0.35f;

    c.defaultAttack = 0.02f;
    c.defaultDecay = 0.4f;
    c.defaultSustain = 0.0f;
    c.defaultRelease = 0.25f;
    c.outputLevel = .56f;
    return c;
  }

  constexpr VoiceConfig makePad() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 3;
    c.oscWaveforms[0] = WAVE_BSP_SAW;
    c.oscWaveforms[1] = WAVE_BSP_SAW;
    c.oscWaveforms[2] = WAVE_BSP_SAW;
    c.oscAmplitudes[0] = 0.33f;
    c.oscAmplitudes[1] = 0.33f;
    c.oscAmplitudes[2] = 0.33f;
    c.harmony[0] = 0;  // Root note
    c.harmony[1] = 4; // Perfect Fifth
    c.harmony[2] = 9;  // Major Third

    c.filterRes = 0.3f;
    c.filterType = FILTER_SVF; // smooth state-variable low-pass
    c.highPassFreq = 140.0f;
    c.highPassRes = 0.08f;
    c.filterMode = VoiceFilterMode::LP12; // SVF response: low-pass

    c.hasOverdrive = false;
    c.overdriveGain = 0.85f;
    c.overdriveDrive = 0.25f;
    c.defaultAttack = 0.02f;   // Slow attack for pad
    c.defaultDecay = 0.2f;
    c.defaultSustain = 0.5f;
    c.defaultRelease = .5f;    // Long release
    c.outputLevel = 0.5f;      // Lower level for pad
    return c;
  }

  constexpr VoiceConfig makePercussion() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;           // No oscillators, only noise
    c.oscWaveforms[0] = WAVE_NOISE;  // Use noise for percussive texture
    c.oscAmplitudes[0] = 1.f;

    c.filterRes = 0.4f;
    c.filterType = FILTER_SVF;
    c.highPassFreq = 200.0f;
    c.filterMode = VoiceFilterMode::LP24; // SVF response: low-pass

    c.hasOverdrive = false;
    c.overdriveGain = 0.45f;
    c.overdriveDrive = 0.3f;

    c.defaultAttack = 0.005f;
    c.defaultDecay = 0.08f;
    c.defaultSustain = 0.0f;
    c.defaultRelease = 0.07f;
    c.outputLevel = 0.5f;
    return c;
  }

  constexpr VoiceConfig makeSubFunk() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 3;
    c.oscWaveforms[0] = WAVE_SIN;
    c.oscWaveforms[1] = WAVE_TRI;
    c.oscWaveforms[2] = WAVE_SIN;
    c.oscAmplitudes[0] = 1.0f;
    c.oscAmplitudes[1] = 0.8f;
    c.oscAmplitudes[2] = 0.65f;
    c.oscDetuning[0] = -12.0f; // sub octave
    c.oscDetuning[1] = -12.0f; // sub octave color
    c.oscDetuning[2] = 0.0f;   // fundamental body
    c.harmony[0] = 0;
    c.harmony[1] = 0;
    c.harmony[2] = 0;

    c.filterRes = 0.5f;
    c.filterType = FILTER_SVF; // resonant low-pass keeps the sub stable under env sweeps
    c.filterMode = VoiceFilterMode::LP12; // SVF response: low-pass
    c.highPassFreq = 55.0f; // keep the sub, shed the rumble

    c.hasOverdrive = true;
    c.overdriveGain = 0.9f;
    c.overdriveDrive = 0.35f; // warm grit

    c.defaultAttack = 0.004f;
    c.defaultDecay = 0.22f;
    c.defaultSustain = 0.35f;
    c.defaultRelease = 0.12f;
    c.outputLevel = 0.9f;
    return c;
  }

  constexpr VoiceConfig makeRubberSub() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 3;
    c.oscWaveforms[0] = WAVE_SIN;
    c.oscWaveforms[1] = WAVE_BSP_SQUARE;
    c.oscWaveforms[2] = WAVE_TRI;
    c.oscAmplitudes[0] = 0.9f;
    c.oscAmplitudes[1] = 0.3f;
    c.oscAmplitudes[2] = 0.5f;
    c.oscDetuning[0] = -12.0f; // sub octave
    c.oscDetuning[1] = -12.0f; // two octaves down: harmonic grit
    c.oscDetuning[2] = 0.0f;   // fundamental body
    c.harmony[0] = 0;
    c.harmony[1] = 0;
    c.harmony[2] = 0;

    c.filterRes = 0.7f; // SVF band-pass honk (2-pole: higher Q than the old BP24 ladder)
    c.filterType = FILTER_SVF;
    c.filterMode = VoiceFilterMode::BP24; // SVF response: band-pass
    c.highPassFreq = 70.0f;

    c.hasOverdrive = true;
    c.overdriveGain = 1.0f;
    c.overdriveDrive = 0.55f; // spit

    c.defaultAttack = 0.002f;
    c.defaultDecay = 0.16f;
    c.defaultSustain = 0.25f;
    c.defaultRelease = 0.09f;
    c.outputLevel = 0.85f;
    return c;
  }
} // namespace VoicePresets
