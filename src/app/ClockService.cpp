#include "ClockService.h"
#include "AppState.h"
#include "ArpPlayback.h"
#include "StepPlayback.h"
#include "VoicePlayback.h"
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
    for (auto *sequencer : AppState::sequencers)
        sequencer->start();
    // Arpeggiator mode: a chord held through the stop starts from its root on
    // the downbeat instead of wherever the walk was left.
    arpTransportStart();
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
    uint32_t step = 0;
    while (clockEvents.steps.tryPop(step))
    {
        // Arpeggiator mode consumes no clock steps: the arp advances on the PPQN
        // path below, where its note divisions live. The queue still drains so
        // it cannot back up while the mode is on.
        if (isClockRunning && !uiState.voiceEditor.active && !uiState.arp.active())
            processSequencerStep(step);
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
        // Sequencer mode publishes note-off at its exact PPQN tick rather than
        // at the next step boundary. Arpeggiator mode owns the tick instead: the
        // engine decides when its notes start and end, and the sequencers'
        // duration counters stay put.
        if (uiState.arp.active())
            arpTick();
        else
            tickSequencerVoices();
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
