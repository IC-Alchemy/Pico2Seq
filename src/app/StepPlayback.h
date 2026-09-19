#pragma once
#include <cstdint>
class Sequencer;

// Core 0 thread context only: no sequencer or voice mutation in the clock ISR.
void processSequencerStep(uint32_t clockStep);
void updateParametersForStep(uint8_t stepToUpdate);
// Compatibility entry point for an already-valid normalized recording sample.
void updateParametersForStepNormalized(uint8_t stepToUpdate, float normalizedValue);
void updateActiveVoiceState(uint8_t stepIndex, Sequencer &activeSeq);
