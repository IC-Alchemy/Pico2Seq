#ifndef LEDMATRIX_H
#define LEDMATRIX_H

#include <Arduino.h>
#include <FastLED.h>
#include "LEDConstants.h"

// ledMatrix.h — 8x4 WS2812B stage mirror (Core 0, FastLED).
// Framebuffer only: set colors, then show() pushes. Out-of-range setLED is a
// safe no-op so renderers can skip their own clipping.
class LEDMatrix {
public:
  // 8x4 geometry and data pin; keep in step with LEDConstants.
  static constexpr uint8_t WIDTH = LEDConstants::MATRIX_WIDTH;
  static constexpr uint8_t HEIGHT = LEDConstants::MATRIX_HEIGHT;
  static constexpr uint8_t DATA_PIN = LEDConstants::MATRIX_DATA_PIN;
  static constexpr uint8_t TOTAL_LEDS = LEDConstants::MATRIX_TOTAL_LEDS;

  LEDMatrix();

  // Arm FastLED; brightness 0-255 (default suits stage visibility).
  void begin(uint8_t brightness = LEDConstants::DEFAULT_BRIGHTNESS);

  // One pad, x 0-7 / y 0-3; out of range is a no-op.
  void setLED(int x, int y, const CRGB& color);

  // All pads at once (clears, washes).
  void setAll(const CRGB& color);

  // Push staged colors to the strip.
  void show();

  // Black-out without pushing (call show() to display).
  void clear();

  // Raw framebuffer for the feedback renderer; bypasses bounds checking.
  CRGB* getLeds();

private:
  CRGB ledArray[TOTAL_LEDS];  // staged frame, pushed by show()
};

#endif // LEDMATRIX_H
