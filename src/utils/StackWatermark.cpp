// StackWatermark.cpp — RP2350 binding for StackWatermark.h. Firmware only: the
// host tests cover the paint/scan logic in the header, and this file just names
// the two stacks the linker script reserves.
#if defined(ARDUINO_ARCH_RP2040)

#include "StackWatermark.h"

extern "C"
{
// From the linker script: each stack occupies [Bottom, Top), growing down.
extern char __StackBottom, __StackTop;       // Core 0
extern char __StackOneBottom, __StackOneTop; // Core 1
}

namespace StackWatermark
{
namespace
{
// Keeps the painted region clear of the caller's live frames and of the frame
// this function itself is using.
constexpr uintptr_t kPaintMargin = 128;

// Written once by the painting core, read by Core 0's diagnostics.
volatile bool g_painted0 = false;
volatile bool g_painted1 = false;

bool paintBelowCaller(char *bottom) noexcept
{
    const uintptr_t frame = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    const uintptr_t low = reinterpret_cast<uintptr_t>(bottom);
    if (frame <= low + kPaintMargin)
        return false; // no room to paint safely
    paint(reinterpret_cast<uint32_t *>(bottom),
          reinterpret_cast<const uint32_t *>(frame - kPaintMargin));
    return true;
}
} // namespace

void paintCore0() noexcept { g_painted0 = paintBelowCaller(&__StackBottom); }
void paintCore1() noexcept { g_painted1 = paintBelowCaller(&__StackOneBottom); }

size_t sizeCore0() noexcept { return static_cast<size_t>(&__StackTop - &__StackBottom); }
size_t sizeCore1() noexcept { return static_cast<size_t>(&__StackOneTop - &__StackOneBottom); }

size_t untouchedCore0() noexcept
{
    if (!g_painted0)
        return kNotPainted;
    return untouchedBytes(reinterpret_cast<const uint32_t *>(&__StackBottom),
                          reinterpret_cast<const uint32_t *>(&__StackTop));
}

size_t untouchedCore1() noexcept
{
    if (!g_painted1)
        return kNotPainted;
    return untouchedBytes(reinterpret_cast<const uint32_t *>(&__StackOneBottom),
                          reinterpret_cast<const uint32_t *>(&__StackOneTop));
}
} // namespace StackWatermark

#endif // ARDUINO_ARCH_RP2040
