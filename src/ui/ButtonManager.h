#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <Arduino.h>
#include "../pico2seq-core/sequencer/SequencerDefs.h"
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
//   PARAMETER NAME HELPERS
// =======================

/**
 * Parameter buttons are keyed by ParamId everywhere (the physical buttons
 * live on the Alchemy ButtonModule8 tile, not at matrix indices anymore).
 * These helpers provide the display name and the reverse lookup so OLED
 * rendering can round-trip ParamId <-> name.
 */

/** Display name for a parameter ("Note", "Velocity", ...). */
const char *paramName(ParamId paramId);

/**
 * @brief Reverse lookup from a display name (see paramName) to its ParamId.
 * @return The matching ParamId, or ParamId::Count when the name is unknown.
 */
ParamId paramIdFromName(const char *name);

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
 * @brief Get the ParamId of the currently held parameter button.
 * @param uiState Const reference to the central UI state object.
 * @return The held parameter's ParamId, or ParamId::Count if none held.
 */
ParamId getHeldParameterParamId(const UIState &uiState);

#endif // BUTTON_MANAGER_H
