#ifndef LED_CONSTANTS_H
#define LED_CONSTANTS_H

#include <FastLED.h>

// LED + OLED constants: 8x4 WS2812B stage mirror, theme palettes, SH1106 page math.
// Player view: hue = which voice, brightness = gate on/off, white bloom = sounding step.

namespace LEDConstants {
  // 8x4 panel = two voice-pair bands; mirrors the 4x8 touch matrix.
  static constexpr uint8_t MATRIX_WIDTH = 8;
  static constexpr uint8_t MATRIX_HEIGHT = 4; // 8x4 panel: mirrors the 4x8 touch matrix
  static constexpr uint8_t MATRIX_DATA_PIN = 1;
  static constexpr uint8_t MATRIX_TOTAL_LEDS = MATRIX_WIDTH * MATRIX_HEIGHT;
  static constexpr uint8_t DEFAULT_BRIGHTNESS = 222;

  // Fade/blink cadence (ms); edits linger so the player sees what changed.
  static constexpr float PULSE_FREQUENCY = 0.006f;
  static constexpr uint8_t PULSE_BASE_BRIGHTNESS = 22;
  static constexpr uint8_t PULSE_AMPLITUDE = 188;
  static constexpr uint32_t BREATHING_CYCLE_MS = 2000;
  static constexpr uint32_t BLINK_INTERVAL_MS = 500;
  static constexpr uint32_t VOICE_PARAM_TIMEOUT_MS = 3000;
  static constexpr uint32_t VOICE_PARAM_SETTINGS_TIMEOUT_MS = 5000;

  // Per-frame blend alphas (0-255): higher = snappier.
  static constexpr uint8_t TARGET_SMOOTHING_BLEND_AMOUNT = 180;
  static constexpr uint8_t STANDARD_BLEND_AMOUNT = 166;
  static constexpr uint8_t DIM_BLEND_AMOUNT = 122;
  static constexpr uint8_t FADE_BLEND_AMOUNT = 64;
  static constexpr uint8_t SUBTLE_BLEND_AMOUNT = 32;

  // Steady-state levels (0-255) for non-pulsing states.
  static constexpr uint8_t FULL_BRIGHTNESS = 255;
  static constexpr uint8_t HIGH_BRIGHTNESS = 180;
  static constexpr uint8_t MEDIUM_BRIGHTNESS = 128;
  static constexpr uint8_t LOW_BRIGHTNESS = 64;
  static constexpr uint8_t DIM_BRIGHTNESS = 24;
  static constexpr uint8_t SUBTLE_BRIGHTNESS = 6;

  // Band map: pair low voice on rows 0-1, pair high voice on rows 2-3.
  static constexpr uint8_t TOP_HALF_OFFSET = 0;       // Band 0 start (pair low voice)
  static constexpr uint8_t BOTTOM_HALF_OFFSET = 16;   // Band 1 start (pair high voice; touch rows 2-3)
  static constexpr uint8_t VOICE_PAIR_SEPARATION = 1; // Rows between pair bands in an 8x4 matrix
  static constexpr uint8_t MAX_STEP_BUTTONS = 16;

  // Extra tint marking a param lane's own playhead when it differs from Gate.
  static constexpr uint8_t POLYRHYTHM_INTENSITY = 64;
  static constexpr size_t POLYRHYTHM_PARAM_COUNT = 3;
}

namespace LEDColors {
  // Dimmed whites: full white blinds at stage distance; 66 reads as paper.
  static constexpr CRGB BLACK = CRGB::Black;
  static constexpr CRGB WHITE = CRGB(66, 66, 66);

  // Idle breathing wash (stopped transport).
  static constexpr CRGB BREATHING_BLUE_BASE = CRGB(0, 0, 94);
  static constexpr uint8_t BREATHING_MIN_INTENSITY = 16;
  static constexpr uint8_t BREATHING_MAX_INTENSITY = 99;

  // Note/velocity/filter playhead tints (see POLYRHYTHM above).
  static constexpr CRGB POLYRHYTHM_NOTE = CRGB(32, 0, 88);
  static constexpr CRGB POLYRHYTHM_VELOCITY = CRGB(0, 88, 22);   // Green tint
  static constexpr CRGB POLYRHYTHM_FILTER = CRGB(0, 10, 88);     // Blue tint
}

namespace OLEDConstants {
  // SH1106 on I2C0 at 400 kHz; -1 = no reset pin wired.
  static constexpr uint8_t I2C_ADDRESS = 0x3C;
  static constexpr uint8_t SCREEN_WIDTH = 128;
  static constexpr uint8_t SCREEN_HEIGHT = 64;
  static constexpr int8_t RESET_PIN = -1;

  // Boot-splash pacing only; the live screen never delays.
  static constexpr uint32_t BORDER_ANIM_INTERVAL_MS = 80;
  static constexpr uint8_t BORDER_ANIM_PHASES = 8;
  static constexpr uint32_t STARTUP_WIPE_DELAY_MS = 12;
  static constexpr uint32_t STARTUP_BOUNCE_DELAY_MS = 20;
  static constexpr uint32_t STARTUP_SETTLE_DELAY_MS = 300;

  // 128x64 page geometry for commitFrame()'s per-page push.
  static constexpr uint8_t BORDER_THICKNESS = 1;
  static constexpr uint8_t TEXT_MARGIN = 5;
  static constexpr uint8_t LINE_SPACING = 10;
  static constexpr uint8_t HEADER_HEIGHT = 14;
  static constexpr uint8_t PROGRESS_BAR_HEIGHT = 10;
  static constexpr uint8_t STEP_INDICATOR_HEIGHT = 8;
}

#endif // LED_CONSTANTS_H
