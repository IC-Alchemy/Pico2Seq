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
}
