#ifndef MASTER_DELAY_H
#define MASTER_DELAY_H

// MasterDelay — analog-style feedback delay on the summed mono voice bus.
//
// Built from rpdsp primitives around a fixed 1 s line:
//   - a fractionally interpolated read (readCubic) whose delay time glides
//     toward new targets with a one-pole slew, so turning the time bends
//     pitch like a tape machine instead of stepping,
//   - regeneration through a DC blocker and a one-pole lowpass into
//     fastTanh: repeats darken every pass, and the loop gain can never
//     exceed 1, so heavy feedback self-limits instead of running away,
//   - a wet tap taken before the feedback filter, so the first repeat keeps
//     its highs while later ones go progressively darker.
//
// Control-thread setters publish targets; process() is audio-thread-only and
// eases mix and time per sample (the same contract as the master gain).

#include "../rpdsp/src/rpdsp/algorithm.h"
#include "../rpdsp/src/rpdsp/delay_line.h"
#include "../rpdsp/src/rpdsp/filter.h"

#include <cmath>
#include <cstddef>

class MasterDelay
{
public:
    // 48000 floats = 1.0 s at 48 kHz (~187.5 KiB). Reserved once, before
    // audio starts; VoiceManager owns this next to the voices.
    static constexpr size_t kCapacitySamples = 48000;

    static constexpr float kMinDelaySeconds = 0.010f;
    static constexpr float kDefaultDelaySeconds = 0.30f;
    // High but stable: fastTanh bounds the loop and the feedback lowpass
    // keeps the ringing repeats musical.
    static constexpr float kDefaultFeedback = 0.85f;
    static constexpr float kFeedbackCutoffHz = 2500.0f;

    void prepare(float sampleRate)
    {
        sampleRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
        maxDelaySeconds_ = static_cast<float>(kCapacitySamples *.75f) / sampleRate_;
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
    }

    // --- Control thread: publish targets ----------------------------------

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
        feedback_ = feedback < 0.0f ? 0.0f : (feedback > 0.995f ? 0.995f : feedback);
    }

    // --- Audio thread -------------------------------------------------------

    float process(float input) noexcept
    {
        delaySamples_ += timeAlpha_ * (targetDelaySamples_ - delaySamples_);
        mix_ += mixAlpha_ * (targetMix_ - mix_);

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
    // ~63% of the way in 15 ms (mix) / 20 ms (delay time). The time slew is
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
    float targetMix_ = 0.0f;
    float mix_ = 0.0f;
    float targetDelaySamples_ = kDefaultDelaySeconds * 48000.0f;
    float delaySamples_ = targetDelaySamples_;
    float mixAlpha_ = 1.0f;
    float timeAlpha_ = 1.0f;
};

#endif // MASTER_DELAY_H
