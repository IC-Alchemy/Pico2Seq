#pragma once
#include "../voice/VoiceEditParameters.h"
#include <cstdint>
namespace VoiceEditor {
void enter();
void exit();
void buttons(uint8_t buttonLevels, uint8_t voiceLevels, uint32_t now);
void encoder(float delta);
void publish(uint8_t voiceIndex, VoiceConfig &config);
VoiceEdit::Id encoderTarget();
} // namespace VoiceEditor
