#pragma once

// VoicePublication: header-only publish helpers shared by app tests and firmware.
// Musical role: same as VoicePlayback — one queued update per gesture, retrigger
// as a one-shot event. Control thread only; state may alias the stored copy.

#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../voice/VoiceManager.h"
#include "../voice/VoiceSystem.h"

// Control-thread publication only. Queue once, then retain a steady state.
// state may alias the stored VoiceSystem state.
inline bool publishVoiceState(VoiceSystem &voices, VoiceManager &manager,
                              uint8_t voiceIndex, const VoiceState &state)
{
    if (voiceIndex >= VoiceSystem::MAX_VOICES ||
        !manager.updateVoiceState(voices.getVoiceId(voiceIndex), state))
        return false;

    voices.getVoiceState(voiceIndex) = state;
    voices.getVoiceState(voiceIndex).shouldRetrigger = false;
    return true;
}

// The sequencer owns the only note countdown; publish on the expiry tick so
// gates end on time, not on the next step.
inline void tickSequencerVoice(Sequencer &sequencer, VoiceSystem &voices,
                               VoiceManager &manager, uint8_t voiceIndex)
{
    if (voiceIndex >= VoiceSystem::MAX_VOICES) return;
    auto &state = voices.getVoiceState(voiceIndex);
    if (sequencer.tickNoteDuration(&state))
        publishVoiceState(voices, manager, voiceIndex, state);
}

inline void stopSequencerVoice(Sequencer &sequencer, VoiceSystem &voices,
                               VoiceManager &manager, uint8_t voiceIndex)
{
    if (voiceIndex >= VoiceSystem::MAX_VOICES) return;
    sequencer.stop();
    auto &state = voices.getVoiceState(voiceIndex);
    // End the sequencer note and its audio gate together, so a restart never
    // slides in from a stale note. Patterns and cursors are kept.
    sequencer.handleNoteOff(&state);
    state.isGateHigh = false;
    state.shouldRetrigger = false;
    state.hasSlide = false;
    publishVoiceState(voices, manager, voiceIndex, state);
}
