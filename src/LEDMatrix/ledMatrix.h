#ifndef LEDMATRIX_H
#define LEDMATRIX_H

#include <Arduino.h>
#include <FastLED.h>
#include "LEDConstants.h"

/**
 * @brief LED Matrix Controller for 8x8 WS2812B Matrix
 * 
 * Manages an 8x8 WS2812B LED matrix with consistent color management
 * and hardware abstraction. Provides methods for individual LED control,
 * bulk operations, and direct access for advanced use cases.
 */
class LEDMatrix {
public:
  // Matrix dimensions and hardware configuration
  static constexpr uint8_t WIDTH = LEDConstants::MATRIX_WIDTH;
  static constexpr uint8_t HEIGHT = LEDConstants::MATRIX_HEIGHT;
  static constexpr uint8_t DATA_PIN = LEDConstants::MATRIX_DATA_PIN;
  static constexpr uint32_t TOTAL_LEDS = LEDConstants::MATRIX_TOTAL_LEDS;

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
   * @brief Set all LEDs to the same color
   * @param color CRGB color value to apply to all LEDs
   */
  void setAll(const CRGB& color);

  /**
   * @brief Update physical LED matrix with current color data
   */
  void show();

  /**
   * @brief Clear all LEDs (set to black)
   */
  void clear();

  /**
   * @brief Get direct access to LED array for advanced operations
   * @return Pointer to internal CRGB array
   * @warning Use with caution - direct manipulation bypasses bounds checking
   */
  CRGB* getLeds();

private:
  CRGB ledArray[TOTAL_LEDS];  // Internal LED color storage
};

#endif // LEDMATRIX_H
