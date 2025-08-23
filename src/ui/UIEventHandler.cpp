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
  static constexpr uint8_t VOICE_PARAM_BUTTON_MIN = 9;
  static constexpr uint8_t VOICE_PARAM_BUTTON_MAX = 24;

  // Filter mode cycling constants
  static constexpr int FILTER_MODE_COUNT = 5;
  static constexpr float FILTER_RESONANCE_STEP = 0.1f;
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
static void handleOtherControlButtons(const MatrixButtonEvent &evt, UIState &uiState);
// Optional micro-helpers for randomize buttons
static inline bool isRandomizeButton(uint8_t idx);
static uint8_t resolveRandomizeVoiceIndex(uint8_t pressedRandomizeButton, const UIState &uiState);

static void autoSelectAS5600Parameter(ParamId paramId, UIState &uiState);
static void handleAS5600ParameterControl(UIState &uiState);

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
  if (evt.buttonIndex == BUTTON_PLAY_STOP)
  {
    handlePlayStopButton(evt, uiState);
    return; // Exit after handling play/stop button
  }

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
    if (uiState.inPresetSelection)
    {
      // Handle voice preset selection and application
      if (evt.buttonIndex < VoicePresets::getPresetCount())
      {
        uint8_t selectedPresetIndex = evt.buttonIndex;
        uint8_t targetVoiceNumber = uiState.settingsMenuIndex + 1; // Convert 0-3 to 1-4

        // Apply preset based on which voice is being configured
        if (uiState.settingsMenuIndex < UIEventConstants::MAX_VOICES)
        {
          uiState.voicePresetIndices[uiState.settingsMenuIndex] = selectedPresetIndex;
          applyVoicePreset(uiState.settingsMenuIndex + 1, selectedPresetIndex);
        }

        // Exit preset selection mode after applying preset
        uiState.inPresetSelection = false;
      }
    }
    else
    {
      // Handle voice parameter configuration (buttons 9-24)
      if (evt.buttonIndex >= UIEventConstants::VOICE_PARAM_BUTTON_MIN &&
          evt.buttonIndex <= UIEventConstants::VOICE_PARAM_BUTTON_MAX && voiceManager)
      {

        // Get the currently selected voice configuration
        uint8_t selectedVoiceIndex = uiState.selectedVoiceIndex;
        uint8_t currentVoiceId = voiceSystem.getVoiceId(selectedVoiceIndex);
        VoiceConfig *voiceConfig = voiceManager->getVoiceConfig(currentVoiceId);

        if (voiceConfig)
        {
          // Set voice parameter editing state for UI feedback
          uiState.inVoiceParameterMode = true;
          uiState.lastVoiceParameterButton = evt.buttonIndex;
          uiState.voiceParameterChangeTime = millis();

          // Display voice number (1-4) for user feedback
          uint8_t displayVoiceNumber = selectedVoiceIndex + 1;

          // Handle specific voice parameter buttons
          switch (evt.buttonIndex)
          {
          case 8: // Toggle envelope on/off
            voiceConfig->hasEnvelope = !voiceConfig->hasEnvelope;
            Serial.print("Voice ");
            Serial.print(displayVoiceNumber);
            Serial.print(" envelope ");
            Serial.println(voiceConfig->hasEnvelope ? "ON" : "OFF");
            break;

          case 9: // Toggle overdrive effect
            voiceConfig->hasOverdrive = !voiceConfig->hasOverdrive;
            Serial.print("Voice ");
            Serial.print(displayVoiceNumber);
            Serial.print(" overdrive ");
            Serial.println(voiceConfig->hasOverdrive ? "ON" : "OFF");
            break;

          case 10: // Toggle wavefolder effect
            voiceConfig->hasWavefolder = !voiceConfig->hasWavefolder;
            Serial.print("Voice ");
            Serial.print(displayVoiceNumber);
            Serial.print(" wavefolder ");
            Serial.println(voiceConfig->hasWavefolder ? "ON" : "OFF");
            break;

          case 11: // Cycle through filter modes
          {
            int currentFilterMode = static_cast<int>(voiceConfig->filterMode);
            currentFilterMode = (currentFilterMode + 1) % UIEventConstants::FILTER_MODE_COUNT;
            voiceConfig->filterMode = static_cast<daisysp::LadderFilter::FilterMode>(currentFilterMode);

            const char *filterModeNames[] = {"LP12", "LP24", "LP36", "BP12", "BP24"};
            Serial.print("Voice ");
            Serial.print(displayVoiceNumber);
            Serial.print(" filter mode: ");
            Serial.println(filterModeNames[currentFilterMode]);
          }
          break;

          case 12: // Cycle through filter resonance levels
          {
            float currentResonance = voiceConfig->filterRes;
            currentResonance += UIEventConstants::FILTER_RESONANCE_STEP;
            if (currentResonance > UIEventConstants::FILTER_RESONANCE_MAX)
            {
              currentResonance = UIEventConstants::FILTER_RESONANCE_MIN;
            }
            voiceConfig->filterRes = currentResonance;

          //  Serial.print("Voice ");
          //  Serial.print(displayVoiceNumber);
          //  Serial.print(" filter resonance: ");
          //  Serial.println(currentResonance, 2);
          }
          break;
          case 13:
          {
            float currentTempo = uClock.getTempo();
            if (currentTempo < 1.0f)
            {
              currentTempo = 1.0f;
            }
            // Dotted quarter-note duration in ms = 1.5 * (60000 / BPM) = 90000 / BPM
            float dottedQuarterMs = 90000.0f / currentTempo;
            // Convert ms to samples at 48kHz (48 samples per ms)
            delayTarget = dottedQuarterMs * 48.0f;
          //  Serial.print("Delay time set to dotted quarter: ");
          //  Serial.print(dottedQuarterMs, 2);
          //  Serial.println(" ms");
          }
          break;
          case 14:
          {
            float currentTempo = uClock.getTempo();

            uClock.setTempo(currentTempo - 5);

            if (currentTempo < 55)
            {
              uClock.setTempo(55);
            }
          }

          break;
          case 15:
          {
            float currentTempo = uClock.getTempo();

            uClock.setTempo(currentTempo + 5);

            if (currentTempo > 160)
            {
              uClock.setTempo(160);
            }
          }

          break;

          default:
            // Buttons 16-24 reserved for future voice parameters
            Serial.print("Voice parameter button ");
            Serial.print(evt.buttonIndex);
            Serial.println(" - not yet implemented");
            break;
          }

          // Apply the updated configuration to the voice
          voiceManager->setVoiceConfig(currentVoiceId, *voiceConfig);
        }
      }
      // Handle main settings menu voice selection (buttons 0-3 for voices 1-4)
      else if (evt.buttonIndex < UIEventConstants::SETTINGS_MENU_VOICE_COUNT)
      {
        // Store selected voice and enter preset selection mode
        uiState.settingsMenuIndex = evt.buttonIndex;
        uiState.inPresetSelection = true;
      }
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

  Serial.print("Switched to Voice ");
  Serial.println(uiState.selectedVoiceIndex + 1);
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

static void handlePlayStopButton(const MatrixButtonEvent &evt, UIState &uiState)
{
  if (evt.type == MATRIX_BUTTON_PRESSED)
  {
    uiState.playStopPressTime = millis();
    uiState.playStopWasPressed = true;
  }
  else if (evt.type == MATRIX_BUTTON_RELEASED && uiState.playStopWasPressed)
  {
    unsigned long pressDurationMs = millis() - uiState.playStopPressTime;
    uiState.playStopWasPressed = false;

    if (isLongPress(pressDurationMs) && !isClockRunning)
    {
      // Long press when stopped: enter settings mode
      uiState.settingsMode = true;
      uiState.settingsMenuIndex = UIEventConstants::SETTINGS_MENU_INITIAL_INDEX;
      uiState.settingsSubMenuIndex = UIEventConstants::SETTINGS_SUBMENU_INITIAL_INDEX;
      uiState.inPresetSelection = false;
      Serial.println("Entered settings mode");
    }
    else if (!isLongPress(pressDurationMs))
    {
      // Short press: normal play/stop functionality
      handleControlButton(evt.buttonIndex, uiState);
    }
  }
}

static void handleOtherControlButtons(const MatrixButtonEvent &evt, UIState &uiState)
{
  // Handle remaining control buttons (only on press events)
  if (evt.type == MATRIX_BUTTON_PRESSED)
  {
    handleControlButton(evt.buttonIndex, uiState);
  }
}