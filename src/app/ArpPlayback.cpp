#include "ArpPlayback.h"
#include "AppState.h"
#include "VoicePlayback.h"
#include "../ui/UIConstants.h"
#include "../ui/UITransitions.h"
#include "../voice/MusicalValues.h"
#include <Arduino.h>
#include <algorithm>

namespace
{
// Voice behind each arp slot, remembered when the note starts. The selected
// voice can move while a note sounds, and the gate-off has to reach the voice
// the note actually went to.
uint8_t slotVoice[Arpeggiator::kMaxSlots] = {0, 1, 2, 3};

// Gate off one voice without touching the sequencer's own note bookkeeping.
void silenceVoice(uint8_t voice)
{
    if (voice >= VoiceSystem::MAX_VOICES)
        return;
    VoiceState &state = voiceSystem.getVoiceState(voice);
    state.isGateHigh = false;
    state.shouldRetrigger = false;
    publishVoiceState(voice, state);
}

// Start one arp note on one voice. The step comes from the voice's own patch, so
// a preset keeps its engine, envelope and level; the arp only overrides what it
// owns: pitch, its octave of range, lidar dynamics and the filter fader.
void arpNoteOn(uint8_t voice, uint8_t degree, uint8_t octave)
{
    if (!voiceManager || voice >= VoiceSystem::MAX_VOICES)
        return;
    const VoiceConfig *config = voiceManager->getVoiceConfig(voiceSystem.getVoiceId(voice));
    if (!config)
        return;

    const Step base = MusicalValues::baseStep(*config);
    VoiceState state;
    state.noteIndex = static_cast<float>(degree);
    state.octaveOffset = static_cast<int8_t>(12 * octave);
    state.velocityLevel = std::clamp(base.velocityLevel * uiState.arp.velocityScale(), 0.0f, 1.0f);
    state.filterCutoff = VoiceEdit::composeLane(ParamId::Filter, uiState.arp.settings().filter, config);
    state.attackTimeSeconds = base.attackTimeSeconds;
    state.decayTimeSeconds = base.decayTimeSeconds;
    state.sustainLevel = base.sustainLevel;
    state.releaseTimeSeconds = base.releaseTimeSeconds;
    state.gateLengthTicks = std::max<uint16_t>(1, uiState.arp.lastGateTicks());
    state.isGateHigh = true;
    state.shouldRetrigger = true; // envelope restart; the voice's event path
    state.hasSlide = false;
    publishVoiceState(voice, state);
}
} // namespace

void arpModeToggle(UIState &uiState)
{
    const bool entering = !uiState.arp.active();

    // Silence first, on both edges: the ticks that would end the sounding note
    // are about to come from the other mode's path.
    if (entering)
    {
        // Entering also ends the sequencers' note tracking, exactly like the
        // editor's entry does: their duration ticks stop arriving in this mode,
        // so an armed gate would never expire.
        for (uint8_t voice = 0; voice < VoiceSystem::MAX_VOICES; ++voice)
            stopSequencerVoice(voice);
    }
    else
    {
        for (uint8_t voice = 0; voice < VoiceSystem::MAX_VOICES; ++voice)
            silenceVoice(voice);
        // stopSequencerVoice() above also cleared each sequencer's running flag.
        // A clock that is still running must not come back to silent sequencers
        // (the editor path never hits this because it stops the clock first).
        if (isClockRunning)
            for (auto *sequencer : AppState::sequencers)
                sequencer->start();
    }

    if (entering)
        UITransitions::enterArpMode(uiState);
    else
        UITransitions::exitArpMode(uiState);

    for (uint8_t slot = 0; slot < Arpeggiator::kMaxSlots; ++slot)
        slotVoice[slot] = slot < VoiceSystem::MAX_VOICES ? slot : 0;

    // The panel draws a different picture in each mode; drop the smoothed
    // colours so no arp chord lingers on the LEDs once the pads are steps again.
    uiState.resetStepsLightsFlag = true;
    uiState.oledNoticeKind = entering ? UIState::OledNoticeKind::ArpOn : UIState::OledNoticeKind::ArpOff;
    uiState.oledNoticeUntil = millis() + OLED_NOTICE_DURATION_MS;
    if (Serial)
        Serial.printf("[ARP] %s pattern=%s rate=%s oct=%u\n", entering ? "mode on" : "mode off",
                      uiState.arp.patternLabel(), uiState.arp.rateLabel(),
                      static_cast<unsigned>(uiState.arp.settings().octaves));
}

void arpTick()
{
    const Arpeggiator::Tick out = uiState.arp.tick();
    for (uint8_t slot = 0; slot < Arpeggiator::kMaxSlots; ++slot)
    {
        const uint8_t slotBit = static_cast<uint8_t>(1u << slot);
        // The engine orders stops before starts on the same tick, so a mono
        // pattern retriggers its voice from silence.
        if (out.stopMask & slotBit)
            silenceVoice(slotVoice[slot]);
        if (out.startMask & slotBit)
        {
            const uint8_t voice = Arpeggiator::slotVoiceIndex(slot, uiState.selectedVoiceIndex);
            slotVoice[slot] = voice;
            arpNoteOn(voice, out.degrees[slot], out.octaves[slot]);
        }
    }
}

void arpTransportStart()
{
    if (uiState.arp.active())
        uiState.arp.restart();
}
