#include "VoicePresets.h"

namespace VoicePresets {

  VoiceConfig getAnalogVoice()
  {
    VoiceConfig config;
    config.oscillatorCount = 3;
    config.oscWaveforms[0] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
    config.oscWaveforms[1] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
    config.oscWaveforms[2] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
    config.oscAmplitudes[0] = .4f;
    config.oscAmplitudes[1] = .2f;
    config.oscAmplitudes[2] = .2f;
    config.oscDetuning[0] = 0.0f;
    config.oscDetuning[1] = 0.045f;   // Slight detune
    config.oscDetuning[2] = -0.04f; // Slight detune opposite
    config.harmony[0] = 0;          // Root note
    config.harmony[1] = 0;          // Unison (no harmony)
    config.harmony[2] = 0;          // Unison (no harmony)

    config.filterRes = 0.33f;
    config.filterDrive = 3.1f;
    config.filterMode = daisysp::LadderFilter::FilterMode::LP24;
    config.filterPassbandGain = 0.23f;
    config.highPassFreq = 150.0f;

    config.hasOverdrive = false;
    config.hasWavefolder = false;
    config.overdriveGain = 0.5f;
    config.overdriveDrive = 0.35f;
    config.wavefolderGain = 4.5f;
    config.wavefolderOffset = 1.3f;

    config.defaultAttack = 0.04f;
    config.defaultDecay = 0.14f;
    config.defaultSustain = 0.45f;
    config.defaultRelease = 0.1f;
    config.outputLevel = 0.9f;   

    return config;
  }

  VoiceConfig getDigitalVoice()
  {
    VoiceConfig config;
    config.oscillatorCount = 1;
    config.oscWaveforms[0] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;

    config.oscAmplitudes[0] = 1.f;
    config.oscDetuning[0] = 0.0f; // Fixed duplicate assignment

    config.harmony[0] = 0;  // Root note
    config.filterRes = 0.2f;
    config.filterDrive = 1.5f;
    config.filterPassbandGain = 0.2f;
    config.highPassFreq = 150.0f;
    config.highPassRes = 0.15f;
    config.filterMode = daisysp::LadderFilter::FilterMode::LP24; // Low-pass filter

    config.hasOverdrive = false;
    config.hasWavefolder = false;
    config.overdriveGain = 0.5f;
    config.overdriveDrive = 0.31f;
    config.wavefolderGain = 4.0f;
    config.defaultAttack = 0.015f;
    config.defaultDecay = 0.1f;
    config.defaultSustain = 0.1f;
    config.defaultRelease = 0.1f;
    config.outputLevel = 0.9f;  
    return config;
  }

  VoiceConfig getBassVoice()
  {
    VoiceConfig config;
    config.oscillatorCount = 2;
    config.oscWaveforms[0] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
    config.oscWaveforms[1] = daisysp::Oscillator::WAVE_POLYBLEP_TRI;
    config.oscAmplitudes[0] = .3f;
    config.oscAmplitudes[1] = 1.f;
    config.oscDetuning[0] = 0.0f;
    config.oscDetuning[1] = -12.0f;
    config.harmony[0] = 0; // Root note
    config.harmony[1] = 0; // Unison (bass typically monophonic)
    config.highPassRes = 0.4f;
    config.filterRes = 0.33f;
    config.filterDrive = 2.5f;
    config.filterPassbandGain = 0.12f;
    config.highPassFreq = 85.0f; // Lower for bass
    config.filterMode = daisysp::LadderFilter::FilterMode::LP24;
    config.hasWavefolder = false;
    config.hasOverdrive = false;
    config.overdriveGain = 0.75f;
    config.overdriveDrive = 0.15f; // Subtle overdrive

    config.defaultAttack = 0.01f;
    config.defaultDecay = 0.3f;
    config.defaultSustain = 0.4f;
    config.defaultRelease = 0.2f;
    config.outputLevel = 1.f;    // Lower level for pad

    return config;
  }

  VoiceConfig getLeadVoice()
  {
    VoiceConfig config;
    config.oscillatorCount = 2;
    config.oscWaveforms[0] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
    config.oscWaveforms[1] = daisysp::Oscillator::WAVE_POLYBLEP_SQUARE;
    config.oscAmplitudes[0] = .6f;
    config.oscAmplitudes[1] = .4f;
    config.oscDetuning[0] = 0.0f;
    config.oscDetuning[1] = 12.0015f;
    config.harmony[0] = 0; // Root note
    config.harmony[1] = 0; // Unison (lead typically monophonic)
    config.oscPulseWidth[1] = 0.3f;

    config.filterRes = 0.23f;
    config.filterDrive = 3.f;
    config.filterPassbandGain = 0.23f;
    config.highPassFreq = 160.0f;
    config.filterMode = daisysp::LadderFilter::FilterMode::LP24;
    config.hasOverdrive = false;
    config.hasWavefolder = false;
    config.overdriveGain = 0.6f;
    config.overdriveDrive = 0.25f;
    config.wavefolderGain = 1.0f;

    config.defaultAttack = 0.02f;
    config.defaultDecay = 0.2f;
    config.defaultSustain = 0.35f;
    config.defaultRelease = 0.15f;
    config.outputLevel = 0.7f;   

    return config;
  }

  VoiceConfig getSquareVoice()
  {
    VoiceConfig config;
    config.oscillatorCount = 1;
    config.oscWaveforms[0] = daisysp::Oscillator::WAVE_POLYBLEP_SQUARE;
    config.oscAmplitudes[0] = .95f;
    config.oscDetuning[0] = 0.0f;

    config.harmony[0] = 7; // Root note
    config.oscPulseWidth[0] = 0.255f;

    config.filterRes = 0.55f;
    config.filterDrive = 2.8f;
    config.filterPassbandGain = 0.33f;
    config.highPassFreq = 150.0f;
    config.filterMode = daisysp::LadderFilter::FilterMode::LP24;
    config.hasOverdrive = false;
    config.hasWavefolder = false;
    config.overdriveGain = 0.6f;
    config.overdriveDrive = 0.25f;
    config.wavefolderGain = 3.0f;

    config.defaultAttack = 0.02f;
    config.defaultDecay = 0.2f;
    config.defaultSustain = 0.0f;
    config.defaultRelease = 0.15f;
    config.outputLevel = 0.8f;   

    return config;
  }

  VoiceConfig getPadVoice()
  {
    VoiceConfig config;
    config.oscillatorCount = 3;
    config.oscWaveforms[0] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
    config.oscWaveforms[1] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
    config.oscWaveforms[2] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
    config.oscAmplitudes[0] = 0.33f;
    config.oscAmplitudes[1] = 0.33f;
    config.oscAmplitudes[2] = 0.33f;
    config.harmony[0] = 0; // Root note
    config.harmony[1] = 4; // Perfect Fifth
    config.harmony[2] = 9; // Major Third One octave up

    config.filterRes = 0.1f;
    config.filterDrive = 1.f;
    config.filterPassbandGain = 0.23f;
    config.highPassFreq = 160.0f;
    config.highPassRes = 0.03f;
    config.filterMode = daisysp::LadderFilter::FilterMode::LP12;

    config.hasOverdrive = false;
    config.hasWavefolder = false;

    config.defaultAttack = 0.5f; // Slow attack for pad
    config.defaultDecay = 0.2f;
    config.defaultSustain = 0.5f;
    config.defaultRelease = .5f; // Long release
    config.outputLevel = 0.95f;    // Lower level for pad

    return config;
  }

  VoiceConfig getPercussionVoice()
  {
    VoiceConfig config;
    config.oscillatorCount = 0; // No oscillators, only noise
    config.oscWaveforms[0] = VoiceConfig::WAVE_NOISE; // Use noise for percussive texture

    config.oscAmplitudes[0] = 1.0f; // Full amplitude for noise
    config.oscAmplitudes[1] = 0.0f;
    config.oscAmplitudes[2] = 0.0f;
    config.oscDetuning[0] = 0.0f;
    config.oscDetuning[1] = 0.0f;
    config.oscDetuning[2] = 0.0f;
    config.harmony[0] = 0; // Root note
    config.harmony[1] = 0; // Percussion typically monophonic
    config.harmony[2] = 0; // Percussion typically monophonic
    config.filterMode = daisysp::LadderFilter::FilterMode::BP24; // Band-pass
    config.filterRes = 0.49f; // High resonance
    config.filterDrive = 3.0f;
    config.filterPassbandGain = 0.33f;
    config.highPassFreq = 222.0f; // High-pass for percussion

    config.hasOverdrive = true;
    config.hasWavefolder = true;
    config.overdriveGain = 0.25f;
    config.overdriveDrive = 0.3f;
    config.wavefolderGain = 3.0f;

    config.defaultAttack = 0.001f; // Very fast attack
    config.defaultDecay = 0.05f;   // Short decay
    config.defaultSustain = 0.0f;  // Low sustain
    config.defaultRelease = 0.1f;  // Short release

    return config;
  }

  // Preset name mapping for settings menu
  static const char* VOICE_PRESET_NAMES[] = {"Analog", "Digital", "Bass", "Lead", "Square", "Pad", "Percussion"};
  static const uint8_t VOICE_PRESET_COUNT = 7;

  const char* getPresetName(uint8_t presetIndex)
  {
    if (presetIndex < VOICE_PRESET_COUNT)
    {
      return VOICE_PRESET_NAMES[presetIndex];
    }
    return "Unknown";
  }

  VoiceConfig getPresetConfig(uint8_t presetIndex)
  {
    switch (presetIndex)
    {
      case 0: return getAnalogVoice();
      case 1: return getDigitalVoice();
      case 2: return getBassVoice();
      case 3: return getLeadVoice();
      case 4: return getSquareVoice();
      case 5: return getPadVoice();
      case 6: return getPercussionVoice();
      default: return getAnalogVoice();
    }
  }

  uint8_t getPresetCount() { return VOICE_PRESET_COUNT; }

} // namespace VoicePresets