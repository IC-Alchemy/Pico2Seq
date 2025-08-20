#include "ButtonManager.h"



// =======================
//   BUTTON MAPPING ARRAY
// =======================

/**
 * @brief Parameter button mappings from physical button indices to parameter IDs
 * 
 * Maps the physical button matrix indices (16-22) to their corresponding
 * parameter functions. These buttons allow real-time parameter control
 * and step programming when held while pressing step buttons.
 */
const ParamButtonMapping PARAM_BUTTON_MAPPINGS[] = {
  {16, ParamId::Note,     "Note"},
  {17, ParamId::Velocity, "Velocity"},
  {18, ParamId::Filter,   "Filter"},
  {19, ParamId::Attack,   "Attack"},
  {20, ParamId::Decay,    "Decay"},
  {21, ParamId::Octave,   "Octave"},
  {22, ParamId::Slide,    "Slide"}
};

const size_t PARAM_BUTTON_MAPPINGS_SIZE = sizeof(PARAM_BUTTON_MAPPINGS) / sizeof(PARAM_BUTTON_MAPPINGS[0]);

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
void initButtonManager(UIState& uiState) {
  // Reset all parameter button hold states
  for (int paramIndex = 0; paramIndex < PARAM_ID_COUNT; ++paramIndex) {
    uiState.parameterButtonHeld[paramIndex] = false;
  }
  
  // Reset all step button press timestamps for long press detection
  for (int stepIndex = 0; stepIndex < SequencerConstants::MAX_STEPS_COUNT; ++stepIndex) {
    uiState.padPressTimestamps[stepIndex] = 0;
  }
  
  // Initialize UI mode states
  uiState.delayOn = true;
  uiState.modGateParamSeqLengthsMode = false;
  uiState.slideMode = false;
  uiState.isVoice2Mode = false;
  uiState.selectedStepForEdit = -1;
  
  // Reset LED flash timing states
  uiState.flash23Until = 0;
  uiState.flash25Until = 0;
  uiState.flash31Until = 0;
  
  // Reset button press timing states
  uiState.lastAS5600ButtonPressTime = 0;
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
bool isLongPress(unsigned long pressDurationMs) {
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
bool isAnyParameterButtonHeld(const UIState& uiState) {
  for (size_t mappingIndex = 0; mappingIndex < PARAM_BUTTON_MAPPINGS_SIZE; ++mappingIndex) {
    ParamId currentParamId = PARAM_BUTTON_MAPPINGS[mappingIndex].paramId;
    
    // Skip Slide parameter button if currently in slide mode to avoid conflicts
    if (currentParamId == ParamId::Slide && uiState.slideMode) {
      continue;
    }
    
    if (uiState.parameterButtonHeld[static_cast<int>(currentParamId)]) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Get the mapping for the currently held parameter button
 * 
 * Returns a pointer to the parameter button mapping that is currently being held.
 * This allows the UI to determine which parameter is being controlled when
 * step buttons are pressed for parameter editing.
 * 
 * @param uiState Const reference to the central UI state object
 * @return Pointer to the held parameter mapping, or nullptr if none held
 */
const ParamButtonMapping* getHeldParameterButton(const UIState& uiState) {
  for (size_t mappingIndex = 0; mappingIndex < PARAM_BUTTON_MAPPINGS_SIZE; ++mappingIndex) {
    const auto& currentMapping = PARAM_BUTTON_MAPPINGS[mappingIndex];
    
    // Skip Slide parameter button if currently in slide mode to avoid conflicts
    if (currentMapping.paramId == ParamId::Slide && uiState.slideMode) {
      continue;
    }
    
    if (uiState.parameterButtonHeld[static_cast<int>(currentMapping.paramId)]) {
      return &currentMapping;
    }
  }
  return nullptr;
}

