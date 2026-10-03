#ifndef MASTER_DELAY_H
#define MASTER_DELAY_H

// MasterDelay.h — analog-style feedback delay on the summed mono voice bus.
// Repeats darken every pass (DC blocker + lowpass in the loop, fastTanh bounds
// loop gain to 1 so high feedback blooms instead of running away). Time glides
// (one-pole slew) so turning it bends pitch like tape. Audio thread after
// prepare(); VoiceManager publishes targets via atomics; no allocation.

#include "../rpdsp/src/rpdsp/algorithm.h"
#include "../rpdsp/src/rpdsp/filter.h"

#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

class MasterDelay
{
public:
    // The full-rate path keeps its sound and precision over 10..375 ms (it was
    // 10..750 ms; halving the line returned 72,000 bytes of heap to the looper, see
    // docs/audio-performance.md). The long synced path stores 6 kHz, 16-bit
    // repeats: 32,768 slots cover a whole note at the slowest supported 45 BPM.
    //
    // Only one of the two rings is live at a time (synced_ picks it), so they share
    // one block of kCapacitySamples * 4 bytes: the float ring uses it as
    // kCapacitySamples floats, the synced ring as kSyncCapacitySamples int16s at the
    // start of the same bytes. That saves the 64 KiB a separate synced ring would
    // cost, and at the halved line the synced ring still fits (65,536 of 72,016
    // bytes). Switching modes marks the incoming ring empty (fastValid_/syncValid_ = 0)
    // and reads before a slot is rewritten return silence, so the other ring's bytes
    // are never interpreted. reset() zeroes the block, which is silence in both views.
    static constexpr size_t kCapacitySamples = 18004;
    static constexpr size_t kMaxDelaySamples = 18000;
    static constexpr size_t kSyncCapacitySamples = 32768;
    static constexpr uint8_t kSyncDecimation = 8;
    static_assert(sizeof(float) == sizeof(uint32_t), "the shared block holds one float per word");
    static_assert(kSyncCapacitySamples * sizeof(int16_t) <= kCapacitySamples * sizeof(float),
                  "the synced ring must fit inside the float ring's storage");

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
        storage_.fill(0); // +0.0f as a float and 0 as an int16: silence in both views
        fastWriteIndex_ = 0;
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
                syncStore_(syncWriteIndex_, static_cast<int16_t>(bounded * 8192.0f));
                syncWriteIndex_ = (syncWriteIndex_ + 1) & (kSyncCapacitySamples - 1);
                if (syncValid_ < kSyncCapacitySamples) ++syncValid_;
            }
        }
        else
        {
            fastStore_(fastWriteIndex_, input + regen);
            if (++fastWriteIndex_ == kCapacitySamples) fastWriteIndex_ = 0;
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
        return readFastCubic_(delaySamples_ - 1.0f);
    }

    // Third-order Lagrange interpolation between four neighbouring samples.
    static float cubic_(float frac, float ym1, float y0, float y1, float y2) noexcept
    {
        const float c0 = (-frac * (frac - 1.0f) * (frac - 2.0f)) * (1.0f / 6.0f);
        const float c1 = ((frac + 1.0f) * (frac - 1.0f) * (frac - 2.0f)) * 0.5f;
        const float c2 = (-(frac + 1.0f) * frac * (frac - 2.0f)) * 0.5f;
        const float c3 = ((frac + 1.0f) * frac * (frac - 1.0f)) * (1.0f / 6.0f);
        return (ym1 * c0) + (y0 * c1) + (y1 * c2) + (y2 * c3);
    }

    // The float ring below is rpdsp::DelayLine's indexing, clamping and cubic read,
    // kept here so it can live in the shared block instead of a private array.
    float fastLoad_(size_t slot) const noexcept
    {
        float value;
        std::memcpy(&value, &storage_[slot], sizeof value);
        return value;
    }

    void fastStore_(size_t slot, float value) noexcept
    {
        std::memcpy(&storage_[slot], &value, sizeof value);
    }

    // The sample written `delaySamples` writes ago (0 = the newest).
    float fastRead_(size_t delaySamples) const noexcept
    {
        const size_t readOffset = delaySamples + 1;
        return fastLoad_(fastWriteIndex_ >= readOffset
                             ? fastWriteIndex_ - readOffset
                             : kCapacitySamples + fastWriteIndex_ - readOffset);
    }

    float readFastCubic_(float delaySamples) const noexcept
    {
        constexpr size_t kLast = kCapacitySamples - 1;
        constexpr float kMaxDelay = static_cast<float>(kLast);
        delaySamples = delaySamples < 0.0f ? 0.0f : (delaySamples > kMaxDelay ? kMaxDelay : delaySamples);
        const auto whole = static_cast<size_t>(delaySamples);
        const float frac = delaySamples - static_cast<float>(whole);
        const size_t next = whole < kLast ? whole + 1 : kLast;
        const size_t after = next < kLast ? next + 1 : kLast;
        return cubic_(frac, fastRead_(whole > 0 ? whole - 1 : 0), fastRead_(whole),
                      fastRead_(next), fastRead_(after));
    }

    // The synced ring's int16 slots occupy the first kSyncCapacitySamples * 2 bytes.
    int16_t syncLoad_(size_t slot) const noexcept
    {
        int16_t value;
        std::memcpy(&value, reinterpret_cast<const unsigned char *>(storage_.data()) + slot * sizeof value,
                    sizeof value);
        return value;
    }

    void syncStore_(size_t slot, int16_t value) noexcept
    {
        std::memcpy(reinterpret_cast<unsigned char *>(storage_.data()) + slot * sizeof value, &value,
                    sizeof value);
    }

    float syncValue_(size_t age) const noexcept
    {
        if (age >= syncValid_) return 0.0f;
        const size_t slot = (syncWriteIndex_ - 1 - age) & (kSyncCapacitySamples - 1);
        return static_cast<float>(syncLoad_(slot)) * (1.0f / 8192.0f);
    }

    float readSyncTap_() const noexcept
    {
        float age = (delaySamples_ - static_cast<float>(syncPhase_ + 1)) /
                    static_cast<float>(kSyncDecimation);
        if (age < 0.0f) age = 0.0f;
        const size_t whole = static_cast<size_t>(age);
        const float frac = age - static_cast<float>(whole);
        return cubic_(frac, syncValue_(whole > 0 ? whole - 1 : 0), syncValue_(whole),
                      syncValue_(whole + 1), syncValue_(whole + 2));
    }

    // Shared by both rings (see the note at the top of the class). Words, not
    // floats, so neither view has to pretend the bytes were last written as its type.
    std::array<uint32_t, kCapacitySamples> storage_{};
    size_t fastWriteIndex_ = 0;
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
