// LoopController.h — Core 0 policy of the looper: when a take starts, what it is called,
// and how the playing loop stays on the step grid.
//
// Musical role: tap the loop button and the take waits for the next loop boundary, records
// exactly the chosen number of steps (4, 8 or 16), then plays in sync. Every further tap
// layers: one more pass mixes the live bus into the loop and writes the sum back over it, so
// many layers share the one small buffer. A tap while a pass is still running queues the
// next layer to start the instant that pass ends. Hold to clear. Technical role: a thin state
// machine over LoopEngine's command ring. It owns no audio and no hardware: the step
// clock, the tempo and the button gestures are passed in, so it is portable and tested
// against the real engine (tests/unit/test_loop_controller.cpp). Core 0 only.
//
// The step clock is counted here (stepIndex_) rather than trusted from uClock, so the
// loop boundary arithmetic cannot be thrown off by how the clock library numbers steps;
// onClockStart() restarts the count with the sequencers.
#pragma once

#include "../voice/LoopEngine.h"
#include "../voice/LoopTiming.h"

#include <cstdint>

class LoopController
{
public:
    enum class Phase : uint8_t
    {
        Unavailable, // no loop buffer (not enough heap)
        Empty,
        Armed,       // waiting for the next boundary to start a take
        Recording,   // first take in progress
        Playing,
        Overdubbing, // a layer pass: live bus mixed into the loop and baked back
    };

    // Attach the engine this controller drives. `engine` must outlive the controller.
    void bind(LoopEngine *engine, float sampleRate = 48000.0f) noexcept
    {
        engine_ = engine;
        sampleRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    }

    // --- Settings the Loop page edits -------------------------------------------
    // The size of the NEXT take; a loop already recorded keeps the size it was made at.
    void setSizeIndex(uint8_t index) noexcept { sizeIndex_ = LoopTiming::clampIndex(index); }
    uint8_t sizeIndex() const noexcept { return sizeIndex_; }
    uint8_t sizeSteps() const noexcept { return LoopTiming::stepsForIndex(sizeIndex_); }

    // --- Button gestures ---------------------------------------------------------
    void tap(float bpm) noexcept;  // arm / disarm a take; during a pass, queue / withdraw the next layer
    void clear() noexcept;         // forget the loop

    // --- Clock (call from the step-processing slice) -----------------------------
    void onClockStart() noexcept;
    void onClockStop() noexcept;
    void onStep(float bpm) noexcept; // once per clock step, in order

    // --- Every Core 0 pass --------------------------------------------------------
    void update(float bpm) noexcept;

    // --- Display ----------------------------------------------------------------------
    Phase phase() const noexcept;
    uint8_t loopSteps() const noexcept { return playingSteps_; }   // steps of the loop in the engine, 0 = none
    uint8_t armedSteps() const noexcept { return armSteps_; }      // steps of the take being waited for
    // True while a layer is queued behind the pass that is running (shown as "+DUB").
    bool layerQueued() const noexcept;
    // Steps until an armed take begins (1 = the very next step); 0 when not armed.
    uint8_t stepsUntilStart() const noexcept;
    // Step (1-based) the loop is at, for "REC 5/16"; 0 when nothing is moving.
    uint8_t currentStep() const noexcept;

private:
    bool postRecord_(uint32_t stepIndex, float bpm) noexcept;
    void flushPending_() noexcept;
    Phase enginePhase_() const noexcept;
    bool takePending_() const noexcept;
    bool firstTakeInFlight_() const noexcept;

    LoopEngine *engine_ = nullptr;
    float sampleRate_ = 48000.0f;
    uint8_t sizeIndex_ = LoopTiming::kDefaultSizeIndex;

    bool clockRunning_ = false;
    uint32_t stepIndex_ = 0;   // index of the NEXT step to arrive (the first is 0)
    uint32_t anchorStep_ = 0;  // step the playing loop's first take began on

    bool armed_ = false;
    bool armOverdub_ = false;  // armed take layers over the playing loop
    uint8_t armSteps_ = 0;
    uint8_t playingSteps_ = 0; // size of the loop the engine holds (or is making)

    bool awaitingTake_ = false; // posted, the audio thread has not started it yet
    bool takeIsOverdub_ = false;
    uint32_t takesBefore_ = 0;
    uint32_t lastPeriod_ = 0;

    // Commands that found the ring full are retried every pass until they land.
    bool pendingClear_ = false;
    bool pendingCancel_ = false;
    bool pendingRestart_ = false;
};
