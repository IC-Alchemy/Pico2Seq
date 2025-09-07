#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <Arduino.h>
#include "../sequencer/SequencerDefs.h"
#include "UIState.h" // Include the new state header

/**
 * @brief Button state and timing management for PicoMudrasSequencer UI
 *
 * Provides utilities for button press detection and parameter button mappings,
 * operating on a central UIState object.
 */

// UI Timing Constants
namespace UITimingConstants
{
  static constexpr unsigned long LONG_PRESS_THRESHOLD_MS = 400;
  static constexpr unsigned long DEBOUNCE_DELAY_MS = 50;
  static constexpr unsigned long DOUBLE_PRESS_WINDOW_MS = 300;
  static constexpr unsigned long FLASH_DURATION_MS = 250;
  static constexpr unsigned long VOICE_PARAMETER_DISPLAY_DURATION_MS = 2000;
  static constexpr unsigned long SETTINGS_MODE_TIMEOUT_MS = 30000;
}

// =======================
//   BUTTON MAPPING STRUCTURES
// =======================

/**
 * @brief Complete button mapping with index, parameter, and name.
 * The held state is now managed in UIState, not via a pointer.
 */
struct ParamButtonMapping
{
  uint8_t buttonIndex;
  ParamId paramId;
  const char *name;
};

// =======================
//   BUTTON MAPPING ARRAY
// =======================

// Complete parameter button mappings
extern const ParamButtonMapping PARAM_BUTTON_MAPPINGS[];
extern const size_t PARAM_BUTTON_MAPPINGS_SIZE;

// =======================
//   FUNCTION DECLARATIONS
// =======================

/**
 * @brief Initialize the UI state for the button manager.
 * @param uiState Reference to the central UI state object.
 */
void initButtonManager(UIState &uiState);

/**
 * @brief Check if a press duration qualifies as a long press.
 * @param pressDurationMs Duration of button press in milliseconds.
 * @return true if duration exceeds long press threshold (400ms).
 */
bool isLongPress(unsigned long pressDurationMs);

/**
 * @brief Check if any parameter button is currently held.
 * @param uiState Const reference to the central UI state object.
 * @return true if any parameter button is held.
 */
bool isAnyParameterButtonHeld(const UIState &uiState);

/**
 * @brief Get the mapping for the currently held parameter button.
 * @param uiState Const reference to the central UI state object.
 * @return Pointer to the held parameter mapping, or nullptr if none held.
 */
const ParamButtonMapping *getHeldParameterButton(const UIState &uiState);

#endif // BUTTON_MANAGER_H
