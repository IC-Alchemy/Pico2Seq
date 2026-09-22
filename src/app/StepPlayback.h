#pragma once
#include <cstdint>
#include "../pico2seq-core/sequencer/SequencerDefs.h"
class Sequencer;

// StepPlayback: what each 16th-note tick does to the four voices.
// Musical role: turns clock steps + hand height into sounding notes, and lets the
// hand or faders overwrite the playing step live. Core 0 thread context only —
// the clock ISR only stages steps, never mutates sequencers or voices.
void processSequencerStep(uint32_t clockStep);
// Shared write entry for hand + ENV faders: normalized height into the lane's
// selected step (Step Edit) or playing step (live), then refresh the voice.
// Returns true when the stored value changed.
bool recordParameter(ParamId id, float normalizedValue);
// Step Edit only: drop the step's own value so it follows the patch again.
// Returns true when the step had its own value.
bool resetStepToPatch(ParamId id);
// Detailed Core-0 sequencer telemetry. Runtime-toggle with 'D' over USB serial.
bool sequencerTraceEnabled() noexcept;
void setSequencerTraceEnabled(bool enabled) noexcept;
void updateActiveVoiceState(uint8_t stepIndex, Sequencer &activeSeq);
