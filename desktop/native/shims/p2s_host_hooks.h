#pragma once
// Hooks the device shims call into desktop-owned code (DisplayBridge.cpp).
// Declared with plain types so shim headers stay independent of each other.

#include <cstddef>
#include <cstdint>

namespace p2s
{
namespace host
{
// FastLED.show() equivalent: snapshot the LED frame for GUI polling.
void onFastLedShow(const void *leds, size_t count);

// SH1106 display() equivalent: snapshot the 128x64 1bpp framebuffer.
void onOledDisplay(const uint8_t *buffer1024);
}
}
