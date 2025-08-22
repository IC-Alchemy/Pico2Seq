/*
Copyright (c) 2020 Electrosmith, Corp

Use of this source code is governed by an MIT-style
license that can be found in the LICENSE file or at
https://opensource.org/licenses/MIT.
*/

#pragma once
#ifndef DSY_HERMDELAY_H
#define DSY_HERMDELAY_H

#include <stdint.h>
#include <math.h>
#include <string.h>

#ifdef __cplusplus

namespace daisysp
{
/** Variable delay line with Hermite interpolation, aligned to project style.
    Maintains existing features (process/reset/getDelayedSample) while adding
    Init and DelayLine-like methods for compatibility.
*/
class VariableDelay
{
  public:
    VariableDelay()
    : buffer_(nullptr),
      buffer_size_(0),
      write_idx_(0),
      max_delay_sec_(0.0f),
      sample_rate_(48000.0f),
      frac_(0.0f),
      delay_samples_(1)
    {
    }

    VariableDelay(float maxDelayTimeInSeconds, float sampleRate)
    : VariableDelay()
    {
        Init(maxDelayTimeInSeconds, sampleRate);
    }

    ~VariableDelay()
    {
        if(buffer_)
        {
            delete[] buffer_;
            buffer_ = nullptr;
        }
    }

    /** Initializes the delay line and allocates internal buffer. */
    void Init(float maxDelayTimeInSeconds, float sampleRate)
    {
        if(buffer_)
        {
            delete[] buffer_;
            buffer_ = nullptr;
        }
        sample_rate_  = sampleRate;
        max_delay_sec_ = maxDelayTimeInSeconds;
        buffer_size_  = (int)(maxDelayTimeInSeconds * sampleRate) + 4;
        if(buffer_size_ < 4)
            buffer_size_ = 4;
        buffer_ = new float[buffer_size_];
        memset(buffer_, 0, buffer_size_ * sizeof(float));
        write_idx_      = 0;
        frac_           = 0.0f;
        delay_samples_  = 1;
    }

    /** Clears buffer, resets write pointer and default delay. */
    void Reset()
    {
        if(buffer_)
            memset(buffer_, 0, buffer_size_ * sizeof(float));
        write_idx_     = 0;
        frac_          = 0.0f;
        delay_samples_ = 1;
    }

    // Backward-compatible alias
    inline void reset() { Reset(); }

    /** Sets the delay time in samples (integer). */
    inline void SetDelay(size_t delay)
    {
        frac_          = 0.0f;
        delay_samples_ = (delay < (size_t)buffer_size_) ? (int)delay : (buffer_size_ - 1);
    }

    /** Sets the delay time in samples (floating point). */
    inline void SetDelay(float delay)
    {
        int32_t int_delay = static_cast<int32_t>(delay);
        frac_              = delay - static_cast<float>(int_delay);
        delay_samples_     = int_delay < buffer_size_ ? int_delay : (buffer_size_ - 1);
    }

    /** Writes a sample into the delay line and advances the write pointer (reverse index like DelayLine). */
    inline void Write(const float sample)
    {
        if(!buffer_)
            return;
        buffer_[write_idx_] = sample;
        write_idx_          = (write_idx_ - 1 + buffer_size_) % buffer_size_;
    }

    /** Reads the sample at the current SetDelay() with linear interpolation. */
    inline float Read() const
    {
        if(!buffer_)
            return 0.0f;
        float a = buffer_[(write_idx_ + delay_samples_) % buffer_size_];
        float b = buffer_[(write_idx_ + delay_samples_ + 1) % buffer_size_];
        return a + (b - a) * frac_;
    }

    /** Read from a specific delay in samples with linear interpolation. */
    inline float Read(float delay) const
    {
        if(!buffer_)
            return 0.0f;
        int32_t delay_integral   = static_cast<int32_t>(delay);
        float   delay_fractional = delay - static_cast<float>(delay_integral);
        const float a = buffer_[(write_idx_ + delay_integral) % buffer_size_];
        const float b = buffer_[(write_idx_ + delay_integral + 1) % buffer_size_];
        return a + (b - a) * delay_fractional;
    }

    /** Hermite-interpolated read from a specific delay in samples. */
    inline float ReadHermite(float delay) const
    {
        if(!buffer_)
            return 0.0f;
        int32_t delay_integral = static_cast<int32_t>(delay);
        float   f              = delay - static_cast<float>(delay_integral);

        int32_t t  = (write_idx_ + delay_integral + buffer_size_);
        const float xm1 = buffer_[(t - 1) % buffer_size_];
        const float x0  = buffer_[(t) % buffer_size_];
        const float x1  = buffer_[(t + 1) % buffer_size_];
        const float x2  = buffer_[(t + 2) % buffer_size_];
        const float c   = (x1 - xm1) * 0.5f;
        const float v   = x0 - x1;
        const float w   = c + v;
        const float a   = w + v + (x2 - x0) * 0.5f;
        const float b_n = w + a;
        return (((a * f) - b_n) * f + c) * f + x0;
    }

    /** Allpass interpolation helper matching DelayLine API. */
    inline float Allpass(const float sample, size_t delay, const float coefficient)
    {
        if(!buffer_)
            return 0.0f;
        float read  = buffer_[(write_idx_ + (int)delay) % buffer_size_];
        float write = sample + coefficient * read;
        buffer_[write_idx_] = write;
        write_idx_          = (write_idx_ - 1 + buffer_size_) % buffer_size_;
        return -write * coefficient + read;
    }

    /** Process a single sample with variable delay time (in seconds)
        with optional feedback and dry/wet mix. Backward-compatible API. */
    float process(float input, float delayTimeInSeconds, float feedback = 0.0f, float mix = 0.5f)
    {
        return Process(input, delayTimeInSeconds, feedback, mix);
    }

    /** Project-style Process variant (same behavior as process). */
    float Process(float input, float delayTimeInSeconds, float feedback = 0.0f, float mix = 0.5f)
    {
        if(!buffer_)
            return input;
        if(delayTimeInSeconds > max_delay_sec_)
            delayTimeInSeconds = max_delay_sec_;

        float delay_samples = delayTimeInSeconds * sample_rate_;

        // Write input with feedback
        float delayedSample = GetDelayedSample(delay_samples);
        buffer_[write_idx_] = input + feedback * delayedSample;

        // Increment write index (forward style for this method to match original process())
        write_idx_ = (write_idx_ + 1) % buffer_size_;

        // Output delayed sample
        delayedSample = GetDelayedSample(delay_samples);
        return (1.0f - mix) * input + mix * delayedSample;
    }

    /** Optional convenience: set sample rate after Init. */
    inline void SetSampleRate(float sr) { sample_rate_ = sr; }

    /** Backward-compatible accessor. */
    inline float getDelayedSample(float delaySamples) { return GetDelayedSample(delaySamples); }

  private:
    // Internal Hermite helper used by GetDelayedSample
    inline float Hermite3(float x, float y0, float y1, float y2, float y3) const
    {
        // 4-point, 3rd-order Hermite (x-form)
        float c0    = y1;
        float c1    = 0.5f * (y2 - y0);
        float y0my1 = y0 - y1;
        float c3    = (y1 - y2) + 0.5f * (y3 - y0my1 - y2);
        float c2    = y0my1 + c1 - c3;
        return ((c3 * x + c2) * x + c1) * x + c0;
    }

    // Internal read with Hermite interpolation (seconds -> samples provided as float)
    inline float GetDelayedSample(float delaySamples) const
    {
        if(!buffer_)
            return 0.0f;

        int   readIndex = write_idx_ - (int)delaySamples;
        float fraction  = delaySamples - (int)delaySamples;

        while(readIndex < 0)
            readIndex += buffer_size_;

        int index0 = (readIndex - 1 + buffer_size_) % buffer_size_;
        int index1 = (readIndex) % buffer_size_;
        int index2 = (readIndex + 1) % buffer_size_;
        int index3 = (readIndex + 2) % buffer_size_;

        return Hermite3(fraction, buffer_[index0], buffer_[index1], buffer_[index2], buffer_[index3]);
    }

  private:
    float* buffer_;
    int    buffer_size_;
    int    write_idx_;
    float  max_delay_sec_;
    float  sample_rate_;
    float  frac_;
    int    delay_samples_;
};

} // namespace daisysp

#endif // __cplusplus
#endif // DSY_HERMDELAY_H