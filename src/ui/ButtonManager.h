#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <Arduino.h>
#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include "UIState.h" // Include the new state header

/**
 * @brief Tap/hold timing and parameter-hold queries for the UI.
 *
 * Tap (toggle step) vs hold (edit step) splits at LONG_PRESS_THRESHOLD_MS;
 * debounce lives here, holds live in UIState. Both surfaces (pads + tiles)
 * share these so gestures feel identical. Add new timing windows here.
 */

// UI Timing Constants (Core 0 control loop; all millis()-based, non-blocking).
namespace UITimingConstants
{
  // Tap vs hold split: below = toggle the step, at/above = open step-edit.
  static constexpr unsigned long LONG_PRESS_THRESHOLD_MS = 400;
  // Contact settle time before a second edge counts as a new press.
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
 * Parameter buttons are keyed by ParamId (the tiles carry them, not fixed
 * matrix indices). OLED labels round-trip through these helpers.
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
 * @brief Clear all holds/timestamps/modes to the boot state.
 * Call once at startup; transitions (not this) own mid-session resets.
 * @param uiState Reference to the central UI state object.
 */
void initButtonManager(UIState &uiState);

/**
 * @brief True once the press has lasted long enough to mean "edit", not "toggle".
 * @param pressDurationMs Duration of button press in milliseconds.
 * @return true if duration exceeds long press threshold (400ms).
 */
bool isLongPress(unsigned long pressDurationMs);

/**
 * @brief True while a parameter lane is held/latched (pads then set its length).
 * @param uiState Const reference to the central UI state object.
 * @return true if any parameter button is held.
 */
bool isAnyParameterButtonHeld(const UIState &uiState);

/**
 * @brief Which lane the pads currently address (first held in ParamId order).
 * @param uiState Const reference to the central UI state object.
 * @return The held parameter's ParamId, or ParamId::Count if none held.
 */
ParamId getHeldParameterParamId(const UIState &uiState);

#endif // BUTTON_MANAGER_H
