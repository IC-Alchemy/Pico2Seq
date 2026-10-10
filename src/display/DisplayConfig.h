#pragma once

#include <cstdint>

// One display selector. 0 = existing SH1106 128x64 I2C OLED.
//                       1 = 3.5-inch ST7796 320x480 SPI TFT, portrait.
// A build flag may override it without editing the sketch.
#ifndef PICO2SEQ_DISPLAY
#define PICO2SEQ_DISPLAY 0
#endif

namespace DisplayConfig {
enum class Panel : uint8_t { SH1106 = 0, ST7796 = 1 };
inline constexpr Panel kPanel = static_cast<Panel>(PICO2SEQ_DISPLAY);
static_assert(kPanel == Panel::SH1106 || kPanel == Panel::ST7796,
              "PICO2SEQ_DISPLAY must be 0 (SH1106) or 1 (ST7796)");
inline constexpr bool kLargePanel = kPanel == Panel::ST7796;
// Conservative starting speed, not a claim about the module's maximum.
inline constexpr uint32_t kSpiHz = 24000000;
inline constexpr uint8_t kRotation = 0; // 0 or 2 keeps the portrait layout
inline constexpr bool kInverted = false; // change only if the module requires it
}
