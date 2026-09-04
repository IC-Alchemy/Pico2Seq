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

  // Which sequencer parameter set a preset exposes. Out-of-range or unknown
  // indexes fall back to PARAMSET_STANDARD.
  VoiceParamSet getPresetParamSet(uint8_t presetIndex) noexcept;

  // Display name for a sequencer slot under the preset's param set, or
  // nullptr when the slot keeps its standard meaning (caller falls back to
  // paramName()). Names are short enough for the OLED's size-2 text.
  const char* getSequencerParamName(uint8_t presetIndex, ParamId id) noexcept;

  // Settings-mode preset pads start at index 8; returns the preset index for
  // a matrix pad, or -1 when the pad is out of the preset range.
  int presetIndexForPad(uint8_t padIndex, uint8_t presetCount) noexcept;
}