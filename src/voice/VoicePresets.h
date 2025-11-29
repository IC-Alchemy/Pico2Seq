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

  // Preset utilities
  const char* getPresetName(uint8_t presetIndex) noexcept;
  const VoiceConfig& getPresetConfig(uint8_t presetIndex) noexcept;
  uint8_t getPresetCount() noexcept;
  
  // Per-voice applied preset management (uiIndex = 0..3)
  void applyPresetToVoice(uint8_t uiIndex, uint8_t presetIndex);
  void setAppliedPresetForVoice(uint8_t uiIndex, int8_t presetIndex);
  int8_t getAppliedPresetForVoice(uint8_t uiIndex) noexcept;
}