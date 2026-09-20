#include "ledMatrix.h"

/**
 * @brief LEDMatrix implementation for 8x8 WS2812B matrix
 * 
 * Provides hardware abstraction and consistent color management
 * for the LED matrix display system.
 */

LEDMatrix::LEDMatrix() {
  clear();
}

void LEDMatrix::begin(uint8_t brightness) {
  Serial.print("LEDMatrix: Initializing with brightness: ");
  Serial.println(brightness);
  
  // Initialize FastLED library with hardware configuration
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(ledArray, TOTAL_LEDS);
  FastLED.setBrightness(brightness);
  // Dithering stays at FastLED's BINARY_DITHER default. It is what keeps the
  // dim UI colours (gate-off pads at 1/16 of their hue, the edit-mode blues)
  // visible at all below a global brightness of 255; disabling it rounded them
  // to black and turned every blend step into a hard on/off flash.
  
  // Clear display and show initial state
  clear();
  show();
}

void LEDMatrix::setLED(int x, int y, const CRGB& color) {
  // Bounds checking for matrix coordinates
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) {
    return;
  }
  
  // Convert 2D coordinates to linear array index
  const int linearIndex = x + (y * WIDTH);
  ledArray[linearIndex] = color;
}

void LEDMatrix::setAll(const CRGB& color) {
  // Efficiently set all LEDs to the same color
  for (int i = 0; i < TOTAL_LEDS; ++i) {
    ledArray[i] = color;
  }
}

void LEDMatrix::show() {
  // Update physical LED matrix with current color data
  FastLED.show();
}

void LEDMatrix::clear() {
  // Set all LEDs to black (off state)
  setAll(LEDColors::BLACK);
}

CRGB* LEDMatrix::getLeds() {
  // Provide direct access to LED array for advanced operations
  return ledArray;
}
