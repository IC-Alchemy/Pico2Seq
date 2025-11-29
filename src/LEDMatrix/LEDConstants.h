#ifndef LED_CONSTANTS_H
#define LED_CONSTANTS_H

// Canonical LED matrix geometry and helpers — use these constants for all matrix math.

#include <FastLED.h>

/**
 * @brief LED Matrix and Display Constants
 * 
 * Centralized constants for LED matrix timing, colors, and animation parameters
 * to improve maintainability and consistency across the LED system.
 *
 * Migration note:
 * Prefer using LED_INDEX/LED_X/LED_Y and MAX_STEP_BUTTONS instead of raw numeric
 * literals. These helpers make code width/height-agnostic and are safe to use
 * during the migration to variable matrix sizes.
 */

namespace LEDConstants {
  // Canonical matrix geometry (configurable)
  static constexpr uint8_t MATRIX_WIDTH = 16;   // number of columns
  static constexpr uint8_t MATRIX_HEIGHT = 16;  // number of rows
  static constexpr uint16_t MATRIX_TOTAL_LEDS = MATRIX_WIDTH * MATRIX_HEIGHT; // total LEDs (16-bit)
  // Backwards-compatible alias for historical usage
  static constexpr uint16_t MATRIX_PIXELS = MATRIX_TOTAL_LEDS;

  // Hardware configuration
  static constexpr uint8_t MATRIX_DATA_PIN = 1;
  static constexpr uint8_t DEFAULT_BRIGHTNESS = 120;

  // Compile-time sanity checks
  static_assert(MATRIX_WIDTH > 0, "MATRIX_WIDTH must be > 0");
  static_assert(MATRIX_HEIGHT > 0, "MATRIX_HEIGHT must be > 0");
  static_assert(MATRIX_TOTAL_LEDS <= 65535, "Matrix too large for 16-bit indices");

  // Step/voice layout helpers
  // Maximum number of step buttons used by sequencer UI (use this, not raw 16)
  static constexpr uint8_t MAX_STEP_BUTTONS = 16;
  // Steps per visual row (typically equals MATRIX_WIDTH)
  static constexpr uint8_t STEPS_PER_ROW = MATRIX_WIDTH;
  // Number of rows required to show MAX_STEP_BUTTONS
  static constexpr uint8_t STEPS_ROWS = (MAX_STEP_BUTTONS + (STEPS_PER_ROW - 1)) / STEPS_PER_ROW;
  // Small vertical gap between top/bottom voice groups
  static constexpr uint8_t VOICE_PAIR_VERTICAL_GAP = 1;
  // Base row indices for top and bottom voice groups
  static constexpr uint8_t TOP_VOICE_BASE_ROW = 0;
  static constexpr uint8_t BOTTOM_VOICE_BASE_ROW = TOP_VOICE_BASE_ROW + STEPS_ROWS + VOICE_PAIR_VERTICAL_GAP;
  // Base offsets (linear index) for top and bottom voice groups
  static constexpr uint16_t TOP_VOICE_BASE_OFFSET = static_cast<uint16_t>(TOP_VOICE_BASE_ROW) * MATRIX_WIDTH;
  static constexpr uint16_t BOTTOM_VOICE_BASE_OFFSET = static_cast<uint16_t>(BOTTOM_VOICE_BASE_ROW) * MATRIX_WIDTH;

  // Sanity: ensure the computed rows fit in the matrix height
  static_assert(STEPS_ROWS > 0, "STEPS_ROWS must be > 0");
  static_assert((TOP_VOICE_BASE_ROW + STEPS_ROWS) <= MATRIX_HEIGHT, "Not enough rows for top voice layout");
  static_assert((BOTTOM_VOICE_BASE_ROW + STEPS_ROWS) <= MATRIX_HEIGHT, "Not enough rows for bottom voice layout");

  // Inline helpers for index math (prefer these to manual calculations)
  inline constexpr uint16_t LED_INDEX(uint8_t x, uint8_t y) noexcept { return static_cast<uint16_t>(y) * MATRIX_WIDTH + x; }
  inline constexpr uint8_t LED_X(uint16_t index) noexcept { return static_cast<uint8_t>(index % MATRIX_WIDTH); }
  inline constexpr uint8_t LED_Y(uint16_t index) noexcept { return static_cast<uint8_t>(index / MATRIX_WIDTH); }

  // Backwards-compatible constants (preserve existing symbols, mark deprecated)
  // TOP_HALF_OFFSET historically used; keep for compatibility.
  static constexpr uint8_t TOP_HALF_OFFSET = 0;
  // BOTTOM_HALF_OFFSET deprecated: prefer using BOTTOM_VOICE_BASE_OFFSET / LED_INDEX
  [[deprecated("Use BOTTOM_VOICE_BASE_OFFSET or LED_INDEX(x,y) instead")]]
  static constexpr uint16_t BOTTOM_HALF_OFFSET = 24;  // deprecated: row 4 start for 8x8 matrix
  // VOICE_PAIR_SEPARATION deprecated: use VOICE_PAIR_VERTICAL_GAP
  [[deprecated("Use VOICE_PAIR_VERTICAL_GAP instead")]]
  static constexpr uint8_t VOICE_PAIR_SEPARATION = 3;  // deprecated: rows between voice pairs

  // LED Animation Timing Constants (preserved)
  static constexpr float PULSE_FREQUENCY = 0.006f;         
  static constexpr uint8_t PULSE_BASE_BRIGHTNESS = 22;
  static constexpr uint8_t PULSE_AMPLITUDE = 188;
  static constexpr uint32_t BREATHING_CYCLE_MS = 2000;
  static constexpr uint32_t BLINK_INTERVAL_MS = 500;
  static constexpr uint32_t VOICE_PARAM_TIMEOUT_MS = 3000;
  static constexpr uint32_t VOICE_PARAM_SETTINGS_TIMEOUT_MS = 5000;

  // LED Color Blending Constants (preserved)
  static constexpr uint8_t TARGET_SMOOTHING_BLEND_AMOUNT = 180;
  static constexpr uint8_t STANDARD_BLEND_AMOUNT = 166;
  static constexpr uint8_t DIM_BLEND_AMOUNT = 122;
  static constexpr uint8_t FADE_BLEND_AMOUNT = 64;
  static constexpr uint8_t SUBTLE_BLEND_AMOUNT = 32;

  // LED Brightness Scaling Constants (preserved)
  static constexpr uint8_t FULL_BRIGHTNESS = 200;
  static constexpr uint8_t HIGH_BRIGHTNESS = 180;
  static constexpr uint8_t MEDIUM_BRIGHTNESS = 128;
  static constexpr uint8_t LOW_BRIGHTNESS = 64;
  static constexpr uint8_t DIM_BRIGHTNESS = 32;
  static constexpr uint8_t SUBTLE_BRIGHTNESS = 16;

  // Polyrhythmic Overlay Constants (preserved)
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