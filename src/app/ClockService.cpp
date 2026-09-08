#include "ClockService.h"
#include "AppState.h"
#include "StepPlayback.h"
#include "../midi/MidiManager.h"
#include "../utils/SpscQueue.h"
#include <Arduino.h>
#include <uClock.h>

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
    Serial.println("[uClock] onClockStop()");
}

void processClockEvents()
{
    uint32_t step = 0;
    while (clockEvents.steps.tryPop(step))
    {
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
    while (clockEvents.ppqnTicksPending > 0)
    {
        // Decrement the counter *before* processing the tick
        clockEvents.ppqnTicksPending--;
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
