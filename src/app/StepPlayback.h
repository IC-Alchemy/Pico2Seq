#pragma once
#include <cstdint>
#include "../pico2seq-core/sequencer/SequencerDefs.h"
class Sequencer;

// Core 0 thread context only: no sequencer or voice mutation in the clock ISR.
void processSequencerStep(uint32_t clockStep);
// Shared recording entry point for the lidar and the ENV-mode faders: writes
// a normalized value into the selected voice's lane (the selected step in
// Step Edit, else the playing step) and refreshes the sounding voice.
// Returns true when the stored value changed.
bool recordParameter(ParamId id, float normalizedValue);
// Step Edit only: the selected step's lane follows the patch again.
// Returns true when the step had its own value.
bool resetStepToPatch(ParamId id);
void updateActiveVoiceState(uint8_t stepIndex, Sequencer &activeSeq);
