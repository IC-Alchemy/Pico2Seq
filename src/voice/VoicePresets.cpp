#include "VoicePresets.h"
#include <array>

namespace VoicePresets
{

  // Thread-safe static storage for presets
  static const std::array<VoiceConfig, 7> &presetStorage() noexcept
  {
    static std::array<VoiceConfig, 7> presets = []() noexcept
    {
      std::array<VoiceConfig, 7> p{};
      // Analog
      {
        VoiceConfig &config = p[0];
        config.oscillatorCount = 3;
        config.oscWaveforms[0] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
        config.oscWaveforms[1] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
        config.oscWaveforms[2] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
        config.oscAmplitudes[0] = .5f;
        config.oscAmplitudes[1] = .25f;
        config.oscAmplitudes[2] = .25f;
        config.oscDetuning[0] = 0.0f;
        config.oscDetuning[1] = 0.08f;  // Slight detune
        config.oscDetuning[2] = -0.08f; // Slight detune opposite
        config.harmony[0] = 0;           // Root note
        config.harmony[1] = 0;           // Unison (no harmony)
        config.harmony[2] = 0;           // Unison (no harmony)

        config.filterRes = 0.33f;
        config.filterDrive = 3.1f;
        config.filterMode = daisysp::LadderFilter::FilterMode::LP24;
        config.filterPassbandGain = 0.23f;
        config.highPassFreq = 150.0f;

        config.hasOverdrive = false;
        config.hasWavefolder = false;
        config.overdriveGain = 0.8f;
        config.overdriveDrive = 0.25f;
        config.wavefolderGain = 5.5f;
        config.wavefolderOffset = 1.3f;

        config.defaultAttack = 0.04f;
        config.defaultDecay = 0.14f;
        config.defaultSustain = 0.3f;
        config.defaultRelease = 0.1f;
        config.outputLevel = 0.5f;
      }

      // Digital
      {
        VoiceConfig &config = p[1];
        config.oscillatorCount = 2;
        config.oscWaveforms[0] = daisysp::Oscillator::WAVE_POLYBLEP_SQUARE;
        config.oscWaveforms[1] = daisysp::Oscillator::WAVE_POLYBLEP_TRI;

        config.oscAmplitudes[0] = .75f;
        config.oscAmplitudes[1] = 1.f;
        config.oscDetuning[0] = 0.0f; // Fixed duplicate assignment
        config.oscDetuning[1] = 12.0f;  // Fixed duplicate assignment

        config.harmony[0] = 0; // Root note
        config.filterRes = 0.4f;
        config.filterDrive = 2.5f;
        config.filterPassbandGain = 0.25f;
        config.highPassFreq = 111.0f;
        config.highPassRes = 0.15f;
        config.filterMode = daisysp::LadderFilter::FilterMode::LP12; // Low-pass filter

        config.hasOverdrive = false;
        config.hasWavefolder = false;
        config.overdriveGain = 0.7f;
        config.overdriveDrive = 0.51f;
        config.wavefolderGain = 8.0f;
        config.defaultAttack = 0.015f;
        config.defaultDecay = 0.1f;
        config.defaultSustain = 0.5f;
        config.defaultRelease = 0.1f;
        config.outputLevel = 0.5f;
      }

      // Bass
      {
        VoiceConfig &config = p[2];
        config.oscillatorCount = 2;
        config.oscWaveforms[0] = daisysp::Oscillator::WAVE_CHEAP_SIN;
        config.oscWaveforms[1] = daisysp::Oscillator::WAVE_POLYBLEP_TRI;
        config.oscAmplitudes[0] = 1.f;
        config.oscAmplitudes[1] = 1.f;
        config.oscDetuning[0] = -12.0f;
        config.oscDetuning[1] = -12.0f;
        config.harmony[0] = 0; // Root note
        config.harmony[1] = 0; // Unison (bass typically monophonic)
        config.highPassRes = 0.4f;
        config.filterRes = 0.33f;
        config.filterDrive = 2.f;
        config.filterPassbandGain = 0.12f;
        config.highPassFreq = 85.0f; // Lower for bass
        config.filterMode = daisysp::LadderFilter::FilterMode::LP12;
        config.hasWavefolder = false;
        config.hasOverdrive = false;
        config.overdriveGain = 0.95f;
        config.overdriveDrive = 0.16f; // Subtle overdrive
        config.wavefolderGain = 9.0f;
        config.defaultAttack = 0.01f;
        config.defaultDecay = 0.3f;
        config.defaultSustain = 0.55f;
        config.defaultRelease = 0.2f;
        config.outputLevel = .95f;
      }

      // Lead
      {
        VoiceConfig &config = p[3];
        config.oscillatorCount = 2;
        config.oscWaveforms[0] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
        config.oscWaveforms[1] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
        config.oscAmplitudes[0] = .6f;
        config.oscAmplitudes[1] = .4f;
        config.oscDetuning[0] = 0.0f;
        config.oscDetuning[1] = 0.00f;
        config.harmony[0] = 0; // Root note
        config.harmony[1] = 3; // Unison (lead typically monophonic)
        // config.oscPulseWidth[1] = 0.36f;

        config.filterRes = 0.4f;
        config.filterDrive = 3.f;
        config.filterPassbandGain = 0.23f;
        config.highPassFreq = 160.0f;
        config.filterMode = daisysp::LadderFilter::FilterMode::LP12;
        config.hasOverdrive = false;
        config.hasWavefolder = false;
        config.overdriveGain = 0.7f;
        config.overdriveDrive = 0.45f;
        config.wavefolderGain = 9.0f;

        config.defaultAttack = 0.02f;
        config.defaultDecay = 0.2f;
        config.defaultSustain = 0.5f;
        config.defaultRelease = 0.15f;
        config.outputLevel = 0.5f;
      }

      // Square
      {
        VoiceConfig &config = p[4];
        config.oscillatorCount = 1;
        config.oscWaveforms[0] = daisysp::Oscillator::WAVE_POLYBLEP_SQUARE;
        config.oscAmplitudes[0] = 1.f;
        config.harmony[0] = 0; // Root note
        config.oscPulseWidth[0] = 0.2f;

        config.filterRes = 0.52f;
        config.filterDrive = 3.3f;
        config.filterPassbandGain = 0.33f;
        config.highPassFreq = 150.0f;
        config.filterMode = daisysp::LadderFilter::FilterMode::LP24;
        config.hasOverdrive = false;
        config.hasWavefolder = false;
        config.overdriveGain = 0.75f;
        config.overdriveDrive = 0.35f;
        config.wavefolderGain = 9.0f;
        config.wavefolderOffset = .4f;

        config.defaultAttack = 0.02f;
        config.defaultDecay = 0.2f;
        config.defaultSustain = 0.0f;
        config.defaultRelease = 0.15f;
        config.outputLevel = .5f;
      }

      // Pad
      {
        VoiceConfig &config = p[5];
        config.oscillatorCount = 3;
        config.oscWaveforms[0] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
        config.oscWaveforms[1] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
        config.oscWaveforms[2] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
        config.oscAmplitudes[0] = 0.33f;
        config.oscAmplitudes[1] = 0.33f;
        config.oscAmplitudes[2] = 0.33f;
        config.harmony[0] = 0;  // Root note
        config.harmony[1] = -3; // Perfect Fifth
        config.harmony[2] = 2;  // Major Third

        config.filterRes = 0.3f;
        config.filterDrive = 2.2f;
        config.filterPassbandGain = 0.23f;
        config.highPassFreq = 140.0f;
        config.highPassRes = 0.08f;
        config.filterMode = daisysp::LadderFilter::FilterMode::LP12;

        config.hasOverdrive = false;
        config.hasWavefolder = false;
        config.overdriveGain = 0.85f;
        config.overdriveDrive = 0.25f;
        config.wavefolderGain = 4.0f;
        config.wavefolderOffset = 4.0f;
        config.defaultAttack = 0.02f; // Slow attack for pad
        config.defaultDecay = 0.2f;
        config.defaultSustain = 0.5f;
        config.defaultRelease = .5f; // Long release
        config.outputLevel = 0.5f;  // Lower level for pad
      }

      // Percussion
      {
        VoiceConfig &config = p[6];
        config.oscillatorCount = 0;                       // No oscillators, only noise
        config.oscWaveforms[0] = VoiceConfig::WAVE_NOISE; // Use noise for percussive texture
        config.oscAmplitudes[0] = 1.f;

        config.filterRes = 0.4f;
        config.filterDrive = 2.3f;
        config.filterPassbandGain = 0.33f;
        config.highPassFreq = 200.0f;
        config.filterMode = daisysp::LadderFilter::FilterMode::LP24;

        config.hasOverdrive = false; 
        config.overdriveGain = 0.45f;
        config.overdriveDrive = 0.3f;
        config.hasWavefolder = false;
        config.wavefolderGain = 7.0f;

        config.defaultAttack = 0.005f;
        config.defaultDecay = 0.08f;
        config.defaultSustain = 0.0f;
        config.defaultRelease = 0.07f;
        config.outputLevel = 0.5f;
      }

      return p;
    }();
    return presets;
  }

  static constexpr const char *VOICE_PRESET_NAMES[] = {"Analog", "Digital", "Bass", "Lead", "Square", "Pad", "Percussion"};
  static constexpr uint8_t VOICE_PRESET_COUNT = 7;

  const VoiceConfig &getAnalogVoice() noexcept { return presetStorage()[0]; }
  const VoiceConfig &getDigitalVoice() noexcept { return presetStorage()[1]; }
  const VoiceConfig &getBassVoice() noexcept { return presetStorage()[2]; }
  const VoiceConfig &getLeadVoice() noexcept { return presetStorage()[3]; }
  const VoiceConfig &getSquareVoice() noexcept { return presetStorage()[4]; }
  const VoiceConfig &getPadVoice() noexcept { return presetStorage()[5]; }
  const VoiceConfig &getPercussionVoice() noexcept { return presetStorage()[6]; }

  const char *getPresetName(uint8_t presetIndex) noexcept
  {
    if (presetIndex < VOICE_PRESET_COUNT)
    {
      return VOICE_PRESET_NAMES[presetIndex];
    }
    return "Unknown";
  }

  const VoiceConfig &getPresetConfig(uint8_t presetIndex) noexcept
  {
    switch (presetIndex)
    {
    default:
    case 0:
      return getAnalogVoice();
    case 1:
      return getDigitalVoice();
    case 2:
      return getBassVoice();
    case 3:
      return getLeadVoice();
    case 4:
      return getSquareVoice();
    case 5:
      return getPadVoice();
    case 6:
      return getPercussionVoice();
    }
  }

  uint8_t getPresetCount() noexcept { return VOICE_PRESET_COUNT; }

} // namespace VoicePresets
