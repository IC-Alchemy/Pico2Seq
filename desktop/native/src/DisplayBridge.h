#pragma once
// Display snapshots for the GUI (LED matrix + OLED), taken at the exact
// points the firmware pushes frames to hardware (FastLED.show() and
// SH1106G.display() via the shims). The GUI polls these at its own pace.

#include <cstddef>
#include <cstdint>

namespace p2s
{
namespace display
{
constexpr size_t kLedCount = 32;   // 8x4 WS2812B matrix
constexpr size_t kOledBytes = (128 * 64) / 8;

// Copy the latest frames; the return value is a monotonically increasing
// frame serial so the GUI can detect changes without memcmp.
uint64_t copyLeds(uint8_t *outRgb);   // kLedCount * 3 bytes
uint64_t copyOled(uint8_t *outBytes); // kOledBytes 1bpp page buffer

uint64_t ledFrameSerial();
uint64_t oledFrameSerial();
}
}
