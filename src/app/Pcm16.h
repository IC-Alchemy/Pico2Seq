#pragma once

#include <cmath>
#include <cstdint>
#if defined(__arm__)
#include "RP2350.h"
#endif

namespace AudioSamples
{
// Float mix (–1..1) to 16-bit DAC words. Clipping here is the difference between
// loud-and-clean and harsh digital crunch, so clamp before scaling.
// Core 1 hot path: branchless, no allocation; host build mirrors ARM saturation.
// Keep clamp-then-truncate-then-saturate: rounding would shift quiet tails by 1 LSB.
inline int16_t toPcm16(float sample) noexcept
{
    constexpr float kPcmScale = 32768.0f;
    sample = fminf(1.0f, fmaxf(-1.0f, sample));
    const int32_t scaled = static_cast<int32_t>(sample * kPcmScale);
#if defined(__arm__)
    return static_cast<int16_t>(__SSAT(scaled, 16));
#else
    // Host mirror of ARM __SSAT for the conversion regression tests.
    return static_cast<int16_t>(scaled > INT16_MAX ? INT16_MAX :
                                scaled < INT16_MIN ? INT16_MIN : scaled);
#endif
}

// Interleaved stereo DAC words (L, R, L, R, ...) from two float channels. Each
// channel is converted on its own, so a hard-panned or opposite-polarity image
// reaches the DAC intact. Always inlined so it lands in the caller's (SRAM)
// section on Core 1 rather than becoming a flash-resident copy.
#if defined(__GNUC__)
__attribute__((always_inline))
#endif
inline void interleavePcm16(const float *left, const float *right, int16_t *out,
                            uint32_t frames) noexcept
{
    for (uint32_t i = 0; i < frames; ++i)
    {
        out[2 * i] = toPcm16(left[i]);
        out[2 * i + 1] = toPcm16(right[i]);
    }
}
}
