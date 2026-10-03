#pragma once

// LoopTiming.h — the arithmetic that ties a loop to the step clock. Pure and
// header-only so the audio engine (Core 1), the Core 0 controller and the host tests
// agree on one definition of "how long is N steps".
//
// A step is a sixteenth note (the sequencer's unit), so at `bpm` one step lasts
// 15 / bpm seconds. The loop sizes the player can choose are 4, 8, 16, 32 and 64
// steps: a quarter-note beat, two beats, a bar, two bars and four bars of 4/4.

#include <cstdint>

namespace LoopTiming {

inline constexpr uint8_t kSizeCount = 5;
inline constexpr uint8_t kSteps[kSizeCount] = {4, 8, 16, 32, 64};
inline constexpr uint8_t kDefaultSizeIndex = 2; // one bar

// Regen: the share of the loop kept on each repeat. 100% never fades; the floor keeps
// the loop around for a few passes instead of vanishing after one.
inline constexpr float kMinRegen = 0.10f;

// Tempo the fader and session loader allow is 45..200; clamp wider than that so a
// stray value can never divide by zero or overflow the frame count.
inline constexpr float kMinBpm = 20.0f;
inline constexpr float kMaxBpm = 300.0f;

// Longest possible loop: 64 steps at kMinBpm at 48 kHz is 2.4 M frames; the engine
// refuses nothing below this, it only has to fit a uint32_t.
inline constexpr uint32_t kMaxFrames = 4u * 1000u * 1000u;

constexpr uint8_t clampIndex(uint8_t index) noexcept
{
    return index < kSizeCount ? index : static_cast<uint8_t>(kSizeCount - 1);
}

constexpr uint8_t stepsForIndex(uint8_t index) noexcept
{
    return kSteps[clampIndex(index)];
}

// Index of an exact size, or the default for anything that is not one.
constexpr uint8_t indexForSteps(uint8_t steps) noexcept
{
    for (uint8_t i = 0; i < kSizeCount; ++i)
        if (kSteps[i] == steps)
            return i;
    return kDefaultSizeIndex;
}

// How often a take can start: every loop boundary for the short loops, every bar
// (16 steps) for the long ones, so the wait for a 64-step loop is at most one bar.
constexpr uint8_t quantizeSteps(uint8_t loopSteps) noexcept
{
    return loopSteps < 16 ? (loopSteps == 0 ? uint8_t{1} : loopSteps) : uint8_t{16};
}

// Frames in `steps` steps at `bpm`, rounded to the nearest frame and clamped to
// 1..kMaxFrames. Non-finite or out-of-range tempos clamp to the supported span.
inline uint32_t loopFrames(uint8_t steps, float bpm, float sampleRate) noexcept
{
    if (!(bpm > kMinBpm))
        bpm = kMinBpm;
    if (bpm > kMaxBpm)
        bpm = kMaxBpm;
    if (!(sampleRate > 0.0f))
        sampleRate = 48000.0f;
    const float frames = static_cast<float>(steps) * 15.0f * sampleRate / bpm;
    if (!(frames >= 1.0f))
        return 1u;
    if (frames >= static_cast<float>(kMaxFrames))
        return kMaxFrames;
    return static_cast<uint32_t>(frames + 0.5f);
}

} // namespace LoopTiming
