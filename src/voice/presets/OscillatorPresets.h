#pragma once

#include "../VoiceConfig.h"
#include "../VoiceParameters.h"

// Eurorack oscillator presets: raw oscillator banks with no main filter, no
// amplitude envelope and no high-pass — voices drone and external modules
// shape tone and dynamics. Only waveform, blend, drive and level tuning
// remain per preset.
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

    c.hasOverdrive = false;
    c.overdriveGain = 0.8f;
    c.overdriveDrive = 0.25f;

    c.outputLevel = 0.5f;
    return c;
  }

  constexpr VoiceConfig makeDigital() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 2;
    c.oscWaveforms[0] = WAVE_BSP_SQUARE;
    c.oscWaveforms[1] = WAVE_BSP_SQUARE; // band-limited square pair, second one slightly detuned

    c.oscAmplitudes[0] = .75f;
    c.oscAmplitudes[1] = .65f;
    c.oscDetuning[0] = 0.0f;
    c.oscDetuning[1] = 0.01f;

    c.harmony[0] = 0; // Root note

    c.hasOverdrive = false;
    c.overdriveGain = 0.7f;
    c.overdriveDrive = 0.51f;
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
    c.hasOverdrive = true;
    c.overdriveGain = 0.95f;
    c.overdriveDrive = 0.16f; // Subtle overdrive
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

    c.hasOverdrive = false;
    c.overdriveGain = 0.7f;
    c.overdriveDrive = 0.45f;

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

    c.hasOverdrive = false;
    c.overdriveGain = 0.75f;
    c.overdriveDrive = 0.35f;

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

    c.hasOverdrive = false;
    c.overdriveGain = 0.85f;
    c.overdriveDrive = 0.25f;
    c.outputLevel = 0.5f;      // Lower level for pad
    return c;
  }

  constexpr VoiceConfig makePercussion() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;           // No oscillators, only noise
    c.oscWaveforms[0] = WAVE_NOISE;  // Use noise for percussive texture
    c.oscAmplitudes[0] = 1.f;

    c.hasOverdrive = false;
    c.overdriveGain = 0.45f;
    c.overdriveDrive = 0.3f;

    c.outputLevel = 0.5f;
    return c;
  }

  constexpr VoiceConfig makeSubFunk() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 3;
    c.oscWaveforms[0] = WAVE_SIN;
    c.oscWaveforms[1] = WAVE_BSP_SQUARE;
    c.oscWaveforms[2] = WAVE_SIN;
    c.oscAmplitudes[0] = 1.0f;
    c.oscAmplitudes[1] = 0.35f;
    c.oscAmplitudes[2] = 0.65f;
    c.oscDetuning[0] = -12.0f; // sub octave fundamental
    c.oscDetuning[1] = -12.0f; // sub octave odd harmonics for funk bite
    c.oscDetuning[2] = 0.0f;   // fundamental body
    c.harmony[0] = 0;
    c.harmony[1] = 0;
    c.harmony[2] = 0;

    c.hasOverdrive = true;
    c.overdriveGain = 0.9f;
    c.overdriveDrive = 0.45f; // warm grit

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

    c.hasOverdrive = true;
    c.overdriveGain = 1.0f;
    c.overdriveDrive = 0.55f; // spit

    c.outputLevel = 0.85f;
    return c;
  }
} // namespace VoicePresets
