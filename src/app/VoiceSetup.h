#pragma once
#include <cstdint>

// VoiceSetup: one-time voice construction before the downbeat.
// Musical role: builds the four starting instruments from saved preset picks.
// Core 0 only, before the clock starts and before voicesReady publishes.
void initializeVoices();
// Preset switch kept for the settings UI; index is zero-based (0-3).
void applyVoicePreset(uint8_t voiceIndex, uint8_t presetIndex);

// Applies a user preset (uploaded from the PC editor) to a voice; slot is
// persistence::userSlotIndex(page, pad). Staged exactly like a factory preset.
// False if the slot is empty or its record no longer validates (flash damage).
bool applyUserPreset(uint8_t voiceIndex, uint8_t slot);
