#pragma once

#include "VoiceClass.h"
#include <cstdint>

namespace VoicePresets {
  // Factory functions for common synthesizer voice types
  VoiceConfig getAnalogVoice();
  VoiceConfig getDigitalVoice();
  VoiceConfig getBassVoice();
  VoiceConfig getLeadVoice();
  VoiceConfig getSquareVoice();
  VoiceConfig getPadVoice();
  VoiceConfig getPercussionVoice();

  // Preset utilities
  const char* getPresetName(uint8_t presetIndex);
  VoiceConfig getPresetConfig(uint8_t presetIndex);
  uint8_t getPresetCount();
}