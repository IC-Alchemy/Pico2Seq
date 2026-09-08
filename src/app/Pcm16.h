#pragma once

#include <cmath>
#include <cstdint>
#if defined(__arm__)
#include "RP2350.h"
#endif

namespace AudioSamples
{
// Preserve the DAC conversion: clamp, truncate toward zero, then saturate.
// Rounding instead would change quiet samples by one least-significant bit.
inline int16_t toPcm16(float sample) noexcept
{
    constexpr float kPcmScale = 32768.0f;
    sample = fminf(1.0f, fmaxf(-1.0f, sample));
    const int32_t scaled = static_cast<int32_t>(sample * kPcmScale);
#if defined(__arm__)
    return static_cast<int16_t>(__SSAT(scaled, 16));
#else
    // Host equivalent of ARM SSAT, for the sample conversion regression tests.
    return static_cast<int16_t>(scaled > INT16_MAX ? INT16_MAX :
                                scaled < INT16_MIN ? INT16_MIN : scaled);
#endif
}
}
