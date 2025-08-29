#include "UIEventHandler.h"
#include "../midi/MidiManager.h"
#include "../scales/scales.h"
#include "../sequencer/Sequencer.h"
#include "../sequencer/ShuffleTemplates.h"
#include "../voice/Voice.h"
#include "../voice/VoiceManager.h"
#include "../voice/VoicePresets.h"
#include "../voice/VoiceSystem.h"
#include "ButtonManager.h"
#include "ButtonHandlers.h"
#include "UIConstants.h"
#include <uClock.h>

// =======================
//   UI EVENT CONSTANTS
// =======================

namespace UIEventConstants
{
  // Voice system constants
  static constexpr uint8_t MAX_VOICES = 4;
  static constexpr uint8_t VOICE_1_INDEX = 0;
  static constexpr uint8_t VOICE_2_INDEX = 1;
  static constexpr uint8_t VOICE_3_INDEX = 2;
  static constexpr uint8_t VOICE_4_INDEX = 3;

  // Default voice preset indices
  static constexpr uint8_t DEFAULT_VOICE_1_PRESET = 0; // Analog preset
  static constexpr uint8_t DEFAULT_VOICE_2_PRESET = 1; // Digital preset

  // Settings mode constants
  static constexpr uint8_t SETTINGS_MENU_VOICE_COUNT = 4;
  static constexpr uint8_t SETTINGS_MENU_INITIAL_INDEX = 0;
  static constexpr uint8_t SETTINGS_SUBMENU_INITIAL_INDEX = 0;

  // Slide mode constants
  static constexpr uint8_t SLIDE_OFF_VALUE = 0;
  static constexpr uint8_t SLIDE_ON_VALUE = 1;

  // Voice parameter button range (buttons 9-24 in settings mode)
  static constexpr uint8_t VOICE_PARAM_BUTTON_MIN = 8;
  static constexpr uint8_t VOICE_PARAM_BUTTON_MAX = 24;

  // Filter mode cycling constants
  static constexpr int FILTER_MODE_COUNT = 5;
  static constexpr float FILTER_RESONANCE_STEP = 0.025f;
  static constexpr float FILTER_RESONANCE_MAX = 1.0f;
  static constexpr float FILTER_RESONANCE_MIN = 0.0f;
}

static_assert(UIState::NUM_RANDOMIZE >= UIEventConstants::MAX_VOICES,
              "UI expects 4 randomize buttons; update UIState::NUM_RANDOMIZE or adjust handlers.");

// External function declarations that the UI calls
extern void onClockStart();
extern void onClockStop();
extern void setLEDTheme(LEDTheme theme);
extern void applyVoicePreset(uint8_t voiceNumber, uint8_t presetIndex);

// External variables that are still needed from the main file
extern uint8_t currentScale;
extern bool isClockRunning;
extern const ParameterDefinition CORE_PARAMETERS[];

// Voice system external declarations
extern std::unique_ptr<VoiceManager> voiceManager;
extern VoiceSystem voiceSystem;
extern Sequencer seq1;
extern Sequencer seq2;
extern Sequencer seq3;
extern Sequencer seq4;
extern float delayTarget;

// Helper function declarations (static to this file)
static bool handleParameterButtonEvent(const MatrixButtonEvent &evt,
                                       UIState &uiState);
static bool handleStepButtonEvent(const MatrixButtonEvent &evt,
                                  UIState &uiState, Sequencer *const *sequencers,
                                  size_t sequencerCount);

// Private helper handlers for matrixEventHandler
static void handleSlideModeToggle(const MatrixButtonEvent &evt, UIState &uiState);
static void handleVoiceSwitch(const MatrixButtonEvent &evt, UIState &uiState, MidiNoteManager &midiNoteManager);
static void handleSlideModeStep(const MatrixButtonEvent &evt, UIState &uiState, Sequencer *const *sequencers, size_t sequencerCount);
static void handleRandomizeMatrixButtons(const MatrixButtonEvent &evt, UIState &uiState);
static void handlePlayStopButton(const MatrixButtonEvent &evt, UIState &uiState);
static void handleAS5600ControlButton(const MatrixButtonEvent &evt, UIState &uiState);
static void handleOtherControlButtons(const MatrixButtonEvent &evt, UIState &uiState);
// Optional micro-helpers for randomize buttons
static inline bool isRandomizeButton(uint8_t idx);
static uint8_t resolveRandomizeVoiceIndex(uint8_t pressedRandomizeButton, const UIState &uiState);

static void autoSelectAS5600Parameter(ParamId paramId, UIState &uiState);
static void handleAS5600ParameterControl(UIState &uiState);

// Settings sub-mode helpers (settings mode refactor)
static void toggleSettingsSubMode(UIState &uiState);
static void handlePresetSelection(const MatrixButtonEvent &evt, UIState &uiState);
static void handleVoiceParameter(const MatrixButtonEvent &evt, UIState &uiState, VoiceManager *voiceManager);

static void handleAS5600ControlButton(const MatrixButtonEvent &evt, UIState &uiState)
{
  if (evt.type == MATRIX_BUTTON_PRESSED)
  {
    uiState.as5600ControlPressTime = millis();
    uiState.as5600ControlWasPressed = true;
    return;
  }

  if (evt.type == MATRIX_BUTTON_RELEASED && uiState.as5600ControlWasPressed)
  {
    unsigned long pressDurationMs = millis() - uiState.as5600ControlPressTime;
    uiState.as5600ControlWasPressed = false;

    if (!isLongPress(pressDurationMs))
    {
      // Short press behavior
      // In Settings mode while stopped, Button 25 toggles between sub-modes.
      // Otherwise, keep the existing AS5600 parameter cycling behavior.
      if (uiState.settingsMode && !isClockRunning)
      {
        toggleSettingsSubMode(uiState);
        uiState.selectedStepForEdit = -1;
      }
      else
      {
        // Existing behavior outside of settings: cycle AS5600 parameter
        handleControlButton(BUTTON_AS5600_CONTROL, uiState);
      }
    }

    // Exit gate sequence length mode on release
    uiState.gateSeqLengthMode = false;
    uiState.selectedStepForEdit = -1;
  }
}

/**
 * @brief Primary matrix event handler that always uses the provided sequencer array.
 *
 * This is the canonical implementation: it selects sequencers exclusively from the
 * caller-supplied array and does not fall back to any globals. Overloads for
 * convenience (2-seq / 4-seq) forward to this implementation.
 */
void matrixEventHandler(const MatrixButtonEvent &evt, UIState &uiState,
                        Sequencer *const *sequencers, size_t sequencerCount,
                        MidiNoteManager &midiNoteManager)
{

  // Poll held buttons (long press detection) using the supplied array.
  pollUIHeldButtons(uiState, sequencers, sequencerCount);

  // =======================
  //   SLIDE MODE HANDLING
  // =======================

  /**
   * Handle slide mode toggle button (Button 22)
   *
   * Slide mode allows users to toggle slide/legato between notes in the sequence.
   * When enabled, it disables other conflicting modes and allows setting slide
   * per step by pressing step buttons.
   */
  if (evt.buttonIndex == BUTTON_SLIDE_MODE)
  {
    if (evt.type == MATRIX_BUTTON_PRESSED)
    {
      handleSlideModeToggle(evt, uiState);
    }
    return; // Exit after handling slide mode toggle
  }

  // =======================
  //   VOICE SWITCHING
  // =======================

  /**
   * Handle Voice Switch button (Button 24) - cycles through 4 voices
   *
   * Allows switching between the 4 available voices for editing and playback.
   * Each voice has its own sequencer, parameters, and sound characteristics.
   * Switching voices changes which sequencer is being edited.
   */
  if (evt.buttonIndex == BUTTON_VOICE_SWITCH)
  {
    if (evt.type == MATRIX_BUTTON_PRESSED)
    {
      handleVoiceSwitch(evt, uiState, midiNoteManager);
    }
    return; // Exit after handling voice switch
  }

  // =======================
  //   SLIDE MODE STEP HANDLING
  // =======================

  /**
   * Handle step buttons in slide mode - toggle slide per step
   *
   * When in slide mode, step buttons toggle the slide parameter for individual
   * steps rather than toggling the step on/off. This allows fine control over
   * which notes in the sequence have smooth pitch transitions.
   */
  if (uiState.slideMode && evt.buttonIndex < NUMBER_OF_STEP_BUTTONS)
  {
    if (evt.type == MATRIX_BUTTON_PRESSED)
    {
      handleSlideModeStep(evt, uiState, sequencers, sequencerCount);
    }
    return; // In slide mode, step buttons only toggle slide
  }

  // Handle other buttons
  // Dedicated handling for AS5600 control to support hold-mode
  if (evt.buttonIndex == BUTTON_AS5600_CONTROL)
  {
    handleAS5600ControlButton(evt, uiState);
    return;
  }
  if (handleParameterButtonEvent(evt, uiState))
    return;
  if (handleStepButtonEvent(evt, uiState, sequencers, sequencerCount))
    return;

  // =======================
  //   RANDOMIZE BUTTON HANDLING
  // =======================

  /**
   * Handle randomize buttons for all 4 voices
   *
   * Each voice has its own randomize button that supports two functions:
   * - Short press: Randomize the sequence parameters
   * - Long press: Reset the sequence to empty state
   *
   * The timing detection is handled by beginRandomizePress/handleRandomizeButton
   * functions which track press duration and trigger appropriate actions.
   */

  // Delegate handling of randomize buttons (BUTTON_RANDOMIZE_SEQ1 and BUTTON_RANDOMIZE_SEQ2)
  if (evt.buttonIndex == BUTTON_RANDOMIZE_SEQ1 || evt.buttonIndex == BUTTON_RANDOMIZE_SEQ2)
  {
    handleRandomizeMatrixButtons(evt, uiState);
    return; // Exit after handling randomize button
  }

  // =======================
  //   PLAY/STOP BUTTON HANDLING
  // =======================

  /**
   * Handle PLAY_STOP button with long press detection for settings mode
   *
   * The play/stop button has dual functionality:
   * - Short press: Start/stop the sequencer transport
   * - Long press (when stopped): Enter settings mode for voice configuration
   *
   * Long press detection prevents accidental settings mode entry during performance.
   */

  // =======================
  //   OTHER CONTROL BUTTONS
  // =======================

  /**
   * Handle remaining control buttons (only on press events)
   *
   * Delegates to handleControlButton for processing other control functions
   * like scale changes, theme changes, delay toggle, etc. Only processes
   * press events to avoid double-triggering on release.
   */
  handleOtherControlButtons(evt, uiState);
}

/**
 * Backwards-compatible convenience overload (2 sequencers).
 * Forwards to the canonical array-based implementation.
 */
void matrixEventHandler(const MatrixButtonEvent &evt, UIState &uiState,
                        Sequencer &seq1, Sequencer &seq2,
                        MidiNoteManager &midiNoteManager)
{
  Sequencer *sequencerArray[2] = {&seq1, &seq2};
  matrixEventHandler(evt, uiState, sequencerArray, 2, midiNoteManager);
}

/**
 * Compatibility overload for 4-sequencer matrix event handling (kept for callers).
 * Forwarder to the canonical array-based implementation.
 */
void matrixEventHandler(const MatrixButtonEvent &evt, UIState &uiState,
                        Sequencer &seq1Ref, Sequencer &seq2Ref,
                        Sequencer &seq3Ref, Sequencer &seq4Ref,
                        MidiNoteManager &midiNoteManager)
{
  Sequencer *sequencerArray[UIEventConstants::MAX_VOICES] = {
      &seq1Ref, &seq2Ref, &seq3Ref, &seq4Ref};
  matrixEventHandler(evt, uiState, sequencerArray, UIEventConstants::MAX_VOICES, midiNoteManager);
}

// =======================
//   INTERNAL HANDLERS
// =======================

/**
 * @brief Handles parameter button events from the matrix button grid
 *
 * Processes button events for parameter control buttons (Note, Velocity, Filter, etc.).
 * Updates the UI state to track which parameter buttons are held, enabling
 * parameter editing mode when step buttons are pressed. Also handles automatic
 * AS5600 parameter selection for real-time control.
 *
 * @param evt Matrix button event containing button index and press/release type
 * @param uiState Reference to the UI state object for tracking button states
 * @return true if the event was handled as a parameter button event, false otherwise
 */
static bool handleParameterButtonEvent(const MatrixButtonEvent &evt,
                                       UIState &uiState)
{
  // Block parameter button handling when in slide mode to avoid conflicts
  if (uiState.slideMode)
  {
    return false;
  }

  // Search through parameter button mappings to find a match
  for (size_t mappingIndex = 0; mappingIndex < PARAM_BUTTON_MAPPINGS_SIZE; ++mappingIndex)
  {
    const auto &currentMapping = PARAM_BUTTON_MAPPINGS[mappingIndex];

    // Check if this button event matches a parameter button
    if (evt.buttonIndex == currentMapping.buttonIndex)
    {
      // Update parameter button held state based on press/release
      bool isButtonPressed = (evt.type == MATRIX_BUTTON_PRESSED);
      uiState.parameterButtonHeld[static_cast<int>(currentMapping.paramId)] = isButtonPressed;

      // Automatically select AS5600 parameter for real-time control (except Note parameter)
      if (isButtonPressed && currentMapping.paramId != ParamId::Note)
      {
        autoSelectAS5600Parameter(currentMapping.paramId, uiState);
      }

      // Handle parameter editing in step edit mode
      if (isButtonPressed && uiState.selectedStepForEdit >= 0)
      {
        if (uiState.currentEditParameter == currentMapping.paramId)
        {
          // Toggle off - stop editing this parameter
          uiState.currentEditParameter = ParamId::Count;
        }
        else
        {
          // Toggle on - start editing this parameter
          uiState.currentEditParameter = currentMapping.paramId;
          autoSelectAS5600Parameter(currentMapping.paramId, uiState);
        }
      }
      return true; // Event was handled as parameter button
    }
  }
  return false; // Event was not a parameter button
}

/**
 * @brief Handles step button events for sequence programming and settings navigation
 *
 * Processes step button presses for multiple functions depending on current mode:
 * - Normal mode: Toggle steps on/off (short press) or enter edit mode (long press)
 * - Parameter hold mode: Set parameter lengths for the held parameter
 * - Settings mode: Navigate voice presets and voice parameter configuration
 *
 * @param evt Matrix button event containing button index and press/release type
 * @param uiState Reference to the UI state object for tracking modes and timing
 * @param seq1 Reference to sequencer 1 (voice 1)
 * @param seq2 Reference to sequencer 2 (voice 2)
 * @param seq3 Reference to sequencer 3 (voice 3)
 * @param seq4 Reference to sequencer 4 (voice 4)
 * @return true if the event was handled as a step button event, false otherwise
 */
static bool handleStepButtonEvent(const MatrixButtonEvent &evt,
                                  UIState &uiState, Sequencer *const *sequencers,
                                  size_t sequencerCount)
{
  // Ignore out-of-bounds button indices
  if (evt.buttonIndex >= NUMBER_OF_STEP_BUTTONS)
  {
    return false;
  }

  // =======================
  //   GATE SEQ LENGTH MODE
  // =======================
  // While holding AS5600 control (long press), allow setting Gate track length (2-16)
  if (uiState.gateSeqLengthMode && evt.type == MATRIX_BUTTON_PRESSED)
  {
    // Resolve current active sequencer
    Sequencer *currentActiveSequencerPtr = nullptr;
    // 'sequencers' array is authoritative
    if (sequencers && uiState.selectedVoiceIndex < sequencerCount)
    {
      currentActiveSequencerPtr = sequencers[uiState.selectedVoiceIndex];
    }
    if (currentActiveSequencerPtr)
    {
      uint8_t requested = static_cast<uint8_t>(evt.buttonIndex + 1); // 1..16
      if (requested < 2) requested = 2;
      if (requested > 16) requested = 16;
      currentActiveSequencerPtr->setParameterStepCount(ParamId::Gate, requested);
      // Optional UI feedback flags
      uiState.resetStepsLightsFlag = true;
      uiState.selectedStepForEdit != -1;
    }
    return true; // consume event in this mode
  }

  // =======================
  //   SETTINGS MODE HANDLING
  // =======================

  /**
   * Handle settings mode navigation and voice configuration
   *
   * Settings mode allows configuration of voice presets and voice parameters.
   * Navigation uses step buttons for menu selection and preset application.
   */
  if (uiState.settingsMode && evt.type == MATRIX_BUTTON_PRESSED)
  {
    // Buttons 0-3 always select voice index (0..3)
    if (evt.buttonIndex < UIEventConstants::SETTINGS_MENU_VOICE_COUNT)
    {
      uiState.selectedVoiceIndex = evt.buttonIndex;
      uiState.isVoice2Mode = (uiState.selectedVoiceIndex == UIEventConstants::VOICE_2_INDEX); // legacy compat
      uiState.settingsMenuIndex = evt.buttonIndex; // used by OLED/LED menus
      return true;
    }

    // Route handling based on active sub-mode
    if (uiState.currentSubMode == UIState::SettingsSubMode::PRESET_SELECTION)
    {
      handlePresetSelection(evt, uiState);
    }
    else // VOICE_PARAMETER
    {
      handleVoiceParameter(evt, uiState, voiceManager.get());
    }
    return true; // Event was handled in settings mode
  }
  // =======================
  //   NORMAL STEP BUTTON HANDLING
  // =======================

  // Select current active sequencer based on selected voice index using supplied array
  Sequencer *currentActiveSequencerPtr = nullptr;
  if (sequencers && uiState.selectedVoiceIndex < sequencerCount)
  {
    currentActiveSequencerPtr = sequencers[uiState.selectedVoiceIndex];
  }

  if (!currentActiveSequencerPtr)
  {
    // No sequencer available for the selected voice — ignore step actions.
    return true;
  }

  Sequencer &currentActiveSequencer = *currentActiveSequencerPtr;

  // Handle parameter length adjustment when holding parameter buttons
  if (isAnyParameterButtonHeld(uiState) && evt.type == MATRIX_BUTTON_PRESSED)
  {
    const ParamButtonMapping *heldParameterMapping = getHeldParameterButton(uiState);
    if (heldParameterMapping)
    {
      uint8_t newParameterStepCount = evt.buttonIndex + 1; // Convert 0-based index to 1-based count
      currentActiveSequencer.setParameterStepCount(heldParameterMapping->paramId, newParameterStepCount);
      uiState.selectedStepForEdit != -1;
    }
    return true; // Event was handled as parameter length adjustment
  }

  // Handle normal step button presses (short/long press detection)
  if (!isAnyParameterButtonHeld(uiState))
  {
    if (evt.type == MATRIX_BUTTON_PRESSED)
    {
      // Record press timestamp for long press detection
      uiState.padPressTimestamps[evt.buttonIndex] = millis();
    }
    else if (evt.type == MATRIX_BUTTON_RELEASED)
    {
      unsigned long pressDurationMs = millis() - uiState.padPressTimestamps[evt.buttonIndex];
      uiState.padPressTimestamps[evt.buttonIndex] = 0; // Clear timestamp

      if (isLongPress(pressDurationMs))
      {
        // Long press: Toggle step edit mode for detailed parameter editing
        if (uiState.selectedStepForEdit == evt.buttonIndex)
        {
          // Exit edit mode for this step
          uiState.selectedStepForEdit = -1;
          uiState.currentEditParameter = ParamId::Count; // Clear edit parameter
        }
        else
        {
          // Enter edit mode for this step
          uiState.selectedStepForEdit = evt.buttonIndex;
        }
      }
      else
      {
        // Short press: Toggle step on/off and exit edit mode
        currentActiveSequencer.toggleStep(evt.buttonIndex);
        uiState.selectedStepForEdit = -1;
        uiState.currentEditParameter = ParamId::Count; // Clear edit parameter
      }
    }
  }
  return true; // Event was handled as step button
}

// Obsolete static handleControlButtonEvent removed; logic is centralized in handleControlButton (ButtonHandlers.cpp)

static void autoSelectAS5600Parameter(ParamId paramId, UIState &uiState)
{
  AS5600ParameterMode newAS5600Param;
  bool isValid = true;
  switch (paramId)
  {
  case ParamId::Note:
    newAS5600Param = AS5600ParameterMode::Note;
    break;
  case ParamId::Velocity:
    newAS5600Param = AS5600ParameterMode::Velocity;
    break;
  case ParamId::Filter:
    newAS5600Param = AS5600ParameterMode::Filter;
    break;
  case ParamId::Attack:
    newAS5600Param = AS5600ParameterMode::Attack;
    break;
  case ParamId::Decay:
    newAS5600Param = AS5600ParameterMode::Decay;
    break;
  default:
    isValid = false;
    break;
  }

  if (isValid && newAS5600Param != uiState.currentAS5600Parameter)
  {
    uiState.currentAS5600Parameter = newAS5600Param;
    // Serial.print("AS5600 auto-selected: ");
    // Serial.println(CORE_PARAMETERS[static_cast<int>(paramId)].name);
  }
}

static void handleAS5600ParameterControl(UIState &uiState)
{
  uiState.currentAS5600Parameter = static_cast<AS5600ParameterMode>(
      (static_cast<uint8_t>(uiState.currentAS5600Parameter) + 1) %
      static_cast<uint8_t>(AS5600ParameterMode::COUNT));

  uiState.lastAS5600ButtonPressTime = millis();

}

// =======================
// Settings sub-mode helpers
// =======================

/**
 * Toggle Settings sub-mode between Preset Selection and Voice Parameter.
 * Also updates legacy flags for backward compatibility.
 */
static void toggleSettingsSubMode(UIState &uiState)
{
  using Sub = UIState::SettingsSubMode;
  uiState.currentSubMode =
      (uiState.currentSubMode == Sub::PRESET_SELECTION) ? Sub::VOICE_PARAMETER : Sub::PRESET_SELECTION;

  // Legacy flags kept in sync for existing renderers/logic
  uiState.inPresetSelection     = (uiState.currentSubMode == Sub::PRESET_SELECTION);
  uiState.inVoiceParameterMode  = (uiState.currentSubMode == Sub::VOICE_PARAMETER);
}

/**
 * Handle Preset Selection sub-mode.
 * - Buttons 0-3 (handled in caller) select current voice.
 * - Buttons 8..14 select and apply preset for selected voice.
 * - Remain in Preset Selection mode after applying a preset.
 */
static void handlePresetSelection(const MatrixButtonEvent &evt, UIState &uiState)
{
  if (evt.type != MATRIX_BUTTON_PRESSED) return;

  const uint8_t baseButton = 8;
  const uint8_t presetCount = VoicePresets::getPresetCount();
  const uint8_t maxAllowedButton = 14; // limit per spec (buttons 8-14)
  const uint8_t maxButtonByPreset = static_cast<uint8_t>(baseButton + (presetCount ? presetCount - 1 : 0));
  const uint8_t maxButton = (maxButtonByPreset < maxAllowedButton) ? maxButtonByPreset : maxAllowedButton;

  if (evt.buttonIndex >= baseButton && evt.buttonIndex <= maxButton)
  {
    const uint8_t presetIndex = static_cast<uint8_t>(evt.buttonIndex - baseButton);

    // Apply to currently selected voice (1..4 for applyVoicePreset)
    const uint8_t voiceIdx = uiState.selectedVoiceIndex;
    if (voiceIdx < UIEventConstants::MAX_VOICES)
    {
      uiState.voicePresetIndices[voiceIdx] = presetIndex;
      applyVoicePreset(static_cast<uint8_t>(voiceIdx + 1), presetIndex);
    }

    // Stay in Preset Selection mode; do not auto-switch.
    uiState.inPresetSelection    = true;  // legacy flag mirror
    uiState.inVoiceParameterMode = false; // legacy flag mirror
  }
}

/**
 * Handle Voice Parameter sub-mode.
 * - Buttons 8..15 perform parameter toggles/adjustments for current voice.
 * - Buttons 16..24 reserved/ignored (with optional debug prints).
 * Only active when currentSubMode == VOICE_PARAMETER.
 */
static void handleVoiceParameter(const MatrixButtonEvent &evt, UIState &uiState, VoiceManager *voiceManager)
{
  if (evt.type != MATRIX_BUTTON_PRESSED) return;

  // Only respond to parameter button range 8..24; active range 8..15 as defined today
  if (evt.buttonIndex < UIEventConstants::VOICE_PARAM_BUTTON_MIN ||
      evt.buttonIndex > UIEventConstants::VOICE_PARAM_BUTTON_MAX ||
      voiceManager == nullptr)
  {
    return;
  }

  // Resolve current voice configuration
  const uint8_t selectedVoiceIndex = uiState.selectedVoiceIndex;
  const uint8_t currentVoiceId = voiceSystem.getVoiceId(selectedVoiceIndex);
  VoiceConfig *voiceConfig = voiceManager->getVoiceConfig(currentVoiceId);
  if (!voiceConfig) return;

  // UI feedback bookkeeping
  uiState.inVoiceParameterMode = true; // legacy flag mirror of active sub-mode
  uiState.lastVoiceParameterButton = evt.buttonIndex;
  uiState.voiceParameterChangeTime = millis();

  const uint8_t displayVoiceNumber = static_cast<uint8_t>(selectedVoiceIndex + 1);

  switch (evt.buttonIndex)
  {
    case 8: // Toggle envelope on/off
      voiceConfig->hasEnvelope = !voiceConfig->hasEnvelope;
      Serial.print("Voice "); Serial.print(displayVoiceNumber);
      Serial.print(" envelope "); Serial.println(voiceConfig->hasEnvelope ? "ON" : "OFF");
      break;

    case 9: // Toggle overdrive
      voiceConfig->hasOverdrive = !voiceConfig->hasOverdrive;
      Serial.print("Voice "); Serial.print(displayVoiceNumber);
      Serial.print(" overdrive "); Serial.println(voiceConfig->hasOverdrive ? "ON" : "OFF");
      break;

    case 10: // Toggle wavefolder
      voiceConfig->hasWavefolder = !voiceConfig->hasWavefolder;
      Serial.print("Voice "); Serial.print(displayVoiceNumber);
      Serial.print(" wavefolder "); Serial.println(voiceConfig->hasWavefolder ? "ON" : "OFF");
      break;

    case 11: // Cycle filter mode
    {
      int currentFilterMode = static_cast<int>(voiceConfig->filterMode);
      currentFilterMode = (currentFilterMode + 1) % UIEventConstants::FILTER_MODE_COUNT;
      voiceConfig->filterMode = static_cast<daisysp::LadderFilter::FilterMode>(currentFilterMode);

      const char *filterModeNames[] = {"LP12", "LP24", "LP36", "BP12", "BP24"};
      Serial.print("Voice "); Serial.print(displayVoiceNumber);
      Serial.print(" filter mode: "); Serial.println(filterModeNames[currentFilterMode]);
    }
    break;

    case 12: // Step filter resonance
    {
      float currentResonance = voiceConfig->filterRes;
      currentResonance += UIEventConstants::FILTER_RESONANCE_STEP;
      if (currentResonance > UIEventConstants::FILTER_RESONANCE_MAX)
      {
        currentResonance = UIEventConstants::FILTER_RESONANCE_MIN;
      }
      voiceConfig->filterRes = currentResonance;
      // Serial.print("Voice "); Serial.print(displayVoiceNumber);
      // Serial.print(" filter resonance: "); Serial.println(currentResonance, 2);
    }
    break;

    case 13: // Set delay time to dotted quarter
    {
      float currentTempo = uClock.getTempo();
      if (currentTempo < 1.0f) currentTempo = 1.0f;
      const float dottedQuarterMs = 90000.0f / currentTempo; // 1.5 * (60000/BPM)
      delayTarget = dottedQuarterMs * 48.0f; // 48kHz -> 48 samples/ms
      // Serial.print("Delay time set to dotted quarter: "); Serial.println(dottedQuarterMs, 2);
    }
    break;

    case 14: // Tempo -5 with floor at 55
    {
      float currentTempo = uClock.getTempo();
      uClock.setTempo(currentTempo - 5);
      if (currentTempo < 55) { uClock.setTempo(55); }
    }
    break;

    case 15: // Tempo +5 with ceiling at 160
    {
      float currentTempo = uClock.getTempo();
      uClock.setTempo(currentTempo + 5);
      if (currentTempo > 160) { uClock.setTempo(160); }
    }
    break;

    default:
      // Buttons 16-24 reserved (ignored)
      Serial.print("Voice parameter button ");
      Serial.print(evt.buttonIndex);
      Serial.println(" - reserved");
      break;
  }

  // Apply updated configuration back to voice manager
  voiceManager->setVoiceConfig(currentVoiceId, *voiceConfig);
}
/**
 * @brief Poll for long press detection on randomize buttons
 *
 * Continuously checks if any randomize buttons have been held long enough
 * to trigger a sequence reset. This function must be called regularly
 * to ensure responsive long press detection during button holds.
 *
 * @param uiState Reference to UI state containing button press timing data
 * @param seq1 Reference to sequencer 1 for potential reset
 * @param seq2 Reference to sequencer 2 for potential reset
 */
void pollUIHeldButtons(UIState &uiState, Sequencer *const *sequencers, size_t sequencerCount)
{
  unsigned long currentTimeMs = millis();

  // Check for long press resets on all supported voices (up to MAX_VOICES)
  for (uint8_t voiceIndex = 0; voiceIndex < UIEventConstants::MAX_VOICES; voiceIndex++)
  {
    if (uiState.randomizeWasPressed[voiceIndex] &&
        !uiState.randomizeResetTriggered[voiceIndex])
    {
      unsigned long pressDurationMs = currentTimeMs - uiState.randomizePressTime[voiceIndex];
      if (isLongPress(pressDurationMs))
      {
        // Use the supplied sequencer for this voice when available; otherwise ignore.
        Sequencer *targetSequencer = nullptr;
        if (sequencers && voiceIndex < sequencerCount)
        {
          targetSequencer = sequencers[voiceIndex];
        }

        if (targetSequencer)
        {
          targetSequencer->resetAllSteps();
          uiState.resetStepsLightsFlag = true;
          uiState.randomizeResetTriggered[voiceIndex] = true;

        }
      }
    }
  }

  // Detect long hold of AS5600 control to enter Gate Sequence Length mode
  // Suppress this feature while in settings menus (stopped state)
  if (uiState.as5600ControlWasPressed && !uiState.gateSeqLengthMode && !uiState.settingsMode)
  {
    unsigned long pressDurationMs = currentTimeMs - uiState.as5600ControlPressTime;
    if (isLongPress(pressDurationMs))
    {
      uiState.gateSeqLengthMode = true;
      // Clear conflicting modes when entering this mode
      uiState.slideMode = false;
      for (int paramIndex = 0; paramIndex < PARAM_ID_COUNT; ++paramIndex)
      {
        uiState.parameterButtonHeld[paramIndex] = false;
      }
      uiState.selectedStepForEdit = -1;
    }
  }
  // Safety: if the AS5600 control is no longer held, ensure we exit the mode
  else if (!uiState.as5600ControlWasPressed && uiState.gateSeqLengthMode)
  {
    uiState.gateSeqLengthMode = false;
          uiState.selectedStepForEdit = -1;

  }
}

/*
 * Backwards-compatible convenience overloads for pollUIHeldButtons.
 * These restore the previous call patterns that passed individual Sequencer references.
 * They construct a small local array of Sequencer* and forward to the canonical implementation.
 */

// 2-sequencer overload
void pollUIHeldButtons(UIState &uiState, Sequencer &seq1, Sequencer &seq2)
{
  Sequencer *sequencers[2] = {&seq1, &seq2};
  pollUIHeldButtons(uiState, sequencers, 2);
}

// 4-sequencer overload
void pollUIHeldButtons(UIState &uiState, Sequencer &seq1, Sequencer &seq2,
                       Sequencer &seq3, Sequencer &seq4)
{
  Sequencer *sequencers[UIEventConstants::MAX_VOICES] = {&seq1, &seq2, &seq3, &seq4};
  pollUIHeldButtons(uiState, sequencers, UIEventConstants::MAX_VOICES);
}

static void handleSlideModeToggle(const MatrixButtonEvent &evt, UIState &uiState)
{
  // Toggle slide mode state
  uiState.slideMode = !uiState.slideMode;

  if (uiState.slideMode)
  {
    // Clear conflicting modes when entering slide mode
    for (int paramIndex = 0; paramIndex < PARAM_ID_COUNT; ++paramIndex)
    {
      uiState.parameterButtonHeld[paramIndex] = false;
    }
    uiState.modGateParamSeqLengthsMode = false;
    uiState.gateSeqLengthMode = false;
    uiState.selectedStepForEdit = -1;
  }
  else
  {
  }
}

static void handleVoiceSwitch(const MatrixButtonEvent &evt, UIState &uiState, MidiNoteManager &midiNoteManager)
{
  midiNoteManager.onModeSwitch();

  // Cycle to next voice (0-3, wrapping around)
  uiState.selectedVoiceIndex = (uiState.selectedVoiceIndex + 1) % UIEventConstants::MAX_VOICES;
  uiState.isVoice2Mode = (uiState.selectedVoiceIndex == UIEventConstants::VOICE_2_INDEX); // Legacy compatibility
  uiState.selectedStepForEdit = -1;                                                       // Clear step editing when switching voices
  uiState.voiceSwitchTriggered = true;                                                    // Set flag for immediate OLED update

  //Serial.print("Switched to Voice ");
//  Serial.println(uiState.selectedVoiceIndex + 1);
}

static void handleSlideModeStep(const MatrixButtonEvent &evt, UIState &uiState, Sequencer *const *sequencers, size_t sequencerCount)
{
  // Get the currently active sequencer pointer from the provided array (no globals)
  Sequencer *activeSequencerPtr = nullptr;
  if (sequencers && uiState.selectedVoiceIndex < sequencerCount)
  {
    activeSequencerPtr = sequencers[uiState.selectedVoiceIndex];
  }

  if (activeSequencerPtr)
  {
    Sequencer &currentActiveSequencer = *activeSequencerPtr;

    // Get current slide value and toggle it
    uint8_t currentSlideValue = currentActiveSequencer.getStepParameterValue(
        ParamId::Slide, evt.buttonIndex);
    uint8_t newSlideValue = (currentSlideValue > UIEventConstants::SLIDE_OFF_VALUE) ? UIEventConstants::SLIDE_OFF_VALUE : UIEventConstants::SLIDE_ON_VALUE;

    // Apply the new slide value to the step
    currentActiveSequencer.setStepParameterValue(ParamId::Slide, evt.buttonIndex, newSlideValue);

  }
  else
  {
    // No sequencer available for the selected voice; ignore.
  }
}

static inline bool isRandomizeButton(uint8_t idx)
{
  return (idx == BUTTON_RANDOMIZE_SEQ1) || (idx == BUTTON_RANDOMIZE_SEQ2);
}

static uint8_t resolveRandomizeVoiceIndex(uint8_t pressedRandomizeButton, const UIState &uiState)
{
  bool firstPage = (uiState.selectedVoiceIndex <= UIEventConstants::VOICE_2_INDEX);
  if (pressedRandomizeButton == BUTTON_RANDOMIZE_SEQ1)
  {
    return firstPage ? UIEventConstants::VOICE_1_INDEX : UIEventConstants::VOICE_3_INDEX;
  }
  else
  {
    return firstPage ? UIEventConstants::VOICE_2_INDEX : UIEventConstants::VOICE_4_INDEX;
  }
}

static void handleRandomizeMatrixButtons(const MatrixButtonEvent &evt, UIState &uiState)
{
  if (!isRandomizeButton(evt.buttonIndex))
  {
    return;
  }

  uint8_t voiceIndex = resolveRandomizeVoiceIndex(evt.buttonIndex, uiState);

  if (evt.type == MATRIX_BUTTON_PRESSED)
  {
    beginRandomizePress(voiceIndex, uiState);
  }
  else if (evt.type == MATRIX_BUTTON_RELEASED)
  {
    handleRandomizeButton(voiceIndex, uiState);
  }
}

static void handleOtherControlButtons(const MatrixButtonEvent &evt, UIState &uiState)
{
  // Handle remaining control buttons (only on press events)
  if (evt.type == MATRIX_BUTTON_PRESSED)
  {
    // AS5600 control is handled separately to support hold behavior
    if (evt.buttonIndex != BUTTON_AS5600_CONTROL)
    {
      handleControlButton(evt.buttonIndex, uiState);
    }
  }
}
