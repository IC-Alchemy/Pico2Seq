#include "ClockService.h"
#include "AppState.h"
#include "StepPlayback.h"
#include "../midi/MidiManager.h"
#include "../utils/SpscQueue.h"
#include <Arduino.h>
#include <uClock.h>
#include <hardware/sync.h>

uint32_t g_processedStepCount = 0;

namespace
{
constexpr size_t kQueuedStepCapacity = 16;
constexpr float kStartingTempoBpm = 90.0f;
struct ClockEvents
{
    SpscQueue<uint32_t, kQueuedStepCapacity> steps;
    // Same-core ISR visibility. The existing increment/decrement policy is
    // retained; volatile does not fix its pre-existing lost-increment window.
    volatile uint32_t ppqnTicksPending = 0;
    volatile uint32_t droppedSteps = 0;
    uint16_t gateTick = 0;
};
ClockEvents clockEvents;

void onStepCallback(uint32_t uClockCurrentStep)
{
    if (!clockEvents.steps.tryPush(uClockCurrentStep))
    {
        clockEvents.droppedSteps++; // loop() stalled longer than the queue
    }
}

void onOutputPPQNCallback(uint32_t tick)
{
    // Increment counter to signal pending tick processing
    clockEvents.ppqnTicksPending++;
}
} // namespace

void onClockStart()
{
    if (uiState.voiceEditor.active) return;
    if (voiceManager) voiceManager->setTransportMuted(false);
     Serial.println("[uClock] onClockStart()");
    // Start all four sequencers so  LEDs and audio advance for 3/4 as well
    seq1.start();
    seq2.start();
    seq3.start();
    seq4.start();
    isClockRunning = true;
}

void onClockStop()
{
    // Stop all four sequencers
    seq1.stop();
    seq2.stop();
    seq3.stop();
    seq4.stop();

    // Use MidiNoteManager for comprehensive cleanup
    midiNoteManager.onSequencerStop();

    // Legacy allNotesOff() call for sequencer state cleanup
    isClockRunning = false;
    if (voiceManager) {
        voiceManager->setTransportMuted(true);
        for (uint8_t i=0;i<VoiceSystem::MAX_VOICES;++i) {
            auto &state=voiceSystem.getVoiceState(i);
            state.isGateHigh=false; state.shouldRetrigger=false; state.hasSlide=false;
            voiceManager->updateVoiceState(voiceSystem.getVoiceId(i),state);
        }
    }
    Serial.println("[uClock] onClockStop()");
}

void processClockEvents()
{
    uint32_t step = 0;
    while (clockEvents.steps.tryPop(step))
    {
        if (isClockRunning && !uiState.voiceEditor.active) processSequencerStep(step);
    }
}

void initializeClock()
{
    uClock.init();
    uClock.setOnClockStart(onClockStart);
    uClock.setOnClockStop(onClockStop);
    uClock.setOutputPPQN(uClock.PPQN_480);
    uClock.setOnStep(onStepCallback);
    uClock.setOnOutputPPQN(onOutputPPQNCallback);
    uClock.setTempo(kStartingTempoBpm);
    uClock.start();
    uClock.setShuffle(true);
}

void processPendingGateTicks()
{
    const uint32_t irqState=save_and_disable_interrupts();
    uint32_t pending=clockEvents.ppqnTicksPending;
    clockEvents.ppqnTicksPending=0;
    restore_interrupts(irqState);
    if (!isClockRunning || uiState.voiceEditor.active) return;
    while (pending-- > 0)
    {
        clockEvents.gateTick++;

        // Update MidiNoteManager timing - this handles all MIDI note-off timing
        midiNoteManager.updateTiming(clockEvents.gateTick);

        // Process sequencer note duration timing
        seq1.tickNoteDuration(&voiceSystem.getVoiceState(0));
        seq2.tickNoteDuration(&voiceSystem.getVoiceState(1));

        // Process gate timers - now synchronized with MidiNoteManager
        voiceSystem.tickAllGateTimers();
    }
}

void stopClockForEditor()
{
    uClock.stop();
    onClockStop(); // Also cleans up if transport was already stopped.
    const uint32_t irqState=save_and_disable_interrupts();
    clockEvents.ppqnTicksPending=0;
    uint32_t unused=0;
    while(clockEvents.steps.tryPop(unused)) {}
    restore_interrupts(irqState);
}
