#include <Arduino.h>
#include "LEDMatrixFeedback.h"
#include <FastLED.h>
#include <cmath>

#include "ledMatrix.h"
#include "LEDConstants.h"
#include "../sequencer/Sequencer.h"
#include "../ui/UIEventHandler.h"
#include "../ui/ButtonManager.h"
#include "../utils/Debug.h"

/**
 * @brief LED Matrix Feedback Implementation
 * 
 * Provides comprehensive visual feedback for sequencer operations including
 * step gate visualization, parameter editing, settings navigation, and
 * animated effects. Uses smoothed color blending for professional appearance.
 */

// LED matrix layout constants
static constexpr int LED_MATRIX_BOTTOM_HALF_OFFSET = LEDConstants::BOTTOM_HALF_OFFSET;

// Smoothed color buffer for smooth LED transitions
CRGB smoothedTargetColorBuffer[LEDConstants::MATRIX_TOTAL_LEDS];

// Color blending constants from LEDConstants
static constexpr uint8_t TARGET_SMOOTHING_BLEND_AMOUNT = LEDConstants::TARGET_SMOOTHING_BLEND_AMOUNT;

// Define common colors as constants for readability and maintainability
// These will be populated from the activeThemeColors pointer
CRGB current_COLOR_GATE_ON_V1;
CRGB current_COLOR_GATE_OFF_V1;
CRGB current_COLOR_PLAYHEAD_ACCENT;
CRGB current_COLOR_GATE_ON_V2;
CRGB current_COLOR_GATE_OFF_V2;
CRGB current_COLOR_IDLE_BREATHING_BLUE;
CRGB current_COLOR_EDIT_MODE_DIM_BLUE_V1;
CRGB current_COLOR_EDIT_MODE_DIM_BLUE_V2;
CRGB current_COLOR_MOD_NOTE_ACTIVE;
CRGB current_COLOR_MOD_NOTE_INACTIVE;
CRGB current_COLOR_MOD_VELOCITY_ACTIVE;
CRGB current_COLOR_MOD_VELOCITY_INACTIVE;
CRGB current_COLOR_MOD_FILTER_ACTIVE;
CRGB current_COLOR_MOD_FILTER_INACTIVE;
CRGB current_COLOR_MOD_DECAY_ACTIVE;
CRGB current_COLOR_MOD_DECAY_INACTIVE;
CRGB current_COLOR_MOD_OCTAVE_ACTIVE;
CRGB current_COLOR_MOD_OCTAVE_INACTIVE;
CRGB current_COLOR_DEFAULT_ACTIVE;
CRGB current_COLOR_DEFAULT_INACTIVE;
CRGB current_COLOR_MOD_PARAM_MODE_ACTIVE;
CRGB current_COLOR_MOD_PARAM_MODE_INACTIVE;
CRGB current_COLOR_MOD_GATE_MODE_ACTIVE;
CRGB current_COLOR_MOD_GATE_MODE_INACTIVE;
CRGB current_COLOR_RANDOMIZE_FLASH;
CRGB current_COLOR_RANDOMIZE_IDLE;

const LEDThemeColors ALL_THEMES[] = {
  
   // DARK_NOCTIS theme - deep charcoal with cool blue/cyan accents
      {  CRGB(20, 90, 140),   // gateOnV1 - cool cyan-blue (visible on dark)
        CRGB(6, 6, 8),       // gateOffV1 - near-black
        CRGB(40, 55, 160), // gateOnV2 - desaturated light blue accent
        CRGB(8, 4, 10),       // gateOffV2 - very dark maroon-ish
        CRGB(24, 48, 80),    // playheadAccent - deep navy accent
        CRGB(18, 30, 50),    // idleBreathingBlue - muted navy
        CRGB(8, 10, 14),     // editModeDimBlueV1 - very dark slate
        CRGB(10, 14, 18),    // editModeDimBlueV2
        CRGB(100, 140, 160), // modNoteActive - cool desaturated teal
        CRGB(24, 28, 30),    // modNoteInactive
        CRGB(140, 160, 180), // modVelocityActive - pale steel blue
        CRGB(30, 34, 36),    // modVelocityInactive
        CRGB(120, 100, 140), // modFilterActive - muted indigo
        CRGB(24, 18, 24),    // modFilterInactive
        CRGB(160, 120, 90),  // modDecayActive - muted warm contrast
        CRGB(28, 26, 22),    // modDecayInactive
        CRGB(120, 150, 110), // modAttackActive - subdued sage
        CRGB(22, 26, 20),    // modAttackInactive
        CRGB(180, 110, 160), // modOctaveActive - muted magenta accent
        CRGB(20, 12, 16),    // modOctaveInactive
        CRGB(100, 160, 170), // modSlideActive - cool cyan slide
        CRGB(18, 26, 28),    // modSlideInactive
        CRGB(200, 200, 200), // defaultActive - light gray
        CRGB(14, 14, 16),    // defaultInactive - near black
        CRGB(120, 200, 170), // modParamModeActive - soft aqua-green
        CRGB(16, 18, 18),    // modParamModeInactive
        CRGB(160, 140, 110), // modGateModeActive - muted warm highlight
        CRGB(18, 16, 14),    // modGateModeInactive
        CRGB(220, 200, 180), // randomizeFlash - soft warm flash
        CRGB(12, 12, 14)     // randomizeIdle - dark subtle tone
    },
    {
        // DARK_EMBER theme - deep charcoal with warm amber ember accents
        CRGB(200, 120, 60),  // gateOnV1 - ember orange
        CRGB(8, 9, 4),       // gateOffV1 - near-black
        CRGB(255, 180, 110), // gateOnV2 - warm amber highlight
        CRGB(10, 6, 5),      // gateOffV2 - deep dark
        CRGB(40, 24, 18),    // playheadAccent - dark warm accent
        CRGB(28, 22, 20),    // idleBreathingBlue - warm slate for breathing (amber-tinted)
        CRGB(10, 8, 8),      // editModeDimBlueV1 - very dark warm slate
        CRGB(12, 10, 10),    // editModeDimBlueV2
        CRGB(220, 160, 120), // modNoteActive - warm beige
        CRGB(28, 24, 20),    // modNoteInactive
        CRGB(200, 160, 140), // modVelocityActive - soft warm gray
        CRGB(30, 26, 24),    // modVelocityInactive
        CRGB(180, 120, 100), // modFilterActive - muted terracotta
        CRGB(24, 18, 16),    // modFilterInactive
        CRGB(255, 200, 150), // modDecayActive - bright amber
        CRGB(28, 20, 16),    // modDecayInactive
        CRGB(160, 180, 140), // modAttackActive - muted olive
        CRGB(22, 20, 18),    // modAttackInactive
        CRGB(220, 140, 160), // modOctaveActive - soft rose ember
        CRGB(20, 12, 12),    // modOctaveInactive
        CRGB(200, 160, 140), // modSlideActive - warm slide accent
        CRGB(18, 16, 14),    // modSlideInactive
        CRGB(230, 220, 200), // defaultActive - light warm gray
        CRGB(14, 12, 12),    // defaultInactive - near black
        CRGB(255, 200, 170), // modParamModeActive - warm pale
        CRGB(16, 14, 12),    // modParamModeInactive
        CRGB(255, 180, 90),  // modGateModeActive - bright ember highlight
        CRGB(18, 14, 12),    // modGateModeInactive
        CRGB(255, 210, 140), // randomizeFlash - bright warm flash
        CRGB(10, 8, 8)       // randomizeIdle - very dark idle tone
    },
  
    {
        // MODERN theme - muted, high-legibility palette with warm accent
        CRGB(48, 177, 111),  // gateOnV1 - soft teal
        CRGB(3, 8, 10),    // gateOffV1 - muted dark teal
        CRGB(222, 130, 66), // gateOnV2 - warm coral accent
        CRGB(15, 6, 5),    // gateOffV2 - deep muted maroon
        CRGB(20, 24, 66),    // playheadAccent base dark (will be brightened when added)
        CRGB(60, 84, 110),   // idleBreathingBlue - slate blue for breathing
        CRGB(12, 16, 20),    // editModeDimBlueV1 - dim slate
        CRGB(18, 22, 26),    // editModeDimBlueV2 - slightly lighter slate
        CRGB(200, 180, 160), // modNoteActive - soft warm note color
        CRGB(70, 60, 56),    // modNoteInactive - desaturated
        CRGB(180, 200, 220), // modVelocityActive - pale cyan
        CRGB(64, 72, 80),    // modVelocityInactive
        CRGB(140, 120, 160), // modFilterActive - muted mauve
        CRGB(48, 36, 48),    // modFilterInactive
        CRGB(220, 200, 140), // modDecayActive - soft amber
        CRGB(64, 54, 36),    // modDecayInactive
        CRGB(140, 160, 120), // modAttackActive - sage
        CRGB(48, 56, 40),    // modAttackInactive
        CRGB(220, 140, 180), // modOctaveActive - soft pink accent
        CRGB(56, 28, 36),    // modOctaveInactive
        CRGB(160, 200, 200), // modSlideActive - muted cyan-tint slide accent
        CRGB(48, 64, 64),    // modSlideInactive
        CRGB(200, 200, 200), // defaultActive - light gray for active defaults
        CRGB(36, 36, 40),    // defaultInactive - near-black for inactive
        CRGB(180, 220, 200), // modParamModeActive - pale green
        CRGB(40, 48, 44),    // modParamModeInactive
        CRGB(240, 200, 160), // modGateModeActive - warm highlight
        CRGB(56, 48, 40),    // modGateModeInactive
        CRGB(255, 210, 170), // randomizeFlash - bright warm flash
        CRGB(40, 44, 46)     // randomizeIdle - subtle gray idle tone
    },
    {
        // BLUE theme - high-contrast cool blues and cyan accents
        CRGB(40, 122, 66),  // gateOnV1 - vivid cyan-blue
        CRGB(6, 8, 12),      // gateOffV1 - almost black
        CRGB(10, 66,200), // gateOnV2 - soft sky blue
        CRGB(2, 12, 2),      // gateOffV2 - deep charcoal
        CRGB(22, 77, 77),   // playheadAccent - strong blue accent
        CRGB(16, 36, 80),    // idleBreathingBlue - deep ocean blue
        CRGB(8, 10, 14),     // editModeDimBlueV1 - very dark slate
        CRGB(12, 16, 20),    // editModeDimBlueV2
        CRGB(140, 190, 220), // modNoteActive - pale blue
        CRGB(20, 24, 28),    // modNoteInactive
        CRGB(180, 210, 230), // modVelocityActive - light cyan
        CRGB(24, 28, 32),    // modVelocityInactive
        CRGB(120, 140, 200), // modFilterActive - muted indigo
        CRGB(20, 18, 24),    // modFilterInactive
        CRGB(200, 160, 120), // modDecayActive - warm contrast (subtle)
        CRGB(22, 20, 16),    // modDecayInactive
        CRGB(120, 180, 140), // modAttackActive - cool sage
        CRGB(18, 22, 16),    // modAttackInactive
        CRGB(220, 140, 200), // modOctaveActive - soft magenta accent
        CRGB(20, 12, 16),    // modOctaveInactive
        CRGB(160, 210, 230), // modSlideActive - cyan slide accent
        CRGB(18, 24, 26),    // modSlideInactive
        CRGB(220, 230, 240), // defaultActive - light gray-blue
        CRGB(14, 14, 18),    // defaultInactive - near black
        CRGB(120, 200, 240), // modParamModeActive - bright aqua
        CRGB(16, 18, 18),    // modParamModeInactive
        CRGB(160, 200, 240), // modGateModeActive - cool highlight
        CRGB(18, 16, 14),    // modGateModeInactive
        CRGB(255, 240, 220), // randomizeFlash - bright neutral flash
        CRGB(12, 12, 14)     // randomizeIdle - dark subtle tone
    }, {
        // BLUE theme - high-contrast cool blues and cyan accents
        CRGB(40, 122, 188),  // gateOnV1 - vivid cyan-blue
        CRGB(6, 8, 12),      // gateOffV1 - almost black
        CRGB(120, 100,200), // gateOnV2 - soft sky blue
        CRGB(8, 6, 10),      // gateOffV2 - deep charcoal
        CRGB(32, 99, 12),   // playheadAccent - strong blue accent
        CRGB(16, 36, 80),    // idleBreathingBlue - deep ocean blue
        CRGB(8, 10, 14),     // editModeDimBlueV1 - very dark slate
        CRGB(12, 16, 20),    // editModeDimBlueV2
        CRGB(140, 190, 220), // modNoteActive - pale blue
        CRGB(20, 24, 28),    // modNoteInactive
        CRGB(180, 210, 230), // modVelocityActive - light cyan
        CRGB(24, 28, 32),    // modVelocityInactive
        CRGB(120, 140, 200), // modFilterActive - muted indigo
        CRGB(20, 18, 24),    // modFilterInactive
        CRGB(200, 160, 120), // modDecayActive - warm contrast (subtle)
        CRGB(22, 20, 16),    // modDecayInactive
        CRGB(120, 180, 140), // modAttackActive - cool sage
        CRGB(18, 22, 16),    // modAttackInactive
        CRGB(220, 140, 200), // modOctaveActive - soft magenta accent
        CRGB(20, 12, 16),    // modOctaveInactive
        CRGB(160, 210, 230), // modSlideActive - cyan slide accent
        CRGB(18, 24, 26),    // modSlideInactive
        CRGB(220, 230, 240), // defaultActive - light gray-blue
        CRGB(14, 14, 18),    // defaultInactive - near black
        CRGB(120, 200, 240), // modParamModeActive - bright aqua
        CRGB(16, 18, 18),    // modParamModeInactive
        CRGB(160, 200, 240), // modGateModeActive - cool highlight
        CRGB(18, 16, 14),    // modGateModeInactive
        CRGB(255, 240, 220), // randomizeFlash - bright neutral flash
        CRGB(12, 12, 14)     // randomizeIdle - dark subtle tone
    }, {
        // BLUE theme - high-contrast cool blues and cyan accents
        CRGB(40, 122, 188),  // gateOnV1 - vivid cyan-blue
        CRGB(6, 8, 12),      // gateOffV1 - almost black
        CRGB(120, 100,200), // gateOnV2 - soft sky blue
        CRGB(8, 6, 10),      // gateOffV2 - deep charcoal
        CRGB(32, 99, 12),   // playheadAccent - strong blue accent
        CRGB(16, 36, 80),    // idleBreathingBlue - deep ocean blue
        CRGB(8, 10, 14),     // editModeDimBlueV1 - very dark slate
        CRGB(12, 16, 20),    // editModeDimBlueV2
        CRGB(140, 190, 220), // modNoteActive - pale blue
        CRGB(20, 24, 28),    // modNoteInactive
        CRGB(180, 210, 230), // modVelocityActive - light cyan
        CRGB(24, 28, 32),    // modVelocityInactive
        CRGB(120, 140, 200), // modFilterActive - muted indigo
        CRGB(20, 18, 24),    // modFilterInactive
        CRGB(200, 160, 120), // modDecayActive - warm contrast (subtle)
        CRGB(22, 20, 16),    // modDecayInactive
        CRGB(120, 180, 140), // modAttackActive - cool sage
        CRGB(18, 22, 16),    // modAttackInactive
        CRGB(220, 140, 200), // modOctaveActive - soft magenta accent
        CRGB(20, 12, 16),    // modOctaveInactive
        CRGB(160, 210, 230), // modSlideActive - cyan slide accent
        CRGB(18, 24, 26),    // modSlideInactive
        CRGB(220, 230, 240), // defaultActive - light gray-blue
        CRGB(14, 14, 18),    // defaultInactive - near black
        CRGB(120, 200, 240), // modParamModeActive - bright aqua
        CRGB(16, 18, 18),    // modParamModeInactive
        CRGB(160, 200, 240), // modGateModeActive - cool highlight
        CRGB(18, 16, 14),    // modGateModeInactive
        CRGB(255, 240, 220), // randomizeFlash - bright neutral flash
        CRGB(12, 12, 14)     // randomizeIdle - dark subtle tone
    },
    {
        // GREEN theme - lush greens with clean high-contrast accents
        CRGB(40, 150, 80),  // gateOnV1 - vivid green
        CRGB(2, 8, 12),       // gateOffV1 - near-black green tint
        CRGB(39, 180, 122), // gateOnV2 - pale mint accent
        CRGB(1, 12, 6),       // gateOffV2 - deep dark
        CRGB(0, 110, 60),   // playheadAccent - strong forest accent
        CRGB(18, 44, 28),    // idleBreathingBlue - deep forest for breathing
        CRGB(8, 12, 10),     // editModeDimBlueV1 - very dark green slate
        CRGB(12, 16, 14),    // editModeDimBlueV2
        CRGB(200, 240, 200), // modNoteActive - pale green
        CRGB(22, 26, 22),    // modNoteInactive
        CRGB(180, 230, 200), // modVelocityActive - soft mint
        CRGB(24, 30, 26),    // modVelocityInactive
        CRGB(140, 180, 160), // modFilterActive - muted green-teal
        CRGB(20, 18, 20),    // modFilterInactive
        CRGB(200, 180, 140), // modDecayActive - subtle warm contrast
        CRGB(22, 20, 18),    // modDecayInactive
        CRGB(140, 200, 120), // modAttackActive - bright sage
        CRGB(18, 20, 16),    // modAttackInactive
        CRGB(220, 180, 200), // modOctaveActive - soft rose accent
        CRGB(20, 12, 12),    // modOctaveInactive
        CRGB(160, 220, 180), // modSlideActive - minty slide accent
        CRGB(18, 20, 18),    // modSlideInactive
        CRGB(240, 250, 240), // defaultActive - off-white for active defaults
        CRGB(12, 14, 12),    // defaultInactive - near black
        CRGB(160, 240, 200), // modParamModeActive - bright mint
        CRGB(16, 14, 14),    // modParamModeInactive
        CRGB(200, 220, 160), // modGateModeActive - soft highlight
        CRGB(16, 14, 12),    // modGateModeInactive
        CRGB(255, 250, 200), // randomizeFlash - warm flash
        CRGB(10, 12, 10)     // randomizeIdle - very dark idle tone
    }
};

static const LEDThemeColors* activeThemeColors = &ALL_THEMES[static_cast<int>(LEDTheme::DEFAULT)];

void setLEDTheme(LEDTheme theme) {
    if (static_cast<int>(theme) < static_cast<int>(LEDTheme::COUNT)) {
        activeThemeColors = &ALL_THEMES[static_cast<int>(theme)];
    }
}

const LEDThemeColors* getActiveThemeColors() {
    return activeThemeColors;
}

DEFINE_GRADIENT_PALETTE( parameterPalette ) {
    0,   0,   0,  255,   // Blue
    85,  0,   255, 0,    // Green
    170, 255, 0,   0,    // Red
    255, 0,   0,  255    // Back to blue
};
CRGBPalette16 parameterColors = parameterPalette;

CRGB getParameterColor(ParamId param, uint8_t intensity) {
    uint8_t paletteIndex = map(static_cast<int>(param), 0, static_cast<int>(ParamId::Count), 0, 255);
    return ColorFromPalette(parameterColors, paletteIndex, intensity);
}

void addPolyrhythmicOverlay(
  LEDMatrix& ledMatrix,
  const Sequencer& sequencer,
  bool isSecondVoiceInPair,
  uint8_t overlayIntensity = LEDConstants::POLYRHYTHM_INTENSITY
) {
  // Only add overlay if sequencer is actively running
  if (!sequencer.isRunning()) {
    return;
  }

  // Calculate base offset for voice positioning in matrix
  const int voiceBaseOffset = isSecondVoiceInPair ? LED_MATRIX_BOTTOM_HALF_OFFSET : LEDConstants::TOP_HALF_OFFSET;

  // Parameter overlay configuration for polyrhythmic visualization
  struct PolyrhythmicParameterOverlay {
    ParamId parameterID;
    CRGB overlayColor;
  };

  const PolyrhythmicParameterOverlay overlayParameters[LEDConstants::POLYRHYTHM_PARAM_COUNT] = {
    {ParamId::Note,     LEDColors::POLYRHYTHM_NOTE},
    {ParamId::Velocity, LEDColors::POLYRHYTHM_VELOCITY},
    {ParamId::Filter,   LEDColors::POLYRHYTHM_FILTER}
  };

  // Apply overlay for each parameter type
  for (size_t paramIndex = 0; paramIndex < LEDConstants::POLYRHYTHM_PARAM_COUNT; ++paramIndex) {
    const ParamId currentParameter = overlayParameters[paramIndex].parameterID;
    const uint8_t currentParameterStep = sequencer.getCurrentStepForParameter(currentParameter);
    const uint8_t parameterStepCount = sequencer.getParameterStepCount(currentParameter);

    // Only apply overlay if parameter is within valid bounds
    if (currentParameterStep < LEDConstants::MAX_STEP_BUTTONS && 
        parameterStepCount > 1 && 
        parameterStepCount <= LEDConstants::MAX_STEP_BUTTONS) {
      
      // Calculate LED matrix position
      const int ledLinearIndex = voiceBaseOffset + currentParameterStep;
      CRGB currentLEDColor = ledMatrix.getLeds()[ledLinearIndex];
      
      // Blend overlay color with existing LED color
      currentLEDColor += overlayParameters[paramIndex].overlayColor;

      // Convert linear index to matrix coordinates
      const int xCoordinate = currentParameterStep % LEDMatrix::WIDTH;
      const int yCoordinate = (currentParameterStep / LEDMatrix::WIDTH) + 
                             (isSecondVoiceInPair ? LEDConstants::VOICE_PAIR_SEPARATION + 1 : 0);

      ledMatrix.setLED(xCoordinate, yCoordinate, currentLEDColor);
    }
  }
}

float ease(float x) {
    return x < 0.5 ? 2 * x * x : 1 - pow(-2 * x + 2, 2) / 2;
}

float smoothBreathing(uint32_t timeMs) {
  // Calculate smooth breathing animation value using easing function
  const float normalizedTime = static_cast<float>(timeMs % LEDConstants::BREATHING_CYCLE_MS) / 
                              static_cast<float>(LEDConstants::BREATHING_CYCLE_MS);
  return ease(0.5f * (1.0f + sin(2.0f * PI * normalizedTime)));
}

void setStepLedColor(uint8_t stepIndex, uint8_t redValue, uint8_t greenValue, uint8_t blueValue) {
  // Legacy function for setting individual step LED colors
  // Note: This function requires a LEDMatrix reference to work properly
  // Consider using the main LED update functions instead
}

void setupLEDMatrixFeedback() {
  // Initialize smoothed color buffer to black (off state)
  for (int ledIndex = 0; ledIndex < LEDConstants::MATRIX_TOTAL_LEDS; ++ledIndex) {
    smoothedTargetColorBuffer[ledIndex] = LEDColors::BLACK;
  }
}

/**
 * @brief Updates LED matrix to show settings mode interface
 *
 * Displays menu options and preset selections using step LEDs:
 * - Main menu: Shows Voice 0 and Voice 1 options (steps 0-1)
 * - Preset selection: Shows available presets (steps 0-5 for 6 presets)
 * - Uses different colors to indicate current selection and available options
 */
void updateSettingsModeLEDs(LEDMatrix& ledMatrix, const UIState& uiState) {
    const LEDThemeColors* activeThemeColors = getActiveThemeColors();

    // Clear all LEDs first
    for (int i = 0; i < LEDMatrix::WIDTH * LEDMatrix::HEIGHT; ++i) {
        ledMatrix.getLeds()[i] = CRGB::Black;
    }

    if (uiState.inPresetSelection) {
        // Preset selection mode - show available presets
        const uint8_t presetCount = 6; // VoicePresets::getPresetCount() returns 6

        // Define preset colors based on voice being configured
        CRGB selectedColor = (uiState.settingsMenuIndex == 0) ?
            activeThemeColors->gateOnV1 : activeThemeColors->gateOnV2;
        CRGB availableColor = (uiState.settingsMenuIndex == 0) ?
            activeThemeColors->gateOffV1 : activeThemeColors->gateOffV2;

        // Show presets in first 6 step positions (0-5)
        for (uint8_t i = 0; i < presetCount && i < 16; i++) {
            CRGB color;

            // Highlight currently selected preset
            uint8_t currentPresetIndex = (uiState.settingsMenuIndex == 0) ?
                uiState.voicePresetIndices[0] : uiState.voicePresetIndices[1];

            if (i == currentPresetIndex) {
                // Current preset - bright pulsing
                uint32_t time = millis();
                float pulse = 0.5f + 0.5f * sinf(time * 0.008f);
                color = selectedColor;
                color.nscale8(static_cast<uint8_t>(128 + 127 * pulse));
            } else {
                // Available preset - dim steady
                color = availableColor;
                color.nscale8(64);
            }

            // Calculate LED position
            int x = i % LEDMatrix::WIDTH;
            int y = i / LEDMatrix::WIDTH;
            ledMatrix.setLED(x, y, color);
        }

        // Show which voice is being configured in bottom row
        if (uiState.settingsMenuIndex == 0) {
            // Voice 0 indicator
            ledMatrix.setLED(0, 7, activeThemeColors->gateOnV1);
          } else {
            // Voice 1 indicator
            ledMatrix.setLED(1, 7, activeThemeColors->gateOnV2);
          }

    } else {
        // Main settings menu - show Voice 0 and Voice 1 options

        // Voice 0 option (step 0)
        CRGB voice1Color = (uiState.settingsMenuIndex == 0) ?
            activeThemeColors->gateOnV1 : activeThemeColors->gateOffV1;
        if (uiState.settingsMenuIndex == 0) {
            // Add pulsing effect for selected option
            uint32_t time = millis();
            float pulse = 0.5f + 0.5f * sinf(time * 0.006f);
            voice1Color.nscale8(static_cast<uint8_t>(128 + 127 * pulse));
        } else {
            voice1Color.nscale8(96);
        }
        ledMatrix.setLED(0, 0, voice1Color);

        // Voice 1 option (step 1)
        CRGB voice2Color = (uiState.settingsMenuIndex == 1) ?
            activeThemeColors->gateOnV2 : activeThemeColors->gateOffV2;
        if (uiState.settingsMenuIndex == 1) {
            // Add pulsing effect for selected option
            uint32_t time = millis();
            float pulse = 0.5f + 0.5f * sinf(time * 0.006f);
            voice2Color.nscale8(static_cast<uint8_t>(128 + 127 * pulse));
        } else {
            voice2Color.nscale8(96);
        }
        ledMatrix.setLED(1, 0, voice2Color);
    }
}

void updateVoiceParameterLEDs(LEDMatrix& ledMatrix, const UIState& uiState) {
    if (!uiState.inVoiceParameterMode) return;

    // Get active theme colors
    const LEDThemeColors* activeThemeColors = getActiveThemeColors();
    if (!activeThemeColors) return;

    // Clear all LEDs first
    for (int i = 0; i < LEDMatrix::WIDTH * LEDMatrix::HEIGHT; i++) {
        ledMatrix.setLED(i % LEDMatrix::WIDTH, i / LEDMatrix::WIDTH, CRGB::Black);
    }

    // Map button index to LED position (buttons 9-24 map to steps 8-23)
    uint8_t ledIndex = uiState.lastVoiceParameterButton - 1;
    if (ledIndex >= LEDMatrix::WIDTH * LEDMatrix::HEIGHT) return;

    // Choose color based on voice and parameter type
    CRGB paramColor;

    switch (uiState.lastVoiceParameterButton) {
        case 9:  // Envelope
            paramColor = uiState.isVoice2Mode ? activeThemeColors->modAttackActive : activeThemeColors->modDecayActive;
            break;
        case 10: // Overdrive
            paramColor = uiState.isVoice2Mode ? activeThemeColors->modFilterActive : activeThemeColors->modVelocityActive;
            break;
        case 11: // Wavefolder
            paramColor = uiState.isVoice2Mode ? activeThemeColors->modOctaveActive : activeThemeColors->modNoteActive;
            break;
        case 12: // Filter Mode
            paramColor = uiState.isVoice2Mode ? activeThemeColors->gateOnV2 : activeThemeColors->gateOnV1;
            break;
        case 13: // Filter Resonance
            paramColor = uiState.isVoice2Mode ? activeThemeColors->modSlideActive : activeThemeColors->modParamModeActive;
            break;
        default:
            paramColor = uiState.isVoice2Mode ? activeThemeColors->defaultActive : activeThemeColors->defaultInactive;
            break;
    }

    // Create pulsing effect for 3 seconds
    if (millis() - uiState.voiceParameterChangeTime < 3000) {
        uint32_t time = millis();
        float pulse = 0.5f + 0.5f * sinf(time * 0.01f); // Faster pulse for voice parameters
        paramColor.nscale8(static_cast<uint8_t>(128 + 127 * pulse));
    } else {
        paramColor.nscale8(64); // Dim after timeout
    }

    // Set the LED for the voice parameter button
    ledMatrix.setLED(ledIndex % LEDMatrix::WIDTH, ledIndex / LEDMatrix::WIDTH, paramColor);
}


/**
 * @brief Updates the LED matrix to reflect the current gate states of both sequencers.
 *
 * This function visually represents the gate status (on/off) for each step of two sequencers
 * on an LED matrix. It supports two main display modes:
 *   - **Idle/Breathing Mode:** When neither sequencer is running and no step is selected for editing,
 *     all step LEDs display a synchronized breathing animation.
 *   - **Active Sequencing Mode:** When either sequencer is running or a step is selected,
 *     each step's LED shows its gate state, slide activation, and playhead position for both voices.
 *
 * The function uses double-buffered color blending for smooth LED transitions and supports
 * highlighting slide and playhead states with accent colors.
 *
 * @param ledMatrix Reference to the LEDMatrix object controlling the physical LEDs.
 * @param seq1      Reference to the first Sequencer (Voice 1).
 * @param seq2      Reference to the second Sequencer (Voice 2).
 * @param uiState   Reference to the UIState struct containing UI and mode flags.
 */
/**
 * @brief Render a voice pair (voices 1/2 or 3/4) into the LED matrix
 * 
 * Displays gate states, playhead position, and slide effects for two voices
 * arranged in top and bottom rows of the matrix display.
 * 
 * @param ledMatrix Reference to LED matrix for output
 * @param firstVoiceSequencer First voice sequencer (top row)
 * @param secondVoiceSequencer Second voice sequencer (bottom row)
 * @param themeColors Pointer to active theme colors
 * @param matrixBaseOffset Base offset for positioning in matrix
 */
static void renderVoicePair(
  LEDMatrix& ledMatrix,
  const Sequencer& firstVoiceSequencer,
  const Sequencer& secondVoiceSequencer,
  const LEDThemeColors* themeColors,
  int matrixBaseOffset
) {
  // Validate sequencer gate step counts
  const uint8_t firstVoiceGateStepCount = firstVoiceSequencer.getParameterStepCount(ParamId::Gate);
  const uint8_t secondVoiceGateStepCount = secondVoiceSequencer.getParameterStepCount(ParamId::Gate);
  
  if (firstVoiceGateStepCount == 0) {
    DBG_WARN("renderVoicePair: First voice has zero gate step count");
    return;
  }
  if (secondVoiceGateStepCount == 0) {
    DBG_WARN("renderVoicePair: Second voice has zero gate step count");
    return;
  }

  // Render each step for both voices in the pair
  for (int stepIndex = 0; stepIndex < LEDConstants::MAX_STEP_BUTTONS; ++stepIndex) {
    // === First Voice (Top Row) Processing ===
    const Step& firstVoiceStep = firstVoiceSequencer.getStep(stepIndex);
    const bool isFirstVoicePlayhead = (firstVoiceSequencer.getCurrentStepForParameter(ParamId::Gate) == stepIndex && 
                                      firstVoiceSequencer.isRunning());
    
    // Determine base color based on gate state
    CRGB firstVoiceColor = firstVoiceStep.isGateActive ? 
      themeColors->gateOnV1 : themeColors->gateOffV1;
    
    // Add slide effect if active for this step
    if (firstVoiceSequencer.getStepParameterValue(ParamId::Slide, stepIndex) > 0) { 
      nblend(firstVoiceColor, themeColors->modSlideActive, LEDConstants::MEDIUM_BRIGHTNESS); 
    }
    
    // Add playhead accent if this is the current step
    if (isFirstVoicePlayhead) { 
      firstVoiceColor += themeColors->playheadAccent; 
    }
    
    // Apply smoothed color blending for top row
    const int topRowLEDIndex = matrixBaseOffset + stepIndex;
    nblend(smoothedTargetColorBuffer[topRowLEDIndex], firstVoiceColor, TARGET_SMOOTHING_BLEND_AMOUNT);
    nblend(ledMatrix.getLeds()[topRowLEDIndex], smoothedTargetColorBuffer[topRowLEDIndex], 
           LEDConstants::STANDARD_BLEND_AMOUNT);

    // === Second Voice (Bottom Row) Processing ===
    const Step& secondVoiceStep = secondVoiceSequencer.getStep(stepIndex);
    const bool isSecondVoicePlayhead = (secondVoiceSequencer.getCurrentStepForParameter(ParamId::Gate) == stepIndex && 
                                       secondVoiceSequencer.isRunning());
    
    // Determine base color based on gate state
    CRGB secondVoiceColor = secondVoiceStep.isGateActive ? 
      themeColors->gateOnV2 : themeColors->gateOffV2;
    
    // Add slide effect if active for this step
    if (secondVoiceSequencer.getStepParameterValue(ParamId::Slide, stepIndex) > 0) { 
      nblend(secondVoiceColor, themeColors->modSlideActive, LEDConstants::MEDIUM_BRIGHTNESS); 
    }
    
    // Add playhead accent if this is the current step
    if (isSecondVoicePlayhead) { 
      secondVoiceColor += themeColors->playheadAccent; 
    }
    
    // Apply smoothed color blending for bottom row
    const int bottomRowLEDIndex = matrixBaseOffset + LED_MATRIX_BOTTOM_HALF_OFFSET + stepIndex;
    nblend(smoothedTargetColorBuffer[bottomRowLEDIndex], secondVoiceColor, TARGET_SMOOTHING_BLEND_AMOUNT);
    nblend(ledMatrix.getLeds()[bottomRowLEDIndex], smoothedTargetColorBuffer[bottomRowLEDIndex], 
           LEDConstants::STANDARD_BLEND_AMOUNT);
  }
}
void updateGateLEDs(
    LEDMatrix& ledMatrix,
    const Sequencer& seq1,
    const Sequencer& seq2,
    const UIState& uiState
) {
    // --- Idle/Breathing Mode ---
    // If both sequencers are stopped and no step is selected for editing,
    // display a blue breathing effect on all step LEDs.
    if (!seq1.isRunning() && !seq2.isRunning() && uiState.selectedStepForEdit == -1) {
        float t = millis() / 5000.0f; // Slow time base for breathing
        float breath = 0.5f * (1.0f + sinf(2.0f * 3.1415926f * t)); // Sine wave [0,1]
        uint8_t b = (uint8_t)(breath * 64.0f + 16.0f); // Blue channel intensity

        for (int step = 0; step < 16; ++step) {
            CRGB currentTarget = CRGB(0, 0, b); // Blue breathing color

            // Blend target color into smoothed buffer for smooth transitions
            nblend(smoothedTargetColorBuffer[step], currentTarget, TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[step], smoothedTargetColorBuffer[step], 222);

            // Voice 2 bottom half - fixed offset calculation
            int ledIndex = 24 + step; // Bottom half of 8x8 matrix
            nblend(smoothedTargetColorBuffer[ledIndex], currentTarget, TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex], 222);
        }
    } else {
        // --- Active Sequencing Mode ---
        // For each step, update the LED color based on gate, slide, and playhead status for both voices.
        for (int step = 0; step < 16; ++step) {
            // --- Voice 1 (seq1) ---
            const Step& s1 = seq1.getStep(step);
            bool isPlayhead1 = (seq1.getCurrentStepForParameter(ParamId::Gate) == step && seq1.isRunning());
            // Choose color based on gate state
            CRGB targetColor1 = s1.isGateActive ? activeThemeColors->gateOnV1 : activeThemeColors->gateOffV1;

            // If slide is active for this step, blend in the slide accent color
            uint8_t slideValue1 = seq1.getStepParameterValue(ParamId::Slide, step);
            if (slideValue1 > 0) {
                nblend(targetColor1, activeThemeColors->modSlideActive, 128);
            }

            // If this step is the playhead, add the playhead accent color
            if (isPlayhead1) {
                targetColor1 += activeThemeColors->playheadAccent;
            }

            // Blend to smoothed buffer and then to the actual LED for smooth transitions
            nblend(smoothedTargetColorBuffer[step], targetColor1, TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[step], smoothedTargetColorBuffer[step], 166);

            // --- Voice 2 (seq2) ---
            const Step& s2 = seq2.getStep(step);
            bool isPlayhead2 = (seq2.getCurrentStepForParameter(ParamId::Gate) == step && seq2.isRunning());
            CRGB targetColor2 = s2.isGateActive ? activeThemeColors->gateOnV2 : activeThemeColors->gateOffV2;

            uint8_t slideValue2 = seq2.getStepParameterValue(ParamId::Slide, step);
            if (slideValue2 > 0) {
                nblend(targetColor2, activeThemeColors->modSlideActive, 128);
            }

            if (isPlayhead2) {
                targetColor2 += activeThemeColors->playheadAccent;
            }

            int ledIndex = 24 + step; // Fixed offset for bottom half
            nblend(smoothedTargetColorBuffer[ledIndex], targetColor2, TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex], 166);
        }
    }
}

void updateStepLEDs(
    LEDMatrix& ledMatrix,
    const Sequencer& seq1,
    const Sequencer& seq2,
    const Sequencer& seq3,
    const Sequencer& seq4,
    const UIState& uiState,
    int mm
) {
    // If requested, immediately clear smoothed buffers to force a visual refresh
    if (uiState.resetStepsLightsFlag) {
        for (int i = 0; i < LEDConstants::MATRIX_TOTAL_LEDS; ++i) {
            smoothedTargetColorBuffer[i] = CRGB::Black;
        }
        // One-shot consumption of the flag. UIState is passed as const to renderers,
        // so we clear it here intentionally to prevent continuous clearing every frame.
        const_cast<UIState&>(uiState).resetStepsLightsFlag = false;
    }

    // Handle settings mode LED feedback
    if (uiState.settingsMode) {
        updateSettingsModeLEDs(ledMatrix, uiState);
        return;
    }

    // Handle voice parameter mode LED feedback
    if (uiState.inVoiceParameterMode && (millis() - uiState.voiceParameterChangeTime < 3000)) {
        updateVoiceParameterLEDs(ledMatrix, uiState);
        return;
    }

    const ParamButtonMapping* heldMapping = getHeldParameterButton(uiState);
    bool anyParamForLengthHeld = (heldMapping != nullptr);
    ParamId activeParamIdForLength = anyParamForLengthHeld ? heldMapping->paramId : ParamId::Count;

    // Gate sequence length mode visualization: blink LEDs up to current gate length for selected voice
    if (uiState.gateSeqLengthMode) {
        // Select active sequencer by selectedVoiceIndex (0..3)
        const Sequencer* seqPtr = (uiState.selectedVoiceIndex == 0) ? &seq1 :
                                  (uiState.selectedVoiceIndex == 1) ? &seq2 :
                                  (uiState.selectedVoiceIndex == 2) ? &seq3 : &seq4;
        const Sequencer& activeSeq = *seqPtr;

        const bool isSecondInPair = (uiState.selectedVoiceIndex % 2) == 1;
        const int baseOffset = isSecondInPair ? 24 : 0; // fixed offset for bottom row
        const CRGB withinColorBase = isSecondInPair ? getActiveThemeColors()->gateOnV2
                                                    : getActiveThemeColors()->gateOnV1;

        // Simple blink state
        static bool blinkState = false;
        static uint32_t lastBlinkMs = 0;
        const uint32_t now = millis();
        if (now - lastBlinkMs > 250) { // ~4 Hz
            blinkState = !blinkState;
            lastBlinkMs = now;
        }

        const uint8_t gateLen = activeSeq.getParameterStepCount(ParamId::Gate);

        // Dim the other row fully to focus on the selected voice
        for (int step = 0; step < LEDConstants::MAX_STEP_BUTTONS; ++step) {
            const int otherIndex = (isSecondInPair ? 0 : 24) + step;
            nblend(smoothedTargetColorBuffer[otherIndex], CRGB::Black, LEDConstants::TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[otherIndex], smoothedTargetColorBuffer[otherIndex], LEDConstants::DIM_BLEND_AMOUNT);
        }

        // Paint selected row with blinking up-to-length visualization
        for (int step = 0; step < LEDConstants::MAX_STEP_BUTTONS; ++step) {
            CRGB target = CRGB::Black;
            if (step < gateLen && gateLen > 1) {
                target = withinColorBase;
                if (blinkState) {
                    // Dim on alternate frames for blink
                    target.nscale8(60);
                }
            }
            const int ledIndex = baseOffset + step;
            nblend(smoothedTargetColorBuffer[ledIndex], target, LEDConstants::TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex], LEDConstants::STANDARD_BLEND_AMOUNT);
        }

        return;
    }

    if (uiState.slideMode) {
        // Select sequencer based on selectedVoiceIndex (0..3)
        const Sequencer* seqPtr = (uiState.selectedVoiceIndex == 0) ? &seq1 :
                                  (uiState.selectedVoiceIndex == 1) ? &seq2 :
                                  (uiState.selectedVoiceIndex == 2) ? &seq3 : &seq4;
        const Sequencer& activeSeq = *seqPtr;
        uint8_t slidePlayhead = activeSeq.getCurrentStepForParameter(ParamId::Slide);
        uint8_t slideLength = activeSeq.getParameterStepCount(ParamId::Slide);

        for (int step = 0; step < NUMBER_OF_STEP_BUTTONS; step++) {
            uint8_t slideValue = activeSeq.getStepParameterValue(ParamId::Slide, step);
            bool isSlideActive = (slideValue > 0);
            bool isPlayhead = (step == slidePlayhead);
            bool isWithinLength = (step < slideLength);

            CRGB color;
            if (isPlayhead && isWithinLength) {
                color = activeThemeColors->modSlideActive;
            } else if (isSlideActive && isWithinLength) {
                color = activeThemeColors->modSlideActive;
                color.nscale8(64);
            } else if (isWithinLength) {
                color = activeThemeColors->modSlideInactive;
                color.nscale8(32);
            } else {
                color = CRGB::Black;
            }

            int x = step % LEDMatrix::WIDTH;
            int y = step / LEDMatrix::WIDTH;
            // Place on top/bottom half based on voice within pair (0/1 top, 2/3 page uses same rows)
            if ((uiState.selectedVoiceIndex % 2) == 1) {
                y += 3; // second voice in pair uses lower band
            }
            ledMatrix.setLED(x, y, color);
        }
        return;
    }

    bool paramValueEditActive = isAnyParameterButtonHeld(uiState);

    // Helper to fetch by selected voice
    auto& activeSeqRef = (uiState.selectedVoiceIndex == 0) ? seq1 :
                         (uiState.selectedVoiceIndex == 1) ? seq2 :
                         (uiState.selectedVoiceIndex == 2) ? seq3 : seq4;

    if (paramValueEditActive) {
        uint8_t currentLength = activeSeqRef.getParameterStepCount(activeParamIdForLength);
        uint8_t paramPlayhead = activeSeqRef.getCurrentStepForParameter(activeParamIdForLength);

        // Dim the non-selected row (top or bottom) in the current page
        bool isSecondInPair = (uiState.selectedVoiceIndex % 2) == 1;
        for (int step = 0; step < 16; ++step) {
            int topIndex = step;
            int bottomIndex = 24 + step; // Fixed offset for 8x8 matrix
            if (!isSecondInPair) {
                // Selected voice is top row; dim bottom
                nblend(smoothedTargetColorBuffer[bottomIndex], CRGB::Black, TARGET_SMOOTHING_BLEND_AMOUNT);
                nblend(ledMatrix.getLeds()[bottomIndex], smoothedTargetColorBuffer[bottomIndex], 32);
            } else {
                // Selected voice is bottom row; dim top
                nblend(smoothedTargetColorBuffer[topIndex], CRGB::Black, TARGET_SMOOTHING_BLEND_AMOUNT);
                nblend(ledMatrix.getLeds()[topIndex], smoothedTargetColorBuffer[topIndex], 32);
            }
        }

        // Paint the selected row with parameter length/playhead info
        for (int step = 0; step < 16; ++step) {
            CRGB targetColor;
            if (step < currentLength) {
                if (step == paramPlayhead && activeSeqRef.isRunning()) {
                    targetColor = getParameterColor(activeParamIdForLength, 180);
                } else {
                    // Use V1 tint for top row, V2 tint for bottom row
                    targetColor = isSecondInPair ? activeThemeColors->editModeDimBlueV2
                                                 : activeThemeColors->editModeDimBlueV1;
                }
            } else {
                targetColor = CRGB::Black;
            }
            int ledIndex = (isSecondInPair ? 24 : 0) + step; // Fixed offset
            nblend(smoothedTargetColorBuffer[ledIndex], targetColor, TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex], isSecondInPair ? 122 : 64);
        }

        return;
    }

    if (anyParamForLengthHeld) {
        uint8_t currentLength = activeSeqRef.getParameterStepCount(activeParamIdForLength);
        uint8_t paramPlayhead = activeSeqRef.getCurrentStepForParameter(activeParamIdForLength);

        // Paint only the selected row's within-length area
        bool isSecondInPair = (uiState.selectedVoiceIndex % 2) == 1;
        for (int step = 0; step < currentLength; ++step) {
            CRGB targetColor = (step == paramPlayhead && activeSeqRef.isRunning())
                                   ? getParameterColor(activeParamIdForLength, 180)
                                   : (isSecondInPair ? activeThemeColors->editModeDimBlueV2
                                                     : activeThemeColors->editModeDimBlueV1);
            int ledIndex = (isSecondInPair ? 24 : 0) + step; // Fixed offset
            nblend(smoothedTargetColorBuffer[ledIndex], targetColor, TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex], isSecondInPair ? 200 : 60);
        }

        // Dim the other row's within-length area
        for (int step = 0; step < currentLength; ++step) {
            int otherIndex = (isSecondInPair ? 0 : 24) + step; // Fixed offset
            nblend(smoothedTargetColorBuffer[otherIndex], CRGB::Black, TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[otherIndex], smoothedTargetColorBuffer[otherIndex], 150);
        }
    } else {
        // Determine which voice pair to display based on selectedVoiceIndex
        bool showFirstPair = (uiState.selectedVoiceIndex < 2);
        bool secondInPair = (uiState.selectedVoiceIndex % 2) == 1;
        const LEDThemeColors* theme = getActiveThemeColors();

        // Clear first to avoid ghosting when switching pages
        for (int i = 0; i < LEDMatrix::WIDTH * LEDMatrix::HEIGHT; ++i) {
            nblend(smoothedTargetColorBuffer[i], CRGB::Black, TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[i], smoothedTargetColorBuffer[i], 64);
        }

        // Render either voices 1/2 (page 1) or 3/4 (page 2)
        if (showFirstPair) {
            renderVoicePair(ledMatrix, seq1, seq2, theme, 0);
        } else {
            renderVoicePair(ledMatrix, seq3, seq4, theme, 0);
        }

        // Polyrhythmic overlays for the visible pair only
        if (showFirstPair) {
            addPolyrhythmicOverlay(ledMatrix, seq1, false, 32);
            addPolyrhythmicOverlay(ledMatrix, seq2, true, 32);
        } else {
            addPolyrhythmicOverlay(ledMatrix, seq3, false, 32);
            addPolyrhythmicOverlay(ledMatrix, seq4, true, 32);
        }

  
        // Highlight selected step if editing
        if (uiState.selectedStepForEdit >= 0 && uiState.selectedStepForEdit < 16) {
            int ledIndex = uiState.selectedVoiceIndex % 2 == 1 ? (24 + uiState.selectedStepForEdit) : uiState.selectedStepForEdit; // Fixed offset

            static bool blinkState = false;
            static uint32_t lastBlinkTime = 0;
            uint32_t currentTime = millis();
            if (currentTime - lastBlinkTime > 500) {
                blinkState = !blinkState;
                lastBlinkTime = currentTime;
            }

            CRGB highlightColor = blinkState ? CRGB::White : CRGB::Black;
            nblend(smoothedTargetColorBuffer[ledIndex], highlightColor, TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex], 100);
        }
    }
}
