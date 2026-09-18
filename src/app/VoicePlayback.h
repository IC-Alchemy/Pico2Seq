#pragma once

#include "../pico2seq-core/sequencer/SequencerDefs.h"

// Core 0 only. Retain the control snapshot and send exactly one update to the
// audio queue. Retrigger is an event: publish it, but never retain it for edits.
void publishVoiceState(uint8_t voiceIndex, const VoiceState &state);

// Sequencer owns note duration. Expiry uses the same publishing path as steps.
void tickSequencerVoices();
void stopSequencerVoice(uint8_t voiceIndex);
