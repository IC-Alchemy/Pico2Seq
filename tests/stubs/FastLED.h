#pragma once
#include <stdint.h>

// Minimal host stub for the FastLED library: only the CRGB surface that the
// host-compiled headers need (LEDConstants.h, ledMatrix.h,
// LEDMatrixFeedback.h). No controller objects — the LED matrix .cpp files are
// never compiled on the host.
struct CRGB {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    enum HTMLColorCode : uint32_t {
        Black = 0x000000,
        White = 0xFFFFFF,
    };

    constexpr CRGB() = default;
    constexpr CRGB(uint8_t red, uint8_t green, uint8_t blue)
        : r(red), g(green), b(blue) {}
    constexpr CRGB(HTMLColorCode code)
        : r(static_cast<uint8_t>((static_cast<uint32_t>(code) >> 16) & 0xFF)),
          g(static_cast<uint8_t>((static_cast<uint32_t>(code) >> 8) & 0xFF)),
          b(static_cast<uint8_t>(static_cast<uint32_t>(code) & 0xFF)) {}
};
