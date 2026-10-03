#include "LoopController.h"

using Phase = LoopController::Phase;
using EngineState = LoopEngine::State;

// A posted take is "waiting" until the audio thread has counted it; checking the counter
// here (not only in update()) keeps phase() right on the pass between the two.
bool LoopController::takePending_() const noexcept
{
    return awaitingTake_ && engine_ && engine_->takesStarted() == takesBefore_;
}

bool LoopController::firstTakeInFlight_() const noexcept
{
    if (!engine_)
        return false;
    return (takePending_() && !takeIsOverdub_) || engine_->state() == EngineState::Recording;
}

Phase LoopController::phase() const noexcept
{
    if (!engine_ || engine_->state() == EngineState::Disabled)
        return Phase::Unavailable;
    if (armed_)
        return Phase::Armed;
    if (takePending_())
        return takeIsOverdub_ ? Phase::Overdubbing : Phase::Recording;
    switch (engine_->state())
    {
    case EngineState::Recording: return Phase::Recording;
    case EngineState::Playing: return Phase::Playing;
    case EngineState::Overdubbing: return Phase::Overdubbing;
    default: return Phase::Empty;
    }
}

uint8_t LoopController::stepsUntilStart() const noexcept
{
    if (!armed_ || !clockRunning_)
        return 0;
    if (armOverdub_ && playingSteps_ != 0)
    {
        const uint32_t into = (stepIndex_ - anchorStep_) % playingSteps_;
        return static_cast<uint8_t>((playingSteps_ - into) % playingSteps_ + 1);
    }
    const uint8_t q = LoopTiming::quantizeSteps(armSteps_);
    return static_cast<uint8_t>((q - stepIndex_ % q) % q + 1);
}

uint8_t LoopController::currentStep() const noexcept
{
    if (!engine_ || playingSteps_ == 0)
        return 0;
    const EngineState state = engine_->state();
    if (state != EngineState::Recording && state != EngineState::Playing &&
        state != EngineState::Overdubbing)
        return 0;
    const uint32_t step = static_cast<uint32_t>(engine_->position() * static_cast<float>(playingSteps_));
    return static_cast<uint8_t>((step < playingSteps_ ? step : playingSteps_ - 1u) + 1u);
}

// A take begins on the step whose index lines up with the loop: every `quantize` steps for a
// fresh loop, every loop length (counted from where the loop began) for a layer.
bool LoopController::postRecord_(uint32_t stepIndex, float bpm) noexcept
{
    // A layer armed over a loop that has since faded out is just a fresh take.
    if (armOverdub_ && (playingSteps_ == 0 || engine_->state() != EngineState::Playing))
        armOverdub_ = false;
    const uint32_t frames = LoopTiming::loopFrames(armSteps_, bpm, sampleRate_);
    const uint32_t before = engine_->takesStarted();
    if (!engine_->postRecord(frames))
        return false;
    armed_ = false;
    awaitingTake_ = true;
    takeIsOverdub_ = armOverdub_;
    takesBefore_ = before;
    if (!armOverdub_)
    {
        playingSteps_ = armSteps_;
        anchorStep_ = stepIndex;
    }
    engine_->setPeriodFrames(frames);
    lastPeriod_ = frames;
    return true;
}

void LoopController::tap(float bpm) noexcept
{
    if (!engine_)
        return;
    switch (phase())
    {
    case Phase::Unavailable:
    case Phase::Overdubbing: // a layer in flight finishes its pass
        return;
    case Phase::Armed:
        armed_ = false;
        return;
    case Phase::Recording:
        // Abandon the first take: it has no use until it is whole.
        pendingCancel_ = true;
        awaitingTake_ = false;
        playingSteps_ = 0;
        flushPending_();
        return;
    case Phase::Empty:
    case Phase::Playing:
        armOverdub_ = playingSteps_ != 0 && engine_->state() == EngineState::Playing;
        armSteps_ = armOverdub_ ? playingSteps_ : sizeSteps();
        armed_ = true;
        // With no step clock there is nothing to wait for: begin at once.
        if (!clockRunning_)
            postRecord_(0, bpm);
        return;
    }
}

void LoopController::clear() noexcept
{
    if (!engine_)
        return;
    armed_ = false;
    awaitingTake_ = false;
    playingSteps_ = 0;
    pendingCancel_ = false;
    pendingClear_ = true;
    flushPending_();
}

void LoopController::onClockStart() noexcept
{
    clockRunning_ = true;
    stepIndex_ = 0;
    anchorStep_ = 0;
    // The sequencers restart from step 0: the loop goes back to its top with them.
    if (engine_ && playingSteps_ != 0)
        pendingRestart_ = true;
}

void LoopController::onClockStop() noexcept
{
    clockRunning_ = false;
    if (!engine_)
        return;
    armed_ = false; // a take waiting on the clock would never come
    if (firstTakeInFlight_())
    {
        // Half a loop of whatever the voices were doing is not worth keeping.
        pendingCancel_ = true;
        awaitingTake_ = false;
        playingSteps_ = 0;
        flushPending_();
    }
}

void LoopController::onStep(float bpm) noexcept
{
    const uint32_t step = stepIndex_++;
    if (!engine_)
        return;
    if (armed_)
    {
        const bool boundary = armOverdub_ && playingSteps_ != 0
                                  ? (step - anchorStep_) % playingSteps_ == 0
                                  : step % LoopTiming::quantizeSteps(armSteps_) == 0;
        if (boundary)
            postRecord_(step, bpm);
        return;
    }
    // Re-align the playing loop to the grid at every repeat. The engine ignores a sync
    // that lands within a few milliseconds of its own wrap, so a healthy loop never moves.
    if (playingSteps_ != 0 && engine_->state() == EngineState::Playing && step > anchorStep_ &&
        (step - anchorStep_) % playingSteps_ == 0)
        engine_->postSync();
}

void LoopController::flushPending_() noexcept
{
    if (!engine_)
        return;
    if (pendingClear_ && engine_->postClear())
        pendingClear_ = false;
    if (pendingCancel_ && engine_->postCancel())
        pendingCancel_ = false;
    if (pendingRestart_ && engine_->postRestart())
        pendingRestart_ = false;
}

void LoopController::update(float bpm) noexcept
{
    if (!engine_)
        return;
    flushPending_();

    // The loop's length on the grid follows the tempo; the engine varispeeds to match.
    if (playingSteps_ != 0)
    {
        const uint32_t frames = LoopTiming::loopFrames(playingSteps_, bpm, sampleRate_);
        if (frames != lastPeriod_ && !awaitingTake_)
        {
            engine_->setPeriodFrames(frames);
            lastPeriod_ = frames;
        }
    }

    if (awaitingTake_ && engine_->takesStarted() != takesBefore_)
        awaitingTake_ = false;
    // A loop that regen faded out (or anything else that emptied the engine) is gone.
    if (!awaitingTake_ && !pendingClear_ && playingSteps_ != 0 &&
        engine_->state() == EngineState::Empty)
        playingSteps_ = 0;
}
