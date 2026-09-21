#include "ledMatrix.h"

// LEDmatrix.cpp — 8x4 WS2812B driver (Core 0): thin FastLED wrapper.
// Player view: the stage mirror — hue per voice, brightness per gate, white
// bloom on the sounding step. Pushing pixels (show()) is Core 0 only; the
// per-frame colors are computed in LEDMatrixFeedback.cpp.

LEDMatrix::LEDMatrix() {
  clear();
}

void LEDMatrix::begin(uint8_t brightness) {
  Serial.print("LEDMatrix: Initializing with brightness: ");
  Serial.println(brightness);
  
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(ledArray, TOTAL_LEDS);
  FastLED.setBrightness(brightness);
  // Dithering stays at FastLED's BINARY_DITHER default. It is what keeps the
  // dim UI colours (gate-off pads at 1/16 of their hue, the edit-mode blues)
  // visible at all below a global brightness of 255; disabling it rounded them
  // to black and turned every blend step into a hard on/off flash.
  
  clear();
  show();
}

void LEDMatrix::setLED(int x, int y, const CRGB& color) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) {
    return;
  }
  
  const int linearIndex = x + (y * WIDTH);
  ledArray[linearIndex] = color;
}

void LEDMatrix::setAll(const CRGB& color) {
  for (int i = 0; i < TOTAL_LEDS; ++i) {
    ledArray[i] = color;
  }
}

void LEDMatrix::show() {
  FastLED.show();
}

void LEDMatrix::clear() {
  setAll(LEDColors::BLACK);
}

CRGB* LEDMatrix::getLeds() {
  return ledArray;
}
