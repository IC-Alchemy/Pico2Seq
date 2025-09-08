#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include <FastLED.h>
#include "../ui/UIState.h"
#include "../sensors/as5600.h"
#include "ledMatrix.h"
#include "LEDMatrixFeedback.h"
#include "LEDConstants.h"

/**
 * @brief LED Controller for PicoMudrasSequencer Control LEDs
 *
 * Manages all control LED functionality including parameter button feedback,
 * mode indicators, voice selection, and AS5600 encoder visual feedback.
 * Consolidates LED control logic while maintaining real-time performance
 * and consistent visual feedback patterns.
 *
 * The controller handles:
 * - Parameter button LED states (Note, Velocity, Filter, Attack, Decay, Octave, Slide)
 * - Delay effect parameter LEDs (Time, Feedback)
 * - Voice selection indicators (Voice 1/2)
 * - Mode indicators (Delay toggle, Randomize)
 * - AS5600 encoder parameter value visualization
 */

namespace ControlLEDIndices
{
  // Matrix index helper for 32x32
  constexpr int idx(int x, int y) { return y * LEDConstants::MATRIX_WIDTH + x; }

  // Parameter button LED positions (placed on row 8, columns 0..6)
  static constexpr int NOTE_LED_INDEX     = idx(0, 8);
  static constexpr int VELOCITY_LED_INDEX = idx(1, 8);
  static constexpr int FILTER_LED_INDEX   = idx(2, 8);
  static constexpr int ATTACK_LED_INDEX   = idx(3, 8);
  static constexpr int DECAY_LED_INDEX    = idx(4, 8);
  static constexpr int OCTAVE_LED_INDEX   = idx(5, 8);
  static constexpr int SLIDE_LED_INDEX    = idx(6, 8);

  // Delay parameter LED positions (row 9)
  static constexpr int DELAY_TIME_LED_INDEX     = idx(0, 9);
  static constexpr int DELAY_FEEDBACK_LED_INDEX = idx(1, 9);

  // Mode indicator LED positions (row 10)
  static constexpr int VOICE1_LED_INDEX      = idx(0, 10);
  static constexpr int VOICE2_LED_INDEX      = idx(1, 10);
  static constexpr int DELAY_TOGGLE_LED_INDEX = idx(3, 10); // Delay effect on/off indicator
  static constexpr int RANDOMIZE_LED_INDEX     = idx(4, 10);
}

// External function declarations for sensor and theme access
extern float getAS5600ParameterValue();
extern const LEDThemeColors *getActiveThemeColors();

/**
 * @brief Initialize the LED controller system
 *
 * Performs any necessary initialization for the control LED system.
 * Currently no initialization is required, but provides extension point.
 */
void initLEDController();

/**
 * @brief Update all control LEDs based on current UI state
 *
 * Updates parameter button LEDs, mode indicators, and AS5600 encoder feedback
 * based on the current UI state. Handles pulsing animations for active parameters,
 * AS5600 parameter value visualization, and mode indicator states.
 *
 * @param ledMatrix Reference to the LED matrix for output
 * @param uiState Const reference to the central UI state object containing
 *                button states, mode flags, and parameter selections
 */
void updateControlLEDs(LEDMatrix &ledMatrix, const UIState &uiState);

#endif // LED_CONTROLLER_H
