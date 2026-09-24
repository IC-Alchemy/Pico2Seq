#include "VoicePlayback.h"
#include "AppState.h"

// Single publish path for steps, live edits, and note-offs (see header).
// Copy-then-retain: callers may pass the retained state itself. Core 0 only.

void publishVoiceState(uint8_t voiceIndex, const VoiceState &state)
{
    if (voiceIndex >= VoiceSystem::MAX_VOICES)
        return;

    // Copy first: callers may pass the retained state itself.
    const VoiceState update = state;
    auto &retained = voiceSystem.getVoiceState(voiceIndex);
    retained = update;
    retained.shouldRetrigger = false;
    if (voiceManager)
        voiceManager->updateVoiceState(voiceSystem.getVoiceId(voiceIndex), update);
}

void tickSequencerVoices()
{
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i)
    {
        auto &state = voiceSystem.getVoiceState(i);
        if (AppState::sequencers[i]->tickNoteDuration(&state))
            publishVoiceState(i, state);
    }
}

void stopSequencerVoice(uint8_t voiceIndex)
{
    if (voiceIndex >= VoiceSystem::MAX_VOICES)
        return;
    auto &sequencer = *AppState::sequencers[voiceIndex];
    auto &state = voiceSystem.getVoiceState(voiceIndex);
    sequencer.stop();
    sequencer.handleNoteOff(&state);
    // Always clear, even from an inactive sequencer (e.g. editor entry), so no
    // gate or slide survives into the next take.
    state.isGateHigh = false;
    state.shouldRetrigger = false;
    state.hasSlide = false;
    publishVoiceState(voiceIndex, state);
}
