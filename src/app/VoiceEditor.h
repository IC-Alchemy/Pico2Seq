#pragma once
#include "../voice/VoiceEditParameters.h"
#include <cstdint>
namespace VoiceEditor {
void enter();
void exit();
void buttons(uint8_t buttonLevels, uint8_t voiceLevels, uint32_t now);
// Encoder increment for the selected voice: edits the editor cursor while the
// editor is open, otherwise the performance encoder target's base.
void encoder(float delta);
// Discards pending encoder motion (driver ticks and accumulated increments).
void clearEncoder();
void publish(uint8_t voiceIndex, VoiceConfig &config);
VoiceEdit::Id encoderTarget();
} // namespace VoiceEditor
