#ifndef LEDMATRIX_H
#define LEDMATRIX_H

#include <Arduino.h>
#include <FastLED.h>
#include "LEDConstants.h"

/**
 * @brief LED Matrix Controller for 16x16 WS2812B Matrix
 *
 * Manages a 16x16 WS2812B LED matrix with consistent color management
 * and hardware abstraction. Provides methods for individual LED control,
 * bulk operations, and direct access for advanced use cases.
 */
class LEDMatrix {
public:
  // Matrix dimensions and hardware configuration
  static constexpr uint8_t WIDTH = LEDConstants::MATRIX_WIDTH;
  static constexpr uint8_t HEIGHT = LEDConstants::MATRIX_HEIGHT;
  static constexpr uint8_t DATA_PIN = LEDConstants::MATRIX_DATA_PIN;
  static constexpr uint16_t TOTAL_LEDS = LEDConstants::MATRIX_TOTAL_LEDS;

  /**
   * @brief Constructor - initializes LED array to black
   */
  LEDMatrix();

  /**
   * @brief Initialize the LED matrix hardware
   * @param brightness Initial brightness level (0-255, default from constants)
   */
  void begin(uint8_t brightness = LEDConstants::DEFAULT_BRIGHTNESS);

  /**
   * @brief Set color of individual LED at matrix coordinates
   * @param x X coordinate (0 to WIDTH-1)
   * @param y Y coordinate (0 to HEIGHT-1)
   * @param color CRGB color value
   */
  void setLED(int x, int y, const CRGB& color);

  /**
   * @brief Get color of individual LED at matrix coordinates
   */
  CRGB getLED(int x, int y) const;

  /**
   * @brief Set all LEDs to the same color
   * @param color CRGB color value to apply to all LEDs
   */
  void setAll(const CRGB& color);

  /**
   * @brief Direct reference access to hardware pixel for blending
   */
  CRGB& pixelAt(int x, int y);

  /**
   * @brief Update physical LED matrix with current color data
   */
  void show();

  /**
   * @brief Clear all LEDs (set to black)
   */
  void clear();

  /**
   * @brief Get direct access to underlying LED array (hardware order)
   * @warning Hardware order uses columns-major zigzag mapping.
   */
  CRGB* getLeds();

private:
  // Convert (x,y) to hardware linear index for TOP+LEFT+COLUMNS+ZIGZAG layout
  inline int indexForXY(int x, int y) const {
    // Bounds should be checked by callers
    // Columns-major: each column contains HEIGHT LEDs
    // Zigzag: even x go top->bottom, odd x go bottom->top
    int base = x * HEIGHT;
    if ((x & 1) == 0) {
      // even column
      return base + y;
    } else {
      // odd column, reversed
      return base + (HEIGHT - 1 - y);
    }
  }

  CRGB ledArray[TOTAL_LEDS];  // Internal LED color storage (hardware order)
};

#endif // LEDMATRIX_H
