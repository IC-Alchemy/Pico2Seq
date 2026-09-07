#include "UIEventHandler.h"
#include "../midi/MidiManager.h"
#include "../sensors/EncoderManager.h"
#include "../pico2seq-core/scales/scales.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../pico2seq-core/sequencer/ShuffleTemplates.h"
#include "../voice/Voice.h"
#include "../voice/VoiceManager.h"
#include "../voice/VoicePresets.h"
#include "../voice/VoiceSystem.h"
#include "ButtonManager.h"
#include "ButtonHandlers.h"
#include "ControlSurfaceLogic.h"
#include "UIConstants.h"
#include "../FeatureConfig.h"
#include <uClock.h>

// =======================
//   UI EVENT CONSTANTS
// =======================

namespace UIEventConstants
{
  // Voice system constants
  static constexpr uint8_t MAX_VOICES = 4; // Number of voices supported by the hardware - used for LED feedback and settings menu navigation (4 voices)
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

  // Filter mode cycling constants (mode list lives in voiceui::kFilterModes)
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
extern void applyVoicePreset(uint8_t voiceIndex, uint8_t presetIndex);

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
#if PICO2SEQ_ENABLE_DELAY_EFFECT
extern float delayTarget;
#endif

// Helper function declarations (static to this file)
static bool handleStepButtonEvent(const MatrixButtonEvent &evt,
                                  UIState &uiState, Sequencer *const *sequencers,
                                  size_t sequencerCount);

// Private helper handlers for matrixEventHandler
static void handleSlideModeStep(const MatrixButtonEvent &evt, UIState &uiState, Sequencer *const *sequencers, size_t sequencerCount);

// Settings sub-mode helpers (settings mode refactor)
static void toggleSettingsSubMode(UIState &uiState);
static void handlePresetSelection(const MatrixButtonEvent &evt, UIState &uiState);
static void handleVoiceParameter(const MatrixButtonEvent &evt, UIState &uiState, VoiceManager *voiceManager);

static void autoSelectEncoderParameter(ParamId paramId, UIState &uiState);

// Shared encoder-control hold/release implementation (used by the tile bridge)
static void encoderControlShortPressAction(UIState &uiState);

// Shared body of a short encoder-control press: in Settings mode while
// stopped it toggles between sub-modes; otherwise it cycles the encoder
// parameter target.
static void encoderControlShortPressAction(UIState &uiState)
{
  // In Settings mode the encoder button toggles between sub-modes — this now
  // also works while the transport runs, so presets can be browsed live.
  // Otherwise, keep the existing encoder parameter cycling behavior.
  if (uiState.settingsMode)
  {
    toggleSettingsSubMode(uiState);
    uiState.selectedStepForEdit = -1;
  }
  else
  {
    // Existing behavior outside of settings: cycle encoder parameter
    handleControlButton(BUTTON_ENCODER_CONTROL, uiState);
  }
}

void beginEncoderControlHold(UIState &uiState)
{
  uiState.encoderControlPressTime = millis();
  uiState.encoderControlWasPressed = true;
}

void endEncoderControlHold(UIState &uiState)
{
  if (!uiState.encoderControlWasPressed)
  {
    return;
  }
  unsigned long pressDurationMs = millis() - uiState.encoderControlPressTime;
  uiState.encoderControlWasPressed = false;

  if (!isLongPress(pressDurationMs))
  {
    encoderControlShortPressAction(uiState);
  }

  // Exit gate sequence length mode on release
  uiState.gateSeqLengthMode = false;
  uiState.selectedStepForEdit = -1;
}

/**
 * @brief Primary matrix event handler that always uses the provided sequencer array.
 *
 * Since the Alchemy tile migration, ALL 32 matrix indices are step pads: the
 * parameter/utility buttons that used to live at indices 16-31 now live on
 * the ButtonModule8/SliderModule tiles and enter through AlchemyControlBridge.
 * Every pad is resolved to (voice, step) through the pad-bank mapping instead
 * of assuming the single selected voice.
 */
void matrixEventHandler(const MatrixButtonEvent &evt, UIState &uiState,
                        Sequencer *const *sequencers, size_t sequencerCount,
                        MidiNoteManager &midiNoteManager)
{

  // Poll held buttons (long press detection) using the supplied array.
  pollUIHeldButtons(uiState, sequencers, sequencerCount);

  // =======================
  //   SLIDE MODE STEP HANDLING
  // =======================

  /**
   * Handle step pads in slide mode - toggle slide per step
   *
   * When in slide mode, step pads toggle the slide parameter for individual
   * steps on their own voice rather than toggling the step on/off.
   */
  if (uiState.slideMode && evt.buttonIndex < NUMBER_OF_STEP_PADS)
  {
    if (evt.type == MATRIX_BUTTON_PRESSED)
    {
      handleSlideModeStep(evt, uiState, sequencers, sequencerCount);
    }
    return; // In slide mode, step pads only toggle slide
  }

  // Step pads: settings navigation, gate-seq-length entry, parameter length
  // programming, step toggling, and Shift+pad clearing.
  handleStepButtonEvent(evt, uiState, sequencers, sequencerCount);
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
 * @brief Handles a parameter button edge keyed by ParamId (Alchemy tile path)
 *
 * The bridge has already folded Shift-latch semantics into
 * parameterButtonHeld[]; this applies the rest of today's behavior:
 * automatic encoder parameter selection for real-time control and parameter
 * editing mode when a step is in edit.
 *
 * @param paramId ParamId as uint8_t of the parameter button
 * @param pressed true on press edge, false on release edge
 * @param uiState Reference to the UI state object
 */
void handleParameterButtonById(uint8_t paramId, bool pressed, UIState &uiState)
{
  // Block parameter button handling when in slide mode to avoid conflicts
  if (uiState.slideMode)
  {
    return;
  }

  if (paramId >= PARAM_ID_COUNT)
  {
    return;
  }
  const ParamId currentParamId = static_cast<ParamId>(paramId);

  // A record-button press also selects that parameter's base control. This
  // applies before step-edit handling so a normal hold and a step-edit press
  // behave identically.
  if (pressed)
  {
    autoSelectEncoderParameter(currentParamId, uiState);
  }

  // Handle parameter editing in step edit mode
  if (pressed && uiState.selectedStepForEdit >= 0)
  {
    if (uiState.currentEditParameter == currentParamId)
    {
      // Toggle off - stop editing this parameter
      uiState.currentEditParameter = ParamId::Count;
    }
    else
    {
      // Toggle on - start editing this parameter
      uiState.currentEditParameter = currentParamId;
      autoSelectEncoderParameter(currentParamId, uiState);
    }
  }
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
  // Ignore out-of-bounds pad indices (all 32 matrix indices are step pads now)
  if (evt.buttonIndex >= NUMBER_OF_STEP_PADS)
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
   * Navigation uses raw pad indices (no bank resolution): pads 0-3 select a
   * voice, pads 8 and up apply presets in the preset sub-mode.
   */
  if (uiState.settingsMode && evt.type == MATRIX_BUTTON_PRESSED)
  {
    // Buttons 0-3 always select voice index (0..3) - used for both voice selection and voice parameter navigation (0-3)
    if (evt.buttonIndex < UIEventConstants::MAX_VOICES)
    {
      uiState.selectedVoiceIndex = evt.buttonIndex;
      uiState.isVoice2Mode = (uiState.selectedVoiceIndex == UIEventConstants::VOICE_2_INDEX); // legacy compat
      uiState.presetPage = uiState.voicePresetIndices[evt.buttonIndex] / VoicePresets::kPresetsPerPage;
      uiState.settingsMenuIndex = evt.buttonIndex;                                            // used by OLED/LED menus
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

  // Resolve the pad through the bank mapping: bank = index/16 picks one of
  // the two voices visible for the current pair, step = index%16.
  const ControlSurface::PadAddress pad =
      ControlSurface::PadBank::resolve(evt.buttonIndex, uiState.selectedVoiceIndex);
  Sequencer *padSequencerPtr = nullptr;
  if (sequencers && pad.voice < sequencerCount)
  {
    padSequencerPtr = sequencers[pad.voice];
  }

  // =======================
  //   SHIFT + PAD: CLEAR STEP
  // =======================

  // Shift + step pad clears that step (gate off, params reset) on the pad's
  // own voice, in any mode.
  if (uiState.shiftHeld && evt.type == MATRIX_BUTTON_PRESSED)
  {
    if (padSequencerPtr)
    {
      clearSequencerStep(*padSequencerPtr, pad.step);
      uiState.selectedStepForEdit = -1;
      uiState.currentEditParameter = ParamId::Count;
    }
    return true;
  }

  // =======================
  //   GATE SEQ LENGTH MODE
  // =======================
  // While holding encoder control (long press), allow setting Gate track length (2-16)
  if (uiState.gateSeqLengthMode && evt.type == MATRIX_BUTTON_PRESSED)
  {
    if (padSequencerPtr)
    {
      uint8_t requested = static_cast<uint8_t>(pad.step + 1); // 1..16
      if (requested < 2)
        requested = 2;
      if (requested > 16)
        requested = 16;
      padSequencerPtr->setParameterStepCount(ParamId::Gate, requested);
      // Optional UI feedback flags
      uiState.resetStepsLightsFlag = true;
      uiState.selectedStepForEdit = -1;
    }
    return true; // consume event in this mode
  }

  // Select the previously "active" (selected voice) sequencer for legacy paths
  Sequencer *currentActiveSequencerPtr = nullptr;
  if (sequencers && uiState.selectedVoiceIndex < sequencerCount)
  {
    currentActiveSequencerPtr = sequencers[uiState.selectedVoiceIndex];
  }

  // Handle parameter length adjustment when holding parameter buttons
  if (isAnyParameterButtonHeld(uiState) && evt.type == MATRIX_BUTTON_PRESSED)
  {
    const ParamId heldParameterId = getHeldParameterParamId(uiState);
    if (heldParameterId != ParamId::Count && padSequencerPtr)
    {
      uint8_t newParameterStepCount = static_cast<uint8_t>(pad.step + 1); // Convert 0-based index to 1-based count
      padSequencerPtr->setParameterStepCount(heldParameterId, newParameterStepCount);
      uiState.selectedStepForEdit = -1;
    }
    return true; // Event was handled as parameter length adjustment
  }

  // Handle normal step pad presses (short/long press detection)
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
        // Long press: Toggle step edit mode for detailed parameter editing.
        // Editing always happens on the selected voice, so entering edit
        // from a pad of the partner voice moves selection to that voice.
        if (currentActiveSequencerPtr == padSequencerPtr && uiState.selectedStepForEdit == pad.step)
        {
          // Exit edit mode for this step
          uiState.selectedStepForEdit = -1;
          uiState.currentEditParameter = ParamId::Count; // Clear edit parameter
        }
        else
        {
          // Enter edit mode for this step on the pad's own voice
          uiState.selectedVoiceIndex = pad.voice;
          uiState.isVoice2Mode = (pad.voice == UIEventConstants::VOICE_2_INDEX); // legacy compat
          uiState.voiceSwitchTriggered = true;                                   // immediate OLED update
          uiState.selectedStepForEdit = pad.step;
        }
      }
      else
      {
        // Short press: Toggle step on/off on the pad's own voice and exit edit mode
        if (padSequencerPtr)
        {
          padSequencerPtr->toggleStep(pad.step);
        }
        uiState.selectedStepForEdit = -1;
        uiState.currentEditParameter = ParamId::Count; // Clear edit parameter
      }
    }
  }
  return true; // Event was handled as step pad
}

// Obsolete static handleControlButtonEvent removed; logic is centralized in handleControlButton (ButtonHandlers.cpp)

static void autoSelectEncoderParameter(ParamId paramId, UIState &uiState)
{
  EncoderParameterMode newEncoderParam;
  if (ControlSurface::encoderBaseModeForRecordParam(paramId, newEncoderParam) &&
      newEncoderParam != uiState.currentEncoderParameter)
  {
    uiState.currentEncoderParameter = newEncoderParam;
    // A turn made for the previous target must not carry over to this one.
    magEncoder.clearPendingTicks();
    // Serial.print("Encoder auto-selected: ");
    // Serial.println(CORE_PARAMETERS[static_cast<int>(paramId)].name);
  }
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
  uiState.inPresetSelection = (uiState.currentSubMode == Sub::PRESET_SELECTION);
  uiState.inVoiceParameterMode = (uiState.currentSubMode == Sub::VOICE_PARAMETER);
}

/**
 * Handle Preset Selection sub-mode.
 * - Buttons 0-3 (handled in caller) select current voice.
 * - Pads 6/7 change page; pads 8..31 apply a preset on that page.
 * - Remain in Preset Selection mode after applying a preset.
 * Safe while the transport runs: applyVoicePreset stages the config and the
 * voice applies it without stopping playback.
 */
static void handlePresetSelection(const MatrixButtonEvent &evt, UIState &uiState)
{
  if (evt.type != MATRIX_BUTTON_PRESSED)
    return;

  const uint8_t count = VoicePresets::getPresetCount();
  if (evt.buttonIndex == VoicePresets::kPreviousPagePad || evt.buttonIndex == VoicePresets::kNextPagePad)
  {
    uiState.presetPage = VoicePresets::changePresetPage(uiState.presetPage,
        evt.buttonIndex == VoicePresets::kNextPagePad ? 1 : -1, count);
    return;
  }
  const int presetIndex = VoicePresets::presetIndexForPad(evt.buttonIndex, count, uiState.presetPage);
  if (presetIndex >= 0)
  {
    // Apply to currently selected voice (0..3 for applyVoicePreset)
    const uint8_t voiceIdx = uiState.selectedVoiceIndex;
    if (voiceIdx < UIEventConstants::MAX_VOICES)
    {
      uiState.voicePresetIndices[voiceIdx] = static_cast<uint8_t>(presetIndex);
      applyVoicePreset(voiceIdx, static_cast<uint8_t>(presetIndex));
    }

    // Stay in Preset Selection mode; do not auto-switch.
    uiState.inPresetSelection = true;     // legacy flag mirror
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
  if (evt.type != MATRIX_BUTTON_PRESSED)
    return;

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
  const VoiceConfig *liveCfg = voiceManager->getVoiceConfig(currentVoiceId);
  if (!liveCfg)
    return;
  // Work on a local copy to avoid mutating live config from UI thread
  VoiceConfig voiceConfig = *liveCfg;

  // UI feedback bookkeeping
  uiState.inVoiceParameterMode = true; // legacy flag mirror of active sub-mode
  uiState.lastVoiceParameterButton = evt.buttonIndex;
  uiState.voiceParameterChangeTime = millis();

  const uint8_t displayVoiceNumber = selectedVoiceIndex; // 0-based

  switch (evt.buttonIndex)
  {
  case 8: // Toggle envelope on/off
    voiceConfig.hasEnvelope = !voiceConfig.hasEnvelope;
    Serial.print("Voice ");
    Serial.print(displayVoiceNumber);
    Serial.print(" envelope ");
    Serial.println(voiceConfig.hasEnvelope ? "ON" : "OFF");
    break;

  case 9: // Toggle overdrive
    voiceConfig.hasOverdrive = !voiceConfig.hasOverdrive;
    Serial.print("Voice ");
    Serial.print(displayVoiceNumber);
    Serial.print(" overdrive ");
    Serial.println(voiceConfig.hasOverdrive ? "ON" : "OFF");
    break;

  // case 10 (wavefolder toggle) removed with the wavefolder effect

  case 11: // Cycle filter mode
  {
    // Cycle through the shared filter-mode table (names and modes stay in sync)
    int currentIndex = 0;
    for (int i = 0; i < voiceui::kFilterModeCount; ++i)
    {
      if (voiceConfig.filterMode == voiceui::kFilterModes[i])
      {
        currentIndex = i;
        break;
      }
    }
    const int nextIndex = (currentIndex + 1) % voiceui::kFilterModeCount;
    voiceConfig.filterMode = voiceui::kFilterModes[nextIndex];

    Serial.print("Voice ");
    Serial.print(displayVoiceNumber);
    Serial.print(" filter mode: ");
    Serial.println(voiceui::kFilterModeNames[nextIndex]);
  }
  break;

  case 12: // Step filter resonance
  {
    float currentResonance = voiceConfig.filterRes;
    currentResonance += UIEventConstants::FILTER_RESONANCE_STEP;
    if (currentResonance > UIEventConstants::FILTER_RESONANCE_MAX)
    {
      currentResonance = UIEventConstants::FILTER_RESONANCE_MIN;
    }
    voiceConfig.filterRes = currentResonance;
    // Serial.print("Voice "); Serial.print(displayVoiceNumber);
    // Serial.print(" filter resonance: "); Serial.println(currentResonance, 2);
  }
  break;

  case 13: // Set delay time to dotted quarter
  {
#if PICO2SEQ_ENABLE_DELAY_EFFECT
    float currentTempo = uClock.getTempo();
    if (currentTempo < 1.0f)
      currentTempo = 1.0f;
    const float dottedQuarterMs = 90000.0f / currentTempo; // 1.5 * (60000/BPM)
    delayTarget = dottedQuarterMs * 48.0f;                 // 48kHz -> 48 samples/ms
    // Serial.print("Delay time set to dotted quarter: "); Serial.println(dottedQuarterMs, 2);
#endif
  }
  break;

  case 14: // Tempo -5, floored at 45
  {
    float currentTempo = uClock.getTempo();
    uClock.setTempo(currentTempo - 5);
    if (currentTempo < 45)
    {
      uClock.setTempo(45);
    }
  }
  break;

  case 15: // Tempo +5, capped at 200
  {
    float currentTempo = uClock.getTempo();
    uClock.setTempo(currentTempo + 5);
    if (currentTempo > 200)
    {
      uClock.setTempo(200);
    }
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
  voiceManager->setVoiceConfig(currentVoiceId, voiceConfig);
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

  // Detect long hold of encoder control to enter Gate Sequence Length mode
  // Suppress this feature while in settings menus (stopped state)
  if (uiState.encoderControlWasPressed && !uiState.gateSeqLengthMode && !uiState.settingsMode)
  {
    unsigned long pressDurationMs = currentTimeMs - uiState.encoderControlPressTime;
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
  // Safety: if the encoder control is no longer held, ensure we exit the mode
  else if (!uiState.encoderControlWasPressed && uiState.gateSeqLengthMode)
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

void handleSlideModePress(UIState &uiState)
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

void selectVoice(UIState &uiState, MidiNoteManager &midiNoteManager, uint8_t voiceIndex)
{
  if (voiceIndex >= UIEventConstants::MAX_VOICES)
  {
    return;
  }

  midiNoteManager.onModeSwitch();

  uiState.selectedVoiceIndex = voiceIndex;
  uiState.isVoice2Mode = (voiceIndex == UIEventConstants::VOICE_2_INDEX); // Legacy compatibility
  uiState.selectedStepForEdit = -1;                                       // Clear step editing when switching voices
  uiState.voiceSwitchTriggered = true;                                    // Set flag for immediate OLED update
}

static void handleSlideModeStep(const MatrixButtonEvent &evt, UIState &uiState, Sequencer *const *sequencers, size_t sequencerCount)
{
  // Resolve the pad to its own voice through the bank mapping
  const ControlSurface::PadAddress pad =
      ControlSurface::PadBank::resolve(evt.buttonIndex, uiState.selectedVoiceIndex);
  Sequencer *activeSequencerPtr = nullptr;
  if (sequencers && pad.voice < sequencerCount)
  {
    activeSequencerPtr = sequencers[pad.voice];
  }

  if (activeSequencerPtr)
  {
    Sequencer &currentActiveSequencer = *activeSequencerPtr;

    // Get current slide value and toggle it
    uint8_t currentSlideValue = currentActiveSequencer.getStepParameterValue(
        ParamId::Slide, pad.step);
    uint8_t newSlideValue = (currentSlideValue > UIEventConstants::SLIDE_OFF_VALUE) ? UIEventConstants::SLIDE_OFF_VALUE : UIEventConstants::SLIDE_ON_VALUE;

    // Apply the new slide value to the step
    currentActiveSequencer.setStepParameterValue(ParamId::Slide, pad.step, newSlideValue);
  }
  else
  {
    // No sequencer available for the pad's voice; ignore.
  }
}

void clearSequencerStep(Sequencer &sequencer, uint8_t stepIdx)
{
  if (stepIdx >= NUMBER_OF_STEP_BUTTONS)
  {
    return;
  }

  // Gate off first: the step falls silent even if the sequencer is running.
  sequencer.setStepParameterValue(ParamId::Gate, stepIdx, 0.0f);

  // Reset the remaining automatable parameters to their track defaults.
  // CORE_PARAMETERS[].defaultValue is a variant (float/bool); fold it to the
  // float the sequencer tracks store.
  auto variantToFloat = [](const ParameterValueType &value) -> float
  {
    if (std::holds_alternative<float>(value))
      return std::get<float>(value);
    if (std::holds_alternative<bool>(value))
      return std::get<bool>(value) ? 1.0f : 0.0f;
    return static_cast<float>(std::get<int>(value));
  };

  for (uint8_t paramIndex = 0; paramIndex < PARAM_ID_COUNT; ++paramIndex)
  {
    const ParamId paramId = static_cast<ParamId>(paramIndex);
    if (paramId == ParamId::Gate)
    {
      continue;
    }
    sequencer.setStepParameterValue(paramId, stepIdx,
                                    variantToFloat(CORE_PARAMETERS[paramIndex].defaultValue));
  }
}

void advanceSequencerStep(Sequencer &seq, uint32_t current_uclock_step, int mm_distance,
                          const UIState &uiState, VoiceState *voiceState)
{
  seq.advanceStep(current_uclock_step, mm_distance,
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Note)],
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Velocity)],
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Filter)],
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Attack)],
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Decay)],
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Octave)],
                  uiState.selectedStepForEdit,
                  voiceState);
}
