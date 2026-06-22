#ifndef LED_CONSTANTS_H
#define LED_CONSTANTS_H

#include <FastLED.h>

/**
 * @brief LED Matrix and Display Constants
 * 
 * Centralized constants for LED matrix timing, colors, and animation parameters
 * to improve maintainability and consistency across the LED system.
 */

namespace LEDConstants {
  // LED Matrix Hardware Configuration
  static constexpr uint8_t MATRIX_WIDTH = 8;
  static constexpr uint8_t MATRIX_HEIGHT = 8;
  static constexpr uint8_t MATRIX_DATA_PIN = 1;
  static constexpr uint8_t MATRIX_TOTAL_LEDS = MATRIX_WIDTH * MATRIX_HEIGHT;
  static constexpr uint8_t DEFAULT_BRIGHTNESS = 120;

  // LED Animation Timing Constants
  static constexpr float PULSE_FREQUENCY = 0.006f;
  static constexpr uint8_t PULSE_BASE_BRIGHTNESS = 22;
  static constexpr uint8_t PULSE_AMPLITUDE = 188;
  static constexpr uint32_t BREATHING_CYCLE_MS = 2000;
  static constexpr uint32_t BLINK_INTERVAL_MS = 500;
  static constexpr uint32_t VOICE_PARAM_TIMEOUT_MS = 3000;
  static constexpr uint32_t VOICE_PARAM_SETTINGS_TIMEOUT_MS = 5000;

  // LED Color Blending Constants
  static constexpr uint8_t TARGET_SMOOTHING_BLEND_AMOUNT = 180;
  static constexpr uint8_t STANDARD_BLEND_AMOUNT = 166;
  static constexpr uint8_t DIM_BLEND_AMOUNT = 122;
  static constexpr uint8_t FADE_BLEND_AMOUNT = 64;
  static constexpr uint8_t SUBTLE_BLEND_AMOUNT = 32;

  // LED Brightness Scaling Constants
  static constexpr uint8_t FULL_BRIGHTNESS = 200;
  static constexpr uint8_t HIGH_BRIGHTNESS = 180;
  static constexpr uint8_t MEDIUM_BRIGHTNESS = 128;
  static constexpr uint8_t LOW_BRIGHTNESS = 64;
  static constexpr uint8_t DIM_BRIGHTNESS = 32;
  static constexpr uint8_t SUBTLE_BRIGHTNESS = 12;

  // LED Matrix Layout Constants
  static constexpr uint8_t TOP_HALF_OFFSET = 0;
  static constexpr uint8_t BOTTOM_HALF_OFFSET = 24;  // Row 4 start for 8x8 matrix
  static constexpr uint8_t VOICE_PAIR_SEPARATION = 3;  // Rows between voice pairs
  static constexpr uint8_t MAX_STEP_BUTTONS = 16;

  // Polyrhythmic Overlay Constants
  static constexpr uint8_t POLYRHYTHM_INTENSITY = 32;
  static constexpr size_t POLYRHYTHM_PARAM_COUNT = 3;
}

namespace LEDColors {
  // Standard LED Colors for Common Use
  static constexpr CRGB BLACK = CRGB::Black;
  static constexpr CRGB WHITE = CRGB::White;
  
  // Delay Effect Colors
  static constexpr CRGB DELAY_INDICATOR = CRGB(0, 166, 55);
  static constexpr CRGB DELAY_TIME_BASE = CRGB(0, 44, 33);
  static constexpr CRGB DELAY_FEEDBACK_BASE = CRGB(0, 55, 22);
  
  // Breathing Animation Colors
  static constexpr CRGB BREATHING_BLUE_BASE = CRGB(0, 0, 94);
  static constexpr uint8_t BREATHING_MIN_INTENSITY = 16;
  static constexpr uint8_t BREATHING_MAX_INTENSITY = 80;
  
  // Polyrhythmic Overlay Colors
  static constexpr CRGB POLYRHYTHM_NOTE = CRGB(0, 32, 32);      // Cyan tint
  static constexpr CRGB POLYRHYTHM_VELOCITY = CRGB(0, 32, 0);   // Green tint
  static constexpr CRGB POLYRHYTHM_FILTER = CRGB(0, 0, 32);     // Blue tint
}

namespace OLEDConstants {
  // OLED Display Configuration
  static constexpr uint8_t I2C_ADDRESS = 0x3C;
  static constexpr uint8_t SCREEN_WIDTH = 128;
  static constexpr uint8_t SCREEN_HEIGHT = 64;
  static constexpr int8_t RESET_PIN = -1;
  
  // OLED Animation Timing
  static constexpr uint32_t BORDER_ANIM_INTERVAL_MS = 80;
  static constexpr uint8_t BORDER_ANIM_PHASES = 8;
  static constexpr uint32_t STARTUP_WIPE_DELAY_MS = 12;
  static constexpr uint32_t STARTUP_BOUNCE_DELAY_MS = 20;
  static constexpr uint32_t STARTUP_SETTLE_DELAY_MS = 300;
  
  // OLED Layout Constants
  static constexpr uint8_t BORDER_THICKNESS = 1;
  static constexpr uint8_t TEXT_MARGIN = 5;
  static constexpr uint8_t LINE_SPACING = 10;
  static constexpr uint8_t HEADER_HEIGHT = 14;
  static constexpr uint8_t PROGRESS_BAR_HEIGHT = 10;
  static constexpr uint8_t STEP_INDICATOR_HEIGHT = 8;
}

#endif // LED_CONSTANTS_H