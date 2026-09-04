#pragma once

#include "Voice.h"
#include <cstdint>

namespace VoicePresets {
  // Factory functions for common synthesizer voice types
  const VoiceConfig& getAnalogVoice() noexcept;
  const VoiceConfig& getDigitalVoice() noexcept;
  const VoiceConfig& getBassVoice() noexcept;
  const VoiceConfig& getLeadVoice() noexcept;
  const VoiceConfig& getSquareVoice() noexcept;
  const VoiceConfig& getPadVoice() noexcept;
  const VoiceConfig& getPercussionVoice() noexcept;

  // Extended factory functions (presets 8-15)
  const VoiceConfig& getSubFunkVoice() noexcept;
  const VoiceConfig& getRubberSubVoice() noexcept;
  const VoiceConfig& getWaveguidePluckVoice() noexcept;
  const VoiceConfig& getWaveguideNylonVoice() noexcept;
  const VoiceConfig& getWaveguideBellVoice() noexcept;
  const VoiceConfig& getWaveguideShimmerVoice() noexcept;
  const VoiceConfig& getHypersawVoice() noexcept;
  const VoiceConfig& getNoiseStormVoice() noexcept;

  // Preset utilities
  const char* getPresetName(uint8_t presetIndex) noexcept;
  const VoiceConfig& getPresetConfig(uint8_t presetIndex) noexcept;
  uint8_t getPresetCount() noexcept;

  // Sequencer-slot interop for non-standard param sets. Inverse of Voice's
  // waveguide T60 mapping (fmap EXP 0.05..10 s) so preset values can be
  // seeded into the re-purposed Decay track.
  float wgT60ToNormalized(float t60Seconds) noexcept;
}