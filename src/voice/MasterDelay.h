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
#include <array>
#include <cstddef>
#include <cstdint>

class MasterDelay
{
public:
    // The original full-rate path keeps its 10..750 ms sound and precision.
    // The long synced path stores 6 kHz, 16-bit repeats: 32,768 slots cover
    // a whole note at the slowest supported 45 BPM without using more SRAM
    // than a second full-rate floating point delay line would require.
    static constexpr size_t kCapacitySamples = 36004;
    static constexpr size_t kMaxDelaySamples = 36000;
    static constexpr size_t kSyncCapacitySamples = 32768;
    static constexpr uint8_t kSyncDecimation = 8;

    static constexpr float kMinDelaySeconds = 0.010f;
    static constexpr float kDefaultDelaySeconds = 0.30f;
    // High but stable: the loop limiter + darkening filter keep long tails musical.
    static constexpr float kDefaultFeedback = 0.75f;
    static constexpr float kFeedbackCutoffHz = 2500.0f;

    void prepare(float sampleRate)
    {
        sampleRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
        maxDelaySeconds_ = static_cast<float>(kMaxDelaySamples) / sampleRate_;
        maxSyncSeconds_ = static_cast<float>((kSyncCapacitySamples - 4) * kSyncDecimation) / sampleRate_;
        feedbackFilter_.prepare(sampleRate_);
        feedbackFilter_.setCutoff(kFeedbackCutoffHz);
        dcBlocker_.prepare(sampleRate_);
        syncInputFilterA_.prepare(sampleRate_);
        syncInputFilterB_.prepare(sampleRate_);
        syncInputFilterA_.setCutoff(kSyncInputCutoffHz);
        syncInputFilterB_.setCutoff(kSyncInputCutoffHz);
        mixAlpha_ = smoothingAlpha(kMixTauSeconds);
        timeAlpha_ = smoothingAlpha(kTimeTauSeconds);
        syncTimeAlpha_ = smoothingAlpha(kSyncTimeTauSeconds);
        reset();
    }

    void reset()
    {
        line_.reset();
        syncLine_.fill(0);
        syncWriteIndex_ = 0;
        syncPhase_ = 0;
        fastValid_ = kCapacitySamples;
        syncValid_ = kSyncCapacitySamples;
        feedbackFilter_.reset();
        dcBlocker_.reset();
        syncInputFilterA_.reset();
        syncInputFilterB_.reset();
        delaySamples_ = targetDelaySamples_;
        timeSnapPending_ = false;
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
        const float maximum = synced_ ? maxSyncSeconds_ : maxDelaySeconds_;
        if (seconds > maximum)
            seconds = maximum;
        targetDelaySamples_ = seconds * sampleRate_;
        if (timeSnapPending_)
        {
            delaySamples_ = targetDelaySamples_;
            timeSnapPending_ = false;
        }
    }

    void setSynced(bool synced) noexcept
    {
        if (synced_ == synced) return;
        synced_ = synced;
        // Invalidate old material without zeroing a large line on Core 1.
        // Each path writes fresh samples before any stale slot can be heard.
        if (synced_) { syncValid_ = 0; syncPhase_ = 0; }
        else fastValid_ = 0;
        feedbackFilter_.reset();
        dcBlocker_.reset();
        syncInputFilterA_.reset();
        syncInputFilterB_.reset();
        timeSnapPending_ = true;
    }

    void setFeedback(float feedback)
    {
        targetFeedback_ = !(feedback > 0.0f) ? 0.0f : (feedback > 1.0f ? 1.0f : feedback);
    }

    // --- Audio thread -------------------------------------------------------

    float process(float input) noexcept
    {
        delaySamples_ += (synced_ ? syncTimeAlpha_ : timeAlpha_) *
                         (targetDelaySamples_ - delaySamples_);
        mix_ += mixAlpha_ * (targetMix_ - mix_);
        feedback_ += mixAlpha_ * (targetFeedback_ - feedback_);

        // In millisecond mode this is the original full-rate fractional tap.
        // In sync mode the compact line is read at its 6 kHz write rate, with
        // phase compensation for the eight 48 kHz output samples per slot.
        const float tap = synced_ ? readSyncTap_() : readFastTap_();
        const float regen = rpdsp::fastTanh(
            feedbackFilter_.process(dcBlocker_.process(tap)) * feedback_);
        if (synced_)
        {
            const float filtered = syncInputFilterB_.process(
                syncInputFilterA_.process(input + regen));
            if (++syncPhase_ == kSyncDecimation)
            {
                syncPhase_ = 0;
                // Allow headroom for dry + feedback before the 16-bit store.
                const float bounded = filtered < -4.0f ? -4.0f :
                                      (filtered > 3.999f ? 3.999f : filtered);
                syncLine_[syncWriteIndex_] = static_cast<int16_t>(bounded * 8192.0f);
                syncWriteIndex_ = (syncWriteIndex_ + 1) & (kSyncCapacitySamples - 1);
                if (syncValid_ < kSyncCapacitySamples) ++syncValid_;
            }
        }
        else
        {
            line_.push(input + regen);
            if (fastValid_ < kCapacitySamples) ++fastValid_;
        }
        return input + tap * mix_;
    }

    // Host-test observability.
    float delaySamplesTarget() const { return targetDelaySamples_; }
    float maxDelaySeconds() const { return maxDelaySeconds_; }
    float maxSyncedDelaySeconds() const { return maxSyncSeconds_; }

private:
    // ~63% of the way in 45 ms (mix/feedback) / 500 ms (delay time). The time slew is
    // the audible part: a gliding read head pitch-warps the loop.
    static constexpr float kMixTauSeconds = 0.045f;
    static constexpr float kTimeTauSeconds = 0.50f;
    static constexpr float kSyncTimeTauSeconds = 0.025f;
    static constexpr float kSyncInputCutoffHz = 2000.0f;

    float smoothingAlpha(float tauSeconds) const
    {
        return 1.0f - std::exp(-1.0f / (tauSeconds * sampleRate_));
    }

    float readFastTap_() const noexcept
    {
        // The valid count matters only after a mode switch. An ordinary reset
        // clears the line, so it starts fully valid and preserves exact timing.
        if (fastValid_ <= static_cast<size_t>(delaySamples_) + 2) return 0.0f;
        return line_.readCubic(delaySamples_ - 1.0f);
    }

    float syncValue_(size_t age) const noexcept
    {
        if (age >= syncValid_) return 0.0f;
        const size_t slot = (syncWriteIndex_ - 1 - age) & (kSyncCapacitySamples - 1);
        return static_cast<float>(syncLine_[slot]) * (1.0f / 8192.0f);
    }

    float readSyncTap_() const noexcept
    {
        float age = (delaySamples_ - static_cast<float>(syncPhase_ + 1)) /
                    static_cast<float>(kSyncDecimation);
        if (age < 0.0f) age = 0.0f;
        const size_t whole = static_cast<size_t>(age);
        const float frac = age - static_cast<float>(whole);
        const float ym1 = syncValue_(whole > 0 ? whole - 1 : 0);
        const float y0 = syncValue_(whole);
        const float y1 = syncValue_(whole + 1);
        const float y2 = syncValue_(whole + 2);
        const float c0 = (-frac * (frac - 1.0f) * (frac - 2.0f)) * (1.0f / 6.0f);
        const float c1 = ((frac + 1.0f) * (frac - 1.0f) * (frac - 2.0f)) * 0.5f;
        const float c2 = (-(frac + 1.0f) * frac * (frac - 2.0f)) * 0.5f;
        const float c3 = ((frac + 1.0f) * frac * (frac - 1.0f)) * (1.0f / 6.0f);
        return ym1 * c0 + y0 * c1 + y1 * c2 + y2 * c3;
    }

    rpdsp::DelayLine<kCapacitySamples> line_;
    std::array<int16_t, kSyncCapacitySamples> syncLine_{};
    size_t syncWriteIndex_ = 0;
    size_t fastValid_ = kCapacitySamples;
    size_t syncValid_ = kSyncCapacitySamples;
    uint8_t syncPhase_ = 0;
    bool synced_ = false;
    bool timeSnapPending_ = false;
    rpdsp::OnePoleLowpass feedbackFilter_;
    rpdsp::DcBlocker dcBlocker_;
    rpdsp::OnePoleLowpass syncInputFilterA_;
    rpdsp::OnePoleLowpass syncInputFilterB_;
    float sampleRate_ = 48000.0f;
    float maxDelaySeconds_ = 0.30f;
    float maxSyncSeconds_ = 0.30f;
    float feedback_ = kDefaultFeedback;
    float targetFeedback_ = kDefaultFeedback;
    float targetMix_ = 0.0f;
    float mix_ = 0.0f;
    float targetDelaySamples_ = kDefaultDelaySeconds * 48000.0f;
    float delaySamples_ = targetDelaySamples_;
    float mixAlpha_ = 1.0f;
    float timeAlpha_ = 1.0f;
    float syncTimeAlpha_ = 1.0f;
};

#endif // MASTER_DELAY_H
