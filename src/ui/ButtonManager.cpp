#include "ButtonManager.h"

#include <cstring> // For strcmp in paramIdFromName

// =======================
//   PARAMETER NAME HELPERS
// =======================

/**
 * @brief Display names for the parameter buttons, keyed by ParamId.
 *
 * Parameter buttons live on the Alchemy ButtonModule8 tile (Shift-keyed,
 * mode-dependent) rather than at fixed matrix indices, so the only mapping
 * that remains is ParamId -> display name. Array order matches ParamId.
 */
const char *paramName(ParamId paramId)
{
  switch (paramId)
  {
  case ParamId::Note:
    return "Note";
  case ParamId::Velocity:
    return "Velocity";
  case ParamId::Filter:
    return "Filter";
  case ParamId::Attack:
    return "Attack";
  case ParamId::Decay:
    return "Decay";
  case ParamId::Octave:
    return "Octave";
  case ParamId::GateLength:
    return "GateLength";
  case ParamId::Gate:
    return "Gate";
  case ParamId::Slide:
    return "Slide";
  default:
    return "Unknown";
  }
}

ParamId paramIdFromName(const char *name)
{
  if (name == nullptr)
  {
    return ParamId::Count;
  }
  for (uint8_t i = 0; i < PARAM_ID_COUNT; ++i)
  {
    const ParamId paramId = static_cast<ParamId>(i);
    if (strcmp(paramName(paramId), name) == 0)
    {
      return paramId;
    }
  }
  return ParamId::Count;
}

// =======================
//   FUNCTION IMPLEMENTATIONS
// =======================

/**
 * @brief Initialize button manager state and reset all UI timing variables
 *
 * Resets all button-related states to their defaults, including parameter
 * button hold states, step press timestamps, and UI mode flags. This ensures
 * a clean starting state for the button management system.
 *
 * @param uiState Reference to the central UI state object to initialize
 */
void initButtonManager(UIState &uiState)
{
  // Reset all parameter button hold states
  for (int paramIndex = 0; paramIndex < PARAM_ID_COUNT; ++paramIndex)
  {
    uiState.parameterButtonHeld[paramIndex] = false;
  }

  // Reset all step button press timestamps for long press detection
  for (int stepIndex = 0; stepIndex < SequencerConstants::MAX_STEPS_COUNT; ++stepIndex)
  {
    uiState.padPressTimestamps[stepIndex] = 0;
  }

  // Initialize UI mode states
  uiState.delayOn = true;
  uiState.modGateParamSeqLengthsMode = false;
  uiState.slideMode = false;
  uiState.isVoice2Mode = false;
  uiState.selectedStepForEdit = -1;

  // Reset transient OLED notice state
  uiState.oledNoticeUntil = 0;
  uiState.oledNoticeKind = UIState::OledNoticeKind::None;
  uiState.oledNoticeVoice = 0;

  // Reset button press timing states
  uiState.lastEncoderButtonPressTime = 0;
  uiState.voiceSwitchPressTime = 0;
  uiState.voiceSwitchWasPressed = false;
  uiState.resetStepsLightsFlag = false;
}

/**
 * @brief Check if a button press duration qualifies as a long press
 *
 * Determines whether the given press duration exceeds the long press threshold.
 * Long presses are used for alternative functions like entering edit mode,
 * resetting sequences, or accessing settings.
 *
 * @param pressDurationMs Duration of button press in milliseconds
 * @return true if duration exceeds long press threshold (400ms)
 */
bool isLongPress(unsigned long pressDurationMs)
{
  return pressDurationMs >= UITimingConstants::LONG_PRESS_THRESHOLD_MS;
}

/**
 * @brief Check if any parameter button is currently being held
 *
 * Scans through all parameter button mappings to determine if any parameter
 * button is currently in a held state. This is used to determine UI behavior
 * when step buttons are pressed (parameter editing vs step toggling).
 *
 * @param uiState Const reference to the central UI state object
 * @return true if any parameter button is currently held, false otherwise
 */
bool isAnyParameterButtonHeld(const UIState &uiState)
{
  for (uint8_t paramIndex = 0; paramIndex < PARAM_ID_COUNT; ++paramIndex)
  {
    const ParamId currentParamId = static_cast<ParamId>(paramIndex);

    // Skip Slide parameter button if currently in slide mode to avoid conflicts
    if (currentParamId == ParamId::Slide && uiState.slideMode)
    {
      continue;
    }

    if (uiState.parameterButtonHeld[paramIndex])
    {
      return true;
    }
  }
  return false;
}

/**
 * @brief Get the ParamId of the currently held parameter button
 *
 * Scans the held states in ParamId order and returns the first held
 * parameter. This allows the UI to determine which parameter is being
 * controlled when step buttons are pressed for parameter editing.
 *
 * @param uiState Const reference to the central UI state object
 * @return The held parameter's ParamId, or ParamId::Count if none held
 */
ParamId getHeldParameterParamId(const UIState &uiState)
{
  for (uint8_t paramIndex = 0; paramIndex < PARAM_ID_COUNT; ++paramIndex)
  {
    const ParamId currentParamId = static_cast<ParamId>(paramIndex);

    // Skip Slide parameter button if currently in slide mode to avoid conflicts
    if (currentParamId == ParamId::Slide && uiState.slideMode)
    {
      continue;
    }

    if (uiState.parameterButtonHeld[paramIndex])
    {
      return currentParamId;
    }
  }
  return ParamId::Count;
}
