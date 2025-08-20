// SmoothedValue: real-time parameter smoothing for audio DSP.
// WHY: Abrupt parameter changes (e.g., knob moves, CV jumps) produce zipper noise and clicks
// when applied directly per-sample. A lightweight smoother generates a sample-by-sample
// transition from the current value to a target value with deterministic cost and no dynamic
// allocation, suitable for audio callbacks.
//
// Two modes cover the common musical use-cases:
// - Exponential (one-pole lowpass): natural, asymptotic glide that feels like an RC filter.
//   The time parameter here is specified as T60 (time to decay ~60 dB to 0.1% of error),
//   which is musically meaningful and stable across ranges. It avoids divide-by-delta issues
//   and automatically adapts to any step size.
// - Linear: constant-rate ramp that reaches the target in exactly `time_s` seconds. Useful
//   when you need predictable arrival time (e.g., parameter morphs or deterministic fades).
//
// Performance/embedded considerations:
// - All methods are trivial/inline and use only stack variables to remain safe in the audio thread.
// - No dynamic allocation; fixed cost per sample (one add/mul in exponential, one add/compare in linear).
// - `Set()` is O(1) and safe to call at control-rate or audio-rate.
#pragma once
#ifndef SMOOTH_H
#define SMOOTH_H

#include <cstdint>
#include "dsp.h"  // onepole_coef_t60(): maps a T60 time to a one-pole smoothing coefficient


class SmoothedValue {

public:

    enum class SmoothType {
        Exponential, // One-pole lowpass step response; asymptotically approaches target with T60 timing
        Linear       // Constant-rate ramp; guaranteed to reach target in exactly `time_s`
    };

    SmoothedValue() = default;
    ~SmoothedValue() = default;

    // Initialize with sample rate, smoothing time and type.
    // WHY default time 50 ms? It's a commonly used anti-zipper value that smooths control steps
    // without feeling sluggish for typical parameter moves.
    void Init(
        const float sample_rate, 
        const float time_s = 0.05f,
        const SmoothType smooth_type = SmoothType::Exponential
    )
    {
        sample_rate_    = sample_rate;
        c_              = 0.0f;     // In exponential mode: alpha (0..1). In linear mode: per-sample increment.
        target_         = 0.0f;
        value_          = 0.0f;
        smooth_type_    = smooth_type;

        SetTime(time_s); // Compute `c_` from time and sample rate. See SetTime() for details.
    }

    // Update sample rate (e.g., host or device changed SR). We recompute the coefficient to
    // preserve the same perceived time constant.
    void SetSampleRate(const float sample_rate)
    {
        sample_rate_ = sample_rate;
        SetTime(time_);
    }

    // Advance the smoother by one sample and return the new value.
    // - Exponential: value += alpha * (target - value); predictable, click-free one-pole.
    // - Linear: add a fixed per-sample step, then clamp when we cross the target to avoid overshoot.
    inline float Process()
    {
        switch (smooth_type_) {
            case SmoothType::Exponential:
                // one pole lowpass (alpha is chosen from T60 via util::onepole_coef_t60)
                value_ += c_ * (target_ - value_);
                break;
            case SmoothType::Linear:
                if (value_ != target_)
                {
                    value_ += c_; // Move at constant rate towards target
                    if (hasReachedLinearTarget())
                    {
                        // WHY clamp? Step size may not divide the remaining distance evenly; clamping
                        // ensures we land exactly on the target and do not overshoot or oscillate.
                        value_ = target_;
                    }
                }
                break;
        }
        return value_;
    }

    // Get last value without applying new smoothing. Useful when you want the current state
    // but do not want to advance time (e.g., out-of-band queries).
    inline float Get() const
    {
        return value_;
    }

    // Set a new target. If `immediate` is true, jump instantly (bypass smoothing), which is
    // sometimes needed for hard resets. Otherwise, begin smoothing from the current `value_`.
    inline void Set(const float target, const bool immediate = false)
    {
        target_ = target;
        if (immediate)
        {
            value_ = target; // Hard set: no transition
        }
        // WHY only recompute `c_` here for Linear? In exponential mode, the coefficient depends on time and SR
        // but not on the magnitude of the step; in linear mode, `c_` is the per-sample increment and depends on
        // the current delta (target - value) to hit the target exactly in `time_` seconds.
        if (smooth_type_ == SmoothType::Linear)
        {
            c_ = (time_ == 0.0f) ? 0.0f : (target_ - value_) / (time_ * sample_rate_);
        }
    }

    // Update the transition time. For exponential mode we convert the musician-friendly T60
    // (time to reduce error by 60 dB, i.e., to 0.1%) to the standard one-pole coefficient.
    // For linear mode we recompute the per-sample increment so that the remaining distance is
    // traversed in the new time.
    inline void SetTime(const float time_s)
    {
        time_ = time_s;
        switch (smooth_type_) {
            case SmoothType::Exponential:
                // `onepole_coef_t60(time, sr)` maps T60 -> alpha. T60≈ln(1000)*tau; the utility uses
                // the 1/ln(1000) constant (~0.14476) internally to get a stable alpha.
                c_ = onepole_coef_t60(time_s, sample_rate_);
                break;
            case SmoothType::Linear:
                // Per-sample increment (signed), so we reach exactly in `time_` seconds.
                // time_ == 0 implies instantaneous change => zero step to avoid INF.
                c_ = (time_ == 0.0f) ? 0.0f : (target_ - value_) / (time_ * sample_rate_);
                break;
        }
    }

    inline float GetTime() const { return time_; }

private:
    SmoothType smooth_type_;
    float sample_rate_, time_;
    float c_;             // Exponential: alpha (0..1). Linear: per-sample increment (signed).
    float target_, value_;// Target we are moving toward, and current smoothed value.

    // Determines if, given the current step direction and remaining distance, the linear ramp
    // has reached or crossed the target. Also returns true for time_ == 0 to force an immediate end.
    inline bool hasReachedLinearTarget() const
    {
        return 
            (time_ == 0.0f) ||
            (c_ > 0.0f && value_ >= target_) ||
            (c_ < 0.0f && value_ <= target_);
    }
};



#endif