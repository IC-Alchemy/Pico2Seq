#pragma once
#include <cstdint>

// Core 0 only. Build the voices once, before the clock starts.
void initializeVoices();
// Retained entry point used by the settings UI; indices are zero-based.
void applyVoicePreset(uint8_t voiceIndex, uint8_t presetIndex);
