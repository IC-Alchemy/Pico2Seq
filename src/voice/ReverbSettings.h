#pragma once

// ReverbSettings.h — the master reverb's user-facing controls as plain data.
// Portable (no DSP, Arduino or UI includes) so the audio adapter, the session
// codec and the control surface share one definition of range and default.
// Units are the ones the performer thinks in: seconds, Hz, 0..1.

#include <cstdint>
#include <cstring>

namespace ReverbParams
{
// Defaults are an audition starting point, not measured optima. Mix defaults to
// zero so an upgraded project sounds exactly as before.
inline constexpr float kMixMin = 0.0f;
inline constexpr float kMixMax = 1.0f;
inline constexpr float kMixDefault = 0.0f;

// Low-frequency T60. 0.1 s..1000 s is DarkReverb's own clamp.
inline constexpr float kDecayMin = 0.1f;
inline constexpr float kDecayMax = 1000.0f;
inline constexpr float kDecayDefault = 20.0f;

// One-pole lowpass in each tank stage. DarkReverb clamps to 100 Hz..0.45 * tank
// rate (10.8 kHz at 48 kHz), so the same range is used everywhere.
inline constexpr float kDampingMin = 100.0f;
inline constexpr float kDampingMax = 10800.0f;
inline constexpr float kDampingDefault = 3000.0f;

// Input high-pass that keeps rumble out of long decays.
inline constexpr float kLowCutMin = 10.0f;
inline constexpr float kLowCutMax = 1000.0f;
inline constexpr float kLowCutDefault = 40.0f;

inline constexpr float kDiffusionMin = 0.0f;
inline constexpr float kDiffusionMax = 1.0f;
inline constexpr float kDiffusionDefault = 0.8f;

inline constexpr float kModDepthMin = 0.0f;
inline constexpr float kModDepthMax = 1.0f;
inline constexpr float kModDepthDefault = 0.5f;

inline constexpr float kModRateMin = 0.01f;
inline constexpr float kModRateMax = 5.0f;
inline constexpr float kModRateDefault = 0.5f;

// 0 = mono wet, 1 = natural, 2 = exaggerated.
inline constexpr float kWidthMin = 0.0f;
inline constexpr float kWidthMax = 2.0f;
inline constexpr float kWidthDefault = 1.0f;

// True for every finite value. Works from the bit pattern on purpose: the
// firmware builds with -ffast-math (-ffinite-math-only), and with those flags
// arm-none-eabi-gcc 16.1 compiles std::isfinite(x) to a constant "true" (and host
// GCC 13.3 makes std::isnan(NaN) false), so the library predicates cannot be used
// to reject a corrupt NaN.
inline bool finite(float value) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return (bits & 0x7F800000u) != 0x7F800000u;
}

// NaN and infinities fall back to the default; finite values clamp into range.
inline float sanitize(float value, float low, float high, float fallback) noexcept
{
    if (!finite(value))
        return fallback;
    return value < low ? low : (value > high ? high : value);
}

// In range, finite and unmodified (what a loader accepts without clamping).
inline bool inRange(float value, float low, float high) noexcept
{
    return finite(value) && value >= low && value <= high;
}
} // namespace ReverbParams

struct ReverbSettings
{
    float mix = ReverbParams::kMixDefault;
    float decaySeconds = ReverbParams::kDecayDefault;
    float dampingHz = ReverbParams::kDampingDefault;
    float lowCutHz = ReverbParams::kLowCutDefault;
    float diffusion = ReverbParams::kDiffusionDefault;
    float modDepth = ReverbParams::kModDepthDefault;
    float modRateHz = ReverbParams::kModRateDefault;
    float width = ReverbParams::kWidthDefault;
    // Performance state, never persisted: a restored project starts unfrozen.
    bool freeze = false;

    // Every field forced into range (non-finite -> default).
    ReverbSettings sanitized() const noexcept
    {
        using namespace ReverbParams;
        ReverbSettings out;
        out.mix = sanitize(mix, kMixMin, kMixMax, kMixDefault);
        out.decaySeconds = sanitize(decaySeconds, kDecayMin, kDecayMax, kDecayDefault);
        out.dampingHz = sanitize(dampingHz, kDampingMin, kDampingMax, kDampingDefault);
        out.lowCutHz = sanitize(lowCutHz, kLowCutMin, kLowCutMax, kLowCutDefault);
        out.diffusion = sanitize(diffusion, kDiffusionMin, kDiffusionMax, kDiffusionDefault);
        out.modDepth = sanitize(modDepth, kModDepthMin, kModDepthMax, kModDepthDefault);
        out.modRateHz = sanitize(modRateHz, kModRateMin, kModRateMax, kModRateDefault);
        out.width = sanitize(width, kWidthMin, kWidthMax, kWidthDefault);
        out.freeze = freeze;
        return out;
    }

    bool operator==(const ReverbSettings &other) const noexcept
    {
        return mix == other.mix && decaySeconds == other.decaySeconds &&
               dampingHz == other.dampingHz && lowCutHz == other.lowCutHz &&
               diffusion == other.diffusion && modDepth == other.modDepth &&
               modRateHz == other.modRateHz && width == other.width && freeze == other.freeze;
    }
    bool operator!=(const ReverbSettings &other) const noexcept { return !(*this == other); }
};
