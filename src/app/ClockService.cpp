#include "ClockService.h"
#include "AppState.h"
#include "StepPlayback.h"
#include "VoicePlayback.h"
#include "../utils/SpscQueue.h"
#include <Arduino.h>
#include <uClock.h>
#include <hardware/sync.h>

// ISR stages, loop() plays: step numbers wait in a 16-deep ring, PPQN ticks in a
// counter. Same-core (Core 0) handoff, no locks; processing stays in thread
// context so the tick ISR never blocks and the groove stays tight.

uint32_t g_processedStepCount = 0;

namespace
{
constexpr size_t kQueuedStepCapacity = 16;
constexpr float kStartingTempoBpm = 90.0f;
struct ClockEvents
{
    SpscQueue<uint32_t, kQueuedStepCapacity> steps;
    // Same-core ISR visibility; increments can still be lost if loop() drains
    // mid-burst. Retained policy: PPQN only shortens notes, never hangs them.
    volatile uint32_t ppqnTicksPending = 0;
    volatile uint32_t droppedSteps = 0;
};
ClockEvents clockEvents;

void onStepCallback(uint32_t uClockCurrentStep)
{
    if (!clockEvents.steps.tryPush(uClockCurrentStep))
    {
        // loop() stalled longer than 16 steps: drop the newest, count it.
        clockEvents.droppedSteps++;
    }
}

void onOutputPPQNCallback(uint32_t tick)
{
    // Stage one pending tick for the loop drain; note length is ticked there.
    clockEvents.ppqnTicksPending++;
}
} // namespace

void onClockStart()
{
    // Thread context (uClock.start() call site): safe to touch sequencers/voices.
    if (uiState.voiceEditor.active) return;
    if (voiceManager) voiceManager->setTransportMuted(false);
     Serial.println("[uClock] onClockStart()");
    for (auto *sequencer : AppState::sequencers)
        sequencer->start();
    isClockRunning = true;
}

void onClockStop()
{
    isClockRunning = false;
    if (voiceManager)
        voiceManager->setTransportMuted(true);
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i)
        stopSequencerVoice(i);
    Serial.println("[uClock] onClockStop()");
}

void processClockEvents()
{
    // Core 0 loop drain: turns staged steps into sounding notes (see StepPlayback).
    uint32_t step = 0;
    while (clockEvents.steps.tryPop(step))
    {
        if (isClockRunning && !uiState.voiceEditor.active) processSequencerStep(step);
    }
}

void initializeClock()
{
    // 90 BPM, 480 PPQN, shuffle on: the default groove before the session overrides.
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
    // Snapshot the tick count under IRQ lock (same-core ISR race), then publish
    // each note-off on its exact tick so short gates never drag to the next step.
    const uint32_t irqState=save_and_disable_interrupts();
    uint32_t pending=clockEvents.ppqnTicksPending;
    clockEvents.ppqnTicksPending=0;
    restore_interrupts(irqState);
    if (!isClockRunning || uiState.voiceEditor.active) return;
    while (pending-- > 0)
    {
        // Publish note-off at its exact PPQN tick, not the next step boundary.
        tickSequencerVoices();
    }
}

void stopClockForEditor()
{
    // Editor needs silence: stop transport, clear gates, and drop staged ticks.
    uClock.stop();
    onClockStop(); // Also cleans up if transport was already stopped.
    const uint32_t irqState=save_and_disable_interrupts();
    clockEvents.ppqnTicksPending=0;
    uint32_t unused=0;
    while(clockEvents.steps.tryPop(unused)) {}
    restore_interrupts(irqState);
}
