#include "UIEventHandler.h"
#include "UITransitions.h"
#include "../app/AppState.h"
#include "../app/ClockService.h"
#include "../app/VoiceSetup.h"
#include "../app/VoiceEditor.h"
#include "../sensors/EncoderManager.h"
#include "../sitar/SitarPerformance.h"
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
#include "SettingsPads.h"
#include "UIConstants.h"
#include <uClock.h>
#include <cstdio>

// UIEventHandler.cpp — one funnel for all 32 step pads + tile entry points.
// Core 0 only, non-blocking (millis() timing, no waits). Tap toggles a step,
// hold opens it for edit, Shift+pad clears it; modes below arbitrate the rest.
// =======================
//   UI EVENT CONSTANTS
// =======================

namespace UIEventConstants
{
  // Four voices, 0-based everywhere internally (UI shows 1..4).
  static constexpr uint8_t MAX_VOICES = 4;
  static constexpr uint8_t VOICE_1_INDEX = 0;
  static constexpr uint8_t VOICE_2_INDEX = 1;
  static constexpr uint8_t VOICE_3_INDEX = 2;
  static constexpr uint8_t VOICE_4_INDEX = 3;

  // Boot presets for voices 1-2 (indices into VoicePresets).
  static constexpr uint8_t DEFAULT_VOICE_1_PRESET = 0; // Analog preset
  static constexpr uint8_t DEFAULT_VOICE_2_PRESET = 1; // Digital preset

  // Settings browser voices shown at once + initial cursor positions.
  static constexpr uint8_t SETTINGS_MENU_VOICE_COUNT = 4;
  static constexpr uint8_t SETTINGS_MENU_INITIAL_INDEX = 0;
  static constexpr uint8_t SETTINGS_SUBMENU_INITIAL_INDEX = 0;

  // Slide gate values: legato off/on per step.
  static constexpr uint8_t SLIDE_OFF_VALUE = 0;
  static constexpr uint8_t SLIDE_ON_VALUE = 1;


}

static_assert(UIState::NUM_RANDOMIZE >= UIEventConstants::MAX_VOICES,
              "UI expects 4 randomize buttons; update UIState::NUM_RANDOMIZE or adjust handlers.");

// LED theme lives in the LED layer; UI only tracks the index.
extern void setLEDTheme(LEDTheme theme);

// Step-parameter metadata (names/defaults) owned by the sequencer core.
extern const ParameterDefinition CORE_PARAMETERS[];

// File-local helpers: step routing, slide steps, settings pages, encoder follow.
static bool handleStepButtonEvent(const MatrixButtonEvent &evt,
                                  UIState &uiState, const SequencerView &sequencers);

// Slide-mode and settings-page helpers (one owner each, called from the funnel).
static void handleSlideModeStep(const MatrixButtonEvent &evt, UIState &uiState, const SequencerView &sequencers);

// Settings sub-mode helpers.
static void handlePresetSelection(const MatrixButtonEvent &evt, UIState &uiState);
static void handleVoiceParameter(const MatrixButtonEvent &evt, UIState &uiState, VoiceManager *voiceManager);

static void autoSelectEncoderParameter(ParamId paramId, UIState &uiState);

// Shared encoder-control hold/release implementation (used by the tile bridge)
static void encoderControlShortPressAction(UIState &uiState);

// Why: the encoder-control button is overloaded (short-press = switch target,
// long-hold = gate-length entry) because the panel has no spare buttons, so one
// shared body keeps the matrix path and the Alchemy tile bridge from drifting
// into two different behaviors.
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
    UITransitions::toggleSettingsPage(uiState);
  }
  else
  {
    // Existing behavior outside of settings: cycle encoder parameter
    handleControlButton(BUTTON_ENCODER_CONTROL, uiState);
  }
}

// Why: hold duration is tracked in UIState with millis() instead of blocking,
// because Core 0 must keep scanning controls/displays every millisecond and
// must never wait inside a button handler.
void beginEncoderControlHold(UIState &uiState)
{
  uiState.encoderControlPressTime = millis();
  uiState.encoderControlWasPressed = true;
}

// Why: short vs. long is decided on release (not on press) so a hold can still
// promote into gate-length mode while held, and releasing always exits that
// modal entry state even if the promotion never fired.
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
 *
 * Why: since the Alchemy migration this is the single funnel for all 32 pads,
 * so it guards voice-editor/modal states first and routes slide vs. normal
 * early — that keeps per-step behavior consistent no matter which surface
 * produced the event.
 */
void matrixEventHandler(const MatrixButtonEvent &evt, UIState &uiState,
                        const SequencerView &sequencers)
{

  if(uiState.voiceEditor.active || uiState.controlsWaitRelease) return;
  // Sitar Explorer owns all 32 pads while it is on: every touch is a fret, a
  // stroke or an exploration pad, never a step toggle. The sequencer keeps
  // playing — the mode layers over the song rather than stopping it.
  if(uiState.sitar.active) {
    Sitar::Performance::onPadEvent(evt, uiState, millis());
    return;
  }
  // Edge-only input: holds are promoted by polling so the loop never blocks.
  pollUIHeldButtons(uiState, sequencers);

  // =======================
  //   SLIDE MODE STEP HANDLING
  // =======================

  // Slide mode owns the pads: each tap flips legato for that step's voice.
  // (No gate toggles here — one gesture, one job.)
  if (!uiState.settingsMode && uiState.slideMode && evt.buttonIndex < NUMBER_OF_STEP_PADS)
  {
    if (evt.type == MATRIX_BUTTON_PRESSED)
    {
      handleSlideModeStep(evt, uiState, sequencers);
    }
    return; // Slide consumed the pad; normal step handling stays out.
  }

  // Step pads: settings navigation, gate-seq-length entry, parameter length
  // programming, step toggling, and Shift+pad clearing.
  handleStepButtonEvent(evt, uiState, sequencers);
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
 *
 * Why: tiles are keyed by ParamId rather than matrix index so the same logic
 * serves both surfaces; auto-selecting the encoder target on press keeps the
 * knob always acting on the parameter the finger just touched (no extra select step).
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
 * @param sequencers Fixed voice-order view of the voice sequencers
 * @return true if the event was handled as a step button event, false otherwise
 *
 * Why: one pad does five jobs (toggle/edit/clear/length/settings) depending on
 * modifiers, so priority order matters — settings and Shift-clear must win over
 * normal toggling or a stuck modifier would corrupt pattern data.
 */
static bool handleStepButtonEvent(const MatrixButtonEvent &evt,
                                  UIState &uiState, const SequencerView &sequencers)
{
  // Pads outside the 32-step grid have no voice; ignore them.
  if (evt.buttonIndex >= NUMBER_OF_STEP_PADS)
  {
    return false;
  }

  // =======================
  //   SETTINGS MODE HANDLING
  // =======================

  // Settings owns the pads: raw indices browse presets/timbre (no bank
  // mapping), voice buttons alone switch voices, releases are swallowed.
  if (uiState.settingsMode)
  {
    // Settings owns every pad edge while open. A release that fell through
    // would reach the step handling below, select a step and move the voice
    // selection to the pad's bank voice, so the next preset tap would land
    // on a different voice than the one on screen.
    if (evt.type != MATRIX_BUTTON_PRESSED)
    {
      uiState.padPressTimestamps[evt.buttonIndex] = 0;
      return true;
    }

    // Route handling based on active sub-mode
    if (uiState.isPresetSelection())
    {
      handlePresetSelection(evt, uiState);
    }
    else // VOICE_PARAMETER
    {
      handleVoiceParameter(evt, uiState, voiceManager.get());
    }
    return true; // Handled inside settings; step toggling stays out.
  }

  // Resolve the pad through the bank mapping: bank = index/16 picks one of
  // the two voices visible for the current pair, step = index%16.
  const ControlSurface::PadAddress pad =
      ControlSurface::PadBank::resolve(evt.buttonIndex, uiState.selectedVoiceIndex);
  Sequencer *padSequencerPtr = sequencers.get(pad.voice);

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
  Sequencer *currentActiveSequencerPtr = sequencers.get(uiState.selectedVoiceIndex);

  // Handle parameter length adjustment when holding parameter buttons
  if (isAnyParameterButtonHeld(uiState) && evt.type == MATRIX_BUTTON_PRESSED)
  {
    const ParamId heldParameterId = getHeldParameterParamId(uiState);
    if (heldParameterId != ParamId::Count && padSequencerPtr)
    {
      uint8_t newParameterStepCount = static_cast<uint8_t>(pad.step + 1); // Convert 0-based index to 1-based count
      if (newParameterStepCount < SequencerConstants::MIN_STEPS_COUNT)
      {
        newParameterStepCount = SequencerConstants::MIN_STEPS_COUNT;
      }
      padSequencerPtr->setParameterStepCount(heldParameterId, newParameterStepCount);
      uiState.selectedStepForEdit = -1;
    }
    return true; // Event was handled as parameter length adjustment
  }

  // Handle normal step pad presses (short/long press detection)
  if (evt.type == MATRIX_BUTTON_PRESSED)
  {
    // Record press timestamp for long press detection. Presses consumed by a
    // branch above return first and stay untimed, so their release is ignored.
    uiState.padPressTimestamps[evt.buttonIndex] = millis();
    return true;
  }

  const ControlSurface::PadRelease release = ControlSurface::classifyPadRelease(
      uiState.padPressTimestamps[evt.buttonIndex], millis(),
      UITimingConstants::LONG_PRESS_THRESHOLD_MS);
  uiState.padPressTimestamps[evt.buttonIndex] = 0; // Clear timestamp
  if (isAnyParameterButtonHeld(uiState))
  {
    return true;
  }

  if (release == ControlSurface::PadRelease::Hold)
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
      UITransitions::focusPad(uiState, pad.voice, pad.step);
    }
  }
  else if (release == ControlSurface::PadRelease::Tap)
  {
    // Short press: Toggle step on/off on the pad's own voice and exit edit mode
    if (padSequencerPtr)
    {
      padSequencerPtr->toggleStep(pad.step);
    }
    uiState.selectedStepForEdit = -1;
    uiState.currentEditParameter = ParamId::Count; // Clear edit parameter
  }
  return true; // Event was handled as step pad
}


// Why: without this the encoder would keep writing to the previously selected
// parameter after the user grabs a new tile — auto-select keeps ear-to-hand
// mapping immediate for live tweaking, and clearing the encoder accumulator
// avoids a value jump on the new target.
static void autoSelectEncoderParameter(ParamId paramId, UIState &uiState)
{
  EncoderParameterMode newEncoderParam;
  if (ControlSurface::encoderBaseModeForRecordParam(paramId, newEncoderParam) &&
      newEncoderParam != uiState.currentEncoderParameter)
  {
    uiState.currentEncoderParameter = newEncoderParam;
    // A turn made for the previous target must not carry over to this one.
    VoiceEditor::clearEncoder();
  }
}

// =======================
// Settings sub-mode helpers
// =======================

// Why: Settings always opens on presets (never resumes the last sub-mode) so the
// LED grid and the handler agree — otherwise preset taps would silently hit the
// voice-parameter toggles while the display still shows presets.
void openSettingsMode(UIState &uiState)
{
  UITransitions::openSettings(uiState);
}

// Why: closing resets every Settings-related flag (not just settingsMode) so no
// modal residue leaks into step editing — a leftover selectedStepForEdit would
// reroute the next pad tap into parameter editing.
void closeSettingsMode(UIState &uiState)
{
  UITransitions::closeSettings(uiState);
}

/**
 * Handle Preset Selection sub-mode.
 * - Pads 0..30 apply that preset to the selected voice (voice buttons pick it).
 * - Remain in Preset Selection mode after applying a preset.
 * Safe while the transport runs: applyVoicePreset stages the config and the
 * voice applies it without stopping playback.
 * Why: staying in the browser after applying (rather than auto-closing) enables
 * fast A/B auditioning of presets while the transport runs — staging makes that
 * safe for the Core 1 audio path.
 */
static void handlePresetSelection(const MatrixButtonEvent &evt, UIState &uiState)
{
  if (evt.type != MATRIX_BUTTON_PRESSED)
    return;

  const int presetIndex = VoicePresets::presetIndexForPad(evt.buttonIndex, VoicePresets::getPresetCount());
  if (presetIndex >= 0)
  {
    // Apply to currently selected voice (0..3 for applyVoicePreset)
    const uint8_t voiceIdx = uiState.selectedVoiceIndex;
    if (voiceIdx < UIEventConstants::MAX_VOICES)
    {
      uiState.voicePresetIndices[voiceIdx] = static_cast<uint8_t>(presetIndex);
      applyVoicePreset(voiceIdx, static_cast<uint8_t>(presetIndex));
    }

    // Applying a preset does not change the settings page.
  }
}

// The same pad catalogue drives edits, persistent LEDs and OLED labels.
static void handleVoiceParameter(const MatrixButtonEvent &evt, UIState &uiState, VoiceManager *voiceManager)
{
  if (evt.type != MATRIX_BUTTON_PRESSED || !voiceManager ||
      uiState.selectedVoiceIndex >= VoiceSystem::MAX_VOICES)
    return;

  const uint8_t voiceIndex = uiState.selectedVoiceIndex;
  const auto *config = voiceManager->getVoiceConfig(voiceSystem.getVoiceId(voiceIndex));
  if (!config)
    return;
  VoiceConfig next = *config;
  if (!SettingsPads::apply(evt.buttonIndex, next, uiState.shiftHeld))
    return;

  // Use the normal control-core publisher, including its glide-time handoff.
  VoiceEditor::publish(voiceIndex, next);
  // Shared transition flag drives the LED/OLED feedback timeout; the name
  // snapshot below lets the notice survive a voice switch.
  UITransitions::showVoiceParameterFeedback(uiState, evt.buttonIndex, millis());
  const auto parameter = SettingsPads::parameter(evt.buttonIndex);
  uiState.voiceParameterNoticeVoice = voiceIndex;
  snprintf(uiState.voiceParameterNoticeName, sizeof(uiState.voiceParameterNoticeName),
           "%s", VoiceEdit::name(parameter, next));
  VoiceEdit::format(parameter, next, uiState.voiceParameterNoticeValue,
                    sizeof(uiState.voiceParameterNoticeValue));
}
/**
 * @brief Poll for long press detection on randomize buttons
 *
 * Continuously checks if any randomize buttons have been held long enough
 * to trigger a sequence reset. This function must be called regularly
 * to ensure responsive long press detection during button holds.
 *
 * @param uiState Reference to UI state containing button press timing data
 * @param sequencers Fixed voice-order view of the voice sequencers
 * Why: long-press actions are polled (not interrupt-driven) because the matrix
 * only delivers edges — polling keeps reset/gate-length entry responsive without
 * blocking the Core 0 scan loop, and suppressing them in Settings avoids
 * mistaking preset browsing for a destructive reset.
 */
void pollUIHeldButtons(UIState &uiState, const SequencerView &sequencers)
{
  if(uiState.voiceEditor.active || uiState.controlsWaitRelease) return;
  unsigned long currentTimeMs = millis();

  // Check for long press resets on every voice in the routing table.
  for (size_t voiceIndex = 0; voiceIndex < sequencers.size(); ++voiceIndex)
  {
    if (uiState.randomizeWasPressed[voiceIndex] &&
        !uiState.randomizeResetTriggered[voiceIndex])
    {
      unsigned long pressDurationMs = currentTimeMs - uiState.randomizePressTime[voiceIndex];
      if (isLongPress(pressDurationMs))
      {
        // The routing table owns one sequencer per voice; get() rejects
        // out-of-range indices instead of resetting the wrong voice.
        Sequencer *targetSequencer = sequencers.get(voiceIndex);

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

// Slide mode is mutually exclusive with parameter-hold and gate-length
// modes: entering it clears the others so a stuck modifier cannot make step
// pads both toggle slides and rewrite track lengths at once.
void handleSlideModePress(UIState &uiState)
{
  UITransitions::toggleSlide(uiState);
}

void selectVoice(UIState &uiState, uint8_t voiceIndex)
{
  // Invalid indices are rejected by the shared transition (no state change).
  UITransitions::selectPerformanceVoice(uiState, voiceIndex);
}

// Why: slide is edited per-step on the pad's own (bank-resolved) voice rather
// than the selected voice, so polymetric partner voices can have independent
// legato without forcing a voice switch first.
static void handleSlideModeStep(const MatrixButtonEvent &evt, UIState &uiState, const SequencerView &sequencers)
{
  // Resolve the pad to its own voice through the bank mapping
  const ControlSurface::PadAddress pad =
      ControlSurface::PadBank::resolve(evt.buttonIndex, uiState.selectedVoiceIndex);
  Sequencer *activeSequencerPtr = sequencers.get(pad.voice);

  if (activeSequencerPtr)
  {
    Sequencer &currentActiveSequencer = *activeSequencerPtr;

    // Flip legato for this step; the performer hears the glide on next pass.
    uint8_t currentSlideValue = currentActiveSequencer.getStepParameterValue(
        ParamId::Slide, pad.step);
    uint8_t newSlideValue = (currentSlideValue > UIEventConstants::SLIDE_OFF_VALUE) ? UIEventConstants::SLIDE_OFF_VALUE : UIEventConstants::SLIDE_ON_VALUE;

    // Write the flipped legato back to the step.
    currentActiveSequencer.setStepParameterValue(ParamId::Slide, pad.step, newSlideValue);
  }
  else
  {
    // Null voice slot (should not happen via PadBank); nothing to flip.
  }
}

// Why: Shift+pad must silence the step (gate off) AND restore track defaults —
// clearing only the gate would leave stale pitch/filter values that reappear
// when the step is re-enabled; playback-transform voices delegate to the
// modifier reset so the underlying pattern is preserved.
void clearSequencerStep(Sequencer &sequencer, uint8_t stepIdx)
{
  if(sequencer.usesPlaybackTransform()) {sequencer.resetModifierStep(stepIdx);return;}
  if (stepIdx >= SequencerConstants::MAX_STEPS_COUNT)
  {
    return;
  }

  // Reset the remaining automatable parameters to their track defaults.
  // CORE_PARAMETERS[].defaultValue is a variant (float/bool); fold it to the
  // float the sequencer tracks store.
  for (uint8_t paramIndex = 0; paramIndex < PARAM_ID_COUNT; ++paramIndex)
  {
    const ParamId paramId = static_cast<ParamId>(paramIndex);
    if (paramId == ParamId::Gate)
    {
      continue;
    }
    sequencer.setStepParameterValue(paramId, stepIdx,
                                    parameterValueAsFloat(CORE_PARAMETERS[paramIndex].defaultValue));
  }

  // Gate off: the step falls silent even if the sequencer is running.
  sequencer.setStepParameterValue(ParamId::Gate, stepIdx, 0.0f);
}

// Why: a per-voice wipe also posts an OLED notice and clears edit/selection
// state, because after destructive input the display and LEDs must agree that
// the voice is empty — presets/transport/tempo are deliberately untouched.
void clearSequencerVoice(UIState &uiState, Sequencer &sequencer, uint8_t voiceIndex)
{
  sequencer.clearPattern();
  uiState.oledNoticeKind = UIState::OledNoticeKind::VoiceCleared;
  uiState.oledNoticeVoice = voiceIndex;
  uiState.oledNoticeUntil = millis() + OLED_NOTICE_DURATION_MS;
  uiState.resetStepsLightsFlag = true;
  uiState.selectedStepForEdit = -1;
  uiState.currentEditParameter = ParamId::Count;
}

// Why: the all-voices clear loops the routing view (skipping nulls) instead of
// assuming 4 voices, so future voice counts need no new chord handler; like the
// single-voice wipe it resets UI light/edit flags so the grid redraws clean.
void clearAllSequencerVoices(UIState &uiState, const SequencerView &sequencers)
{
  for (size_t voice = 0; voice < sequencers.size(); ++voice)
  {
    if (Sequencer *seq = sequencers.get(voice))
    {
      seq->clearPattern();
    }
  }
  uiState.oledNoticeKind = UIState::OledNoticeKind::AllCleared;
  uiState.oledNoticeUntil = millis() + OLED_NOTICE_DURATION_MS;
  uiState.resetStepsLightsFlag = true;
  uiState.selectedStepForEdit = -1;
  uiState.currentEditParameter = ParamId::Count;
}

// Why: pico2seq-core stays UI-agnostic (no UIState include) for reuse in other
// projects, so this thin adapter unpacks the held-parameter/edit-step fields
// here — keeping the StepPlayback call site to one line and the core portable.
void advanceSequencerStep(Sequencer &seq, uint32_t current_uclock_step, int mm_distance,
                          const UIState &uiState, VoiceState *voiceState)
{
  seq.advanceStep(current_uclock_step, mm_distance,
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Note)],
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Velocity)],
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Filter)],
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Attack)],
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Release)],
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Octave)],
                  uiState.selectedStepForEdit,
                  voiceState);
}
