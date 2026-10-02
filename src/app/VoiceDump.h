#pragma once
// VoiceDump: prints every voice's current values to Serial (Shift + Theme in
// Utility mode). Musical role: a bench snapshot of what each voice is playing.
// Core 0 only, thread context; never called from the audio core.
void printAllVoiceValues();
