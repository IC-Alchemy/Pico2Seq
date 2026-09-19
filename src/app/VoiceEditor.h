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
// Param-mode fader position (0-1) for a free fader: sets that lane's base for
// the selected voice and selects it as the encoder target.
void fader(ParamId lane, float position);
// Discards pending encoder motion (driver ticks and accumulated increments).
void clearEncoder();
void publish(uint8_t voiceIndex, VoiceConfig &config);
VoiceEdit::Id encoderTarget();
} // namespace VoiceEditor
