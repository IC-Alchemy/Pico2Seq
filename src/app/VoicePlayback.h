#pragma once

#include "../pico2seq-core/sequencer/SequencerDefs.h"

// VoicePlayback: the Core 0 → Core 1 handoff for sounding notes.
// Musical role: every gate on/off and tweak reaches audio without clicks or stuck
// notes. Retain a steady state, send one queued update; retrigger is an event that
// must never be retained for later edits.
void publishVoiceState(uint8_t voiceIndex, const VoiceState &state);

// Note length lives in the sequencer; expiry publishes like a step so gates end on time.
void tickSequencerVoices();
void stopSequencerVoice(uint8_t voiceIndex);
