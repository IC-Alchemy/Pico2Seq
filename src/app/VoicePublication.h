#pragma once

#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../voice/VoiceManager.h"
#include "../voice/VoiceSystem.h"

// Control-thread publication only. Queue the event once, then retain a steady
// requested state: live refreshes and expiry must never replay a retrigger.
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

// Sequencer owns the only note-duration countdown. Publish on the expiry tick,
// not the next step boundary; all four voices follow the same rule.
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
    // End the sequencer note as well as its audio gate, so restart cannot slide
    // from a stale active note. Pattern data and parameter cursors are retained.
    sequencer.handleNoteOff(&state);
    state.isGateHigh = false;
    state.shouldRetrigger = false;
    state.hasSlide = false;
    publishVoiceState(voices, manager, voiceIndex, state);
}
