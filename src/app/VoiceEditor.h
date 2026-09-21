#pragma once
#include "../voice/VoiceEditParameters.h"
#include <cstdint>
// VoiceEditor: the patch-tweaking mode (Shift + slider 4).
// Musical role: sculpts a voice's timbre while the groove parks; exiting resumes.
// Transport stops on entry so edits never fight the sequencer. Core 0 only.
namespace VoiceEditor {
void enter();
void exit();
void buttons(uint8_t buttonLevels, uint8_t voiceLevels, uint32_t now);
// Knob motion for the selected voice: editor cursor when open, else the
// performance target's patch base (heard at once, shown briefly on the OLED).
void encoder(float delta);
// Discard pending knob motion (driver ticks + accumulated increments).
void clearEncoder();
void publish(uint8_t voiceIndex, VoiceConfig &config);
VoiceEdit::Id encoderTarget();
} // namespace VoiceEditor
