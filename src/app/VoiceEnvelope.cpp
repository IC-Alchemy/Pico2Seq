#include "VoiceEnvelope.h"
#include "AppState.h"
#include "VoicePlayback.h"
#include "../voice/MusicalValues.h"
#include <cmath>

bool VoiceEnvelope::set(uint8_t voice, uint8_t channel, float normalized, bool arpMode)
{
    if (!voiceManager || voice >= VoiceSystem::MAX_VOICES || channel >= 4 ||
        !std::isfinite(normalized))
        return false;
    const auto id = voiceSystem.getVoiceId(voice);
    const auto *requested = voiceManager->getVoiceConfig(id);
    if (!requested) return false;
    VoiceConfig config = *requested;
    VoiceEdit::enablePatch(config);
    normalized = std::clamp(normalized, 0.0f, 1.0f);
    switch (channel) {
    case 0: config.defaultAttack = MusicalValues::attackSeconds(normalized); break;
    case 1: config.defaultDecay = MusicalValues::envelopeSeconds(normalized); break;
    case 2: config.defaultSustain = normalized; break;
    case 3: config.defaultRelease = MusicalValues::releaseSeconds(normalized); break;
    }
    if (!voiceManager->setVoiceConfig(id, config)) return false;
    uiState.voiceEditor.changed[voice] = true;

    // Some engines use Attack/Decay (and even Sustain) lanes as timbre macros.
    // Edit their actual envelope defaults without overwriting those macros.
    constexpr VoiceEdit::Id stages[] = {VoiceEdit::Id::EnvAttack,
        VoiceEdit::Id::EnvDecay, VoiceEdit::Id::Sustain, VoiceEdit::Id::Release};
    const ParamId lane = VoiceEdit::sequenceLane(stages[channel], config);
    if (lane != ParamId::Count && !arpMode)
        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
            AppState::sequencers[voice]->followPatch(lane, step);

    // Only the moved stage changes. Keep the arp pitch/dynamics, gate, slide,
    // and the other stages of a sounding step, even during a release tail.
    VoiceState state = voiceSystem.getVoiceState(voice);
    if (lane != ParamId::Count) {
        const float value = VoiceEdit::composeLane(lane, SequencerConstants::LANE_FOLLOWS_PATCH, &config);
        switch (channel) {
        case 0: state.attackTimeSeconds = value; break;
        case 1: state.decayTimeSeconds = value; break;
        case 2: state.sustainLevel = value; break;
        case 3: state.releaseTimeSeconds = value; break;
        }
    }
    state.shouldRetrigger = false;
    publishVoiceState(voice, state, static_cast<uint8_t>(1u << channel));
    return true;
}
