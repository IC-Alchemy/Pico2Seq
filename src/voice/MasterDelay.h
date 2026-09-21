#ifndef MASTER_DELAY_H
#define MASTER_DELAY_H

// MasterDelay.h — analog-style feedback delay on the summed mono voice bus.
// Repeats darken every pass (DC blocker + lowpass in the loop, fastTanh bounds
// loop gain to 1 so high feedback blooms instead of running away). Time glides
// (one-pole slew) so turning it bends pitch like tape. Audio thread after
// prepare(); VoiceManager publishes targets via atomics; no allocation.

#include "../rpdsp/src/rpdsp/algorithm.h"
#include "../rpdsp/src/rpdsp/delay_line.h"
#include "../rpdsp/src/rpdsp/filter.h"

#include <cmath>
#include <cstddef>

class MasterDelay
{
public:
    // 48000 floats = 1.0 s at 48 kHz. Reserved once at prepare(), before
    // audio starts; VoiceManager owns this next to the voices.
    static constexpr size_t kCapacitySamples = 48000;
    static constexpr size_t kMaxDelaySamples = kCapacitySamples * 3 / 4;

    static constexpr float kMinDelaySeconds = 0.010f;
    static constexpr float kDefaultDelaySeconds = 0.30f;
    // High but stable: the loop limiter + darkening filter keep long tails musical.
    static constexpr float kDefaultFeedback = 0.75f;
    static constexpr float kFeedbackCutoffHz = 2500.0f;

    void prepare(float sampleRate)
    {
        sampleRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
        maxDelaySeconds_ = static_cast<float>(kMaxDelaySamples) / sampleRate_;
        feedbackFilter_.prepare(sampleRate_);
        feedbackFilter_.setCutoff(kFeedbackCutoffHz);
        dcBlocker_.prepare(sampleRate_);
        mixAlpha_ = smoothingAlpha(kMixTauSeconds);
        timeAlpha_ = smoothingAlpha(kTimeTauSeconds);
        reset();
    }

    void reset()
    {
        line_.reset();
        feedbackFilter_.reset();
        dcBlocker_.reset();
        delaySamples_ = targetDelaySamples_;
        mix_ = targetMix_;
        feedback_ = targetFeedback_;
    }

    // --- Audio thread: receive targets from VoiceManager -------------------

    void setMix(float mix)
    {
        targetMix_ = mix < 0.0f ? 0.0f : (mix > 1.0f ? 1.0f : mix);
    }

    void setDelaySeconds(float seconds)
    {
        if (!(seconds > kMinDelaySeconds))
            seconds = kMinDelaySeconds;
        if (seconds > maxDelaySeconds_)
            seconds = maxDelaySeconds_;
        targetDelaySamples_ = seconds * sampleRate_;
    }

    void setFeedback(float feedback)
    {
        targetFeedback_ = !(feedback > 0.0f) ? 0.0f : (feedback > 1.0f ? 1.0f : feedback);
    }

    // --- Audio thread -------------------------------------------------------

    float process(float input) noexcept
    {
        delaySamples_ += timeAlpha_ * (targetDelaySamples_ - delaySamples_);
        mix_ += mixAlpha_ * (targetMix_ - mix_);
        feedback_ += mixAlpha_ * (targetFeedback_ - feedback_);

        // readCubic(D-1) before the push is a D-sample fractional delay (the
        // same one-sample visibility compensation rpdsp::Delay uses).
        const float tap = line_.readCubic(delaySamples_ - 1.0f);
        const float regen = rpdsp::fastTanh(
            feedbackFilter_.process(dcBlocker_.process(tap)) * feedback_);
        line_.push(input + regen);
        return input + tap * mix_;
    }

    // Host-test observability.
    float delaySamplesTarget() const { return targetDelaySamples_; }
    float maxDelaySeconds() const { return maxDelaySeconds_; }

private:
    // ~63% of the way in 45 ms (mix/feedback) / 500 ms (delay time). The time slew is
    // the audible part: a gliding read head pitch-warps the loop.
    static constexpr float kMixTauSeconds = 0.045f;
    static constexpr float kTimeTauSeconds = 0.50f;

    float smoothingAlpha(float tauSeconds) const
    {
        return 1.0f - std::exp(-1.0f / (tauSeconds * sampleRate_));
    }

    rpdsp::DelayLine<kCapacitySamples> line_;
    rpdsp::OnePoleLowpass feedbackFilter_;
    rpdsp::DcBlocker dcBlocker_;
    float sampleRate_ = 48000.0f;
    float maxDelaySeconds_ = 0.30f;
    float feedback_ = kDefaultFeedback;
    float targetFeedback_ = kDefaultFeedback;
    float targetMix_ = 0.0f;
    float mix_ = 0.0f;
    float targetDelaySamples_ = kDefaultDelaySeconds * 48000.0f;
    float delaySamples_ = targetDelaySamples_;
    float mixAlpha_ = 1.0f;
    float timeAlpha_ = 1.0f;
};

#endif // MASTER_DELAY_H
