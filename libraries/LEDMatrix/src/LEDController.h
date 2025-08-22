#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include <FastLED.h>
#include "../ui/UIState.h"
#include "../sensors/as5600.h"
#include "LEDMatrixClass.h"
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

namespace ControlLEDIndices {
  // Parameter button LED positions (buttons 16-22 mapped to matrix positions)
  static constexpr int NOTE_LED_INDEX = 48;
  static constexpr int VELOCITY_LED_INDEX = 49;
  static constexpr int FILTER_LED_INDEX = 50;
  static constexpr int ATTACK_LED_INDEX = 51;
  static constexpr int DECAY_LED_INDEX = 52;
  static constexpr int OCTAVE_LED_INDEX = 53;
  static constexpr int SLIDE_LED_INDEX = 54;

  // Delay parameter LED positions (AS5600 encoder control)
  static constexpr int DELAY_TIME_LED_INDEX = 40;      // Matrix position (0,6)
  static constexpr int DELAY_FEEDBACK_LED_INDEX = 41;  // Matrix position (0,7)

  // Mode indicator LED positions
  static constexpr int VOICE1_LED_INDEX = 56;
  static constexpr int VOICE2_LED_INDEX = 57;
  static constexpr int DELAY_TOGGLE_LED_INDEX = 59;    // Delay effect on/off indicator
  static constexpr int RANDOMIZE_LED_INDEX = 64;
}

// External function declarations for sensor and theme access
extern float getAS5600ParameterValue();
extern const LEDThemeColors* getActiveThemeColors();

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
void updateControlLEDs(LEDMatrix& ledMatrix, const UIState& uiState);

#endif // LED_CONTROLLER_H
