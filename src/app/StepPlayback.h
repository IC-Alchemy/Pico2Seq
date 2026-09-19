#pragma once
#include <cstdint>
#include "../pico2seq-core/sequencer/SequencerDefs.h"
class Sequencer;

// Core 0 thread context only: no sequencer or voice mutation in the clock ISR.
void processSequencerStep(uint32_t clockStep);
// Shared recording entry point for the lidar and armed Alchemy faders: writes
// a normalized value into the selected voice's lane (the selected step in
// Step Edit, else the playing step) and refreshes the sounding voice.
// Returns true when the stored value changed.
bool recordParameter(ParamId id, float normalizedValue);
void updateActiveVoiceState(uint8_t stepIndex, Sequencer &activeSeq);
