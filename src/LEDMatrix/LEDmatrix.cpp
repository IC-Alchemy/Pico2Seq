#include "ledMatrix.h"

/**
 * @brief LEDMatrix implementation for 16x16 WS2812B matrix
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
  
  // Clear display and show initial state
  clear();
  show();
}

void LEDMatrix::setLED(int x, int y, const CRGB& color) {
  // Bounds checking for matrix coordinates
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) {
    return;
  }
  
  // Map to hardware index for TOP+LEFT+COLUMNS+ZIGZAG
  int base = x * HEIGHT;
  const int idx = ((x & 1) == 0) ? (base + y) : (base + (HEIGHT - 1 - y));
  ledArray[idx] = color;
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

CRGB LEDMatrix::getLED(int x, int y) const {
  // Bounds safety: return black for out-of-range coordinates
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) {
    return CRGB::Black;
  }
  int idx = indexForXY(x, y);
  return ledArray[idx];
}

CRGB& LEDMatrix::pixelAt(int x, int y) {
  // Return direct reference to internal pixel for fast in-place updates.
  // Caller is expected to provide valid coordinates; keep this fast for blending.
  int idx = indexForXY(x, y);
  return ledArray[idx];
}
