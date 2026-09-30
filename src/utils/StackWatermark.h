#pragma once

// StackWatermark.h — "how much of this stack has never been touched?"
//
// Paint the unused part of a stack with a known pattern at boot, then scan up from
// the stack's low end later: the words that still hold the pattern were never
// reached, so their byte count is the remaining stack headroom (the high-water
// mark). Portable logic only; StackWatermark.cpp binds it to the RP2350 stacks.
// Diagnostic use: Core 0 reports both stacks in the [DIAG MEM] serial line.

#include <cstddef>
#include <cstdint>

namespace StackWatermark
{
inline constexpr uint32_t kPattern = 0xA5A5A5A5u;

// Fills [bottom, limit) with kPattern. `limit` must lie below the live frames
// (lower address, stacks grow down), so nothing painted is in use.
inline void paint(uint32_t *bottom, const uint32_t *limit) noexcept
{
    for (uint32_t *word = bottom; word < limit; ++word)
        *word = kPattern;
}

// Bytes at the low end of [bottom, top) that still hold kPattern: the stack space
// never reached since paint(). Only a run of words the program itself wrote as
// kPattern could make the reading optimistic (by that run's length); ordinary
// stack contents do not form one.
inline size_t untouchedBytes(const uint32_t *bottom, const uint32_t *top) noexcept
{
    const uint32_t *word = bottom;
    while (word < top && *word == kPattern)
        ++word;
    return static_cast<size_t>(word - bottom) * sizeof(uint32_t);
}

// --- Firmware binding (StackWatermark.cpp; RP2350 builds only) ----------------
// Paint from the calling core's entry point, as early as possible: depth used
// before the call is not measured. Core 0 = the setup()/loop() stack, Core 1 =
// the audio stack (setup1()/loop1() and the interrupts that run on that core).
void paintCore0() noexcept;
void paintCore1() noexcept;
// Total size of each core's stack, from the linker's __Stack*/__StackOne* symbols.
size_t sizeCore0() noexcept;
size_t sizeCore1() noexcept;
// Never-reached bytes of each core's stack, or kNotPainted before its paint call
// (Core 1 paints after Core 0 has finished setup, so Core 0 can ask too early).
inline constexpr size_t kNotPainted = static_cast<size_t>(-1);
size_t untouchedCore0() noexcept;
size_t untouchedCore1() noexcept;
} // namespace StackWatermark
