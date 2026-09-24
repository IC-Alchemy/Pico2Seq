#include "ButtonManager.h"

#include <cstring> // For strcmp in paramIdFromName

// =======================
//   PARAMETER NAME HELPERS
// =======================

/**
 * @brief Display names for the parameter lanes, keyed by ParamId.
 *
 * Array order matches ParamId so OLED labels and tile buttons stay in sync.
 */
const char *paramName(ParamId paramId)
{
  const auto *definition = parameterDefinition(paramId);
  return definition ? definition->name : "Unknown";
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
 * @brief Reset holds, pad timers, and modes to the boot state.
 *
 * Gives the performer a known grid: no stuck holds, no half-open editor.
 *
 * @param uiState Reference to the central UI state object to initialize
 */
void initButtonManager(UIState &uiState)
{
  // Reset all parameter button hold states (no stuck lanes after boot).
  for (int paramIndex = 0; paramIndex < PARAM_ID_COUNT; ++paramIndex)
  {
    uiState.parameterButtonHeld[paramIndex] = false;
  }

  // Clear pad timers so no boot-time release reads as a hold.
  for (int stepIndex = 0; stepIndex < SequencerConstants::MAX_STEPS_COUNT; ++stepIndex)
  {
    uiState.padPressTimestamps[stepIndex] = 0;
  }

  // Initialize UI mode states
  uiState.modGateParamSeqLengthsMode = false;
  uiState.slideMode = false;
  uiState.selectedVoiceIndex = 0;
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
 * @brief Tap vs hold split for step pads and tile buttons.
 *
 * Below threshold the performer toggles a step; at/above, the step opens for
 * editing. Polled from the control loop, never blocking.
 *
 * @param pressDurationMs Duration of button press in milliseconds
 * @return true if duration exceeds long press threshold (400ms)
 */
bool isLongPress(unsigned long pressDurationMs)
{
  return pressDurationMs >= UITimingConstants::LONG_PRESS_THRESHOLD_MS;
}

/**
 * @brief True while a parameter lane is held (pads then set its track length).
 *
 * Slide is skipped while slide mode owns the pads, so one gesture cannot both
 * toggle legato and rewrite a track length.
 *
 * @param uiState Const reference to the central UI state object
 * @return true if any parameter button is currently held, false otherwise
 */
bool isAnyParameterButtonHeld(const UIState &uiState)
{
  for (uint8_t paramIndex = 0; paramIndex < PARAM_ID_COUNT; ++paramIndex)
  {
    const ParamId currentParamId = static_cast<ParamId>(paramIndex);

    // Slide is modal: while it owns the pads it must not also count as a held lane.
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
 * @brief Which lane the pads currently address (first held in ParamId order).
 *
 * The held lane decides what a pad tap writes: track length, not a gate flip.
 *
 * @param uiState Const reference to the central UI state object
 * @return The held parameter's ParamId, or ParamId::Count if none held
 */
ParamId getHeldParameterParamId(const UIState &uiState)
{
  for (uint8_t paramIndex = 0; paramIndex < PARAM_ID_COUNT; ++paramIndex)
  {
    const ParamId currentParamId = static_cast<ParamId>(paramIndex);

    // Slide is modal: while it owns the pads it must not also count as a held lane.
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
