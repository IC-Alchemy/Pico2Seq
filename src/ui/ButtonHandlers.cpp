#include "ButtonHandlers.h"
#include "../app/AppState.h"
#include "../app/ClockService.h"
#include "../app/VoiceEditor.h"

#include "../LEDMatrix/LEDMatrixFeedback.h"
#include "../pico2seq-core/scales/scales.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../pico2seq-core/sequencer/ShuffleTemplates.h"
#include "../voice/VoiceManager.h"
#include "../voice/VoiceSystem.h"
#include "ButtonManager.h"
#include "UIConstants.h"
#include "UIEventHandler.h"
#include "UIState.h"
#include "UITransitions.h"
#include "../sensors/EncoderManager.h"

#include <uClock.h>

// ButtonHandlers.cpp — what each button gesture does to the music.
// All timing is millis()-based and non-blocking (Core 0 must keep scanning).
// Transport/scale/theme act immediately; destructive wipes are hold-gated.

// Stamp the press start so release can split tap (audition) vs hold (commit).
void beginRandomizePress(int voiceIndex, UIState &state)
{
  if (voiceIndex < 0 || voiceIndex >= UIState::NUM_RANDOMIZE)
    return;
  state.randomizePressTime[voiceIndex] = millis();
  state.randomizeWasPressed[voiceIndex] = true;
}

// Clear the tap/hold latch so one press can never fire twice.
void endRandomizePress(int voiceIndex, UIState &state)
{
  if (voiceIndex < 0 || voiceIndex >= UIState::NUM_RANDOMIZE)
    return;
  state.randomizeWasPressed[voiceIndex] = false;
  state.randomizeResetTriggered[voiceIndex] = false;
}

// Tap: performer gets a fresh variation now. Hold (via pollUIHeldButtons):
// the voice goes silent-empty. Short-press path only; the hold path lives in
// the poller because the matrix delivers edges, not holds.
void handleRandomizeButton(int voiceIndex, UIState &state)
{
  if (voiceIndex < 0 || voiceIndex >= UIState::NUM_RANDOMIZE)
    return;

  Sequencer *seq = AppState::sequencerView.get(voiceIndex);
  if (!seq)
    return;

  // Tap vs hold split, decided on release so a hold can still promote mid-press.
  unsigned long heldTime = millis() - state.randomizePressTime[voiceIndex];
  if (!isLongPress(heldTime))
  {
    // Short press: shuffle this voice's steps for instant variation.
    seq->randomizeParameters();

    // Brief OLED banner so the performer sees which voice just shuffled.
    state.oledNoticeKind = UIState::OledNoticeKind::Randomized;
    state.oledNoticeVoice = static_cast<uint8_t>(voiceIndex);
    state.oledNoticeUntil = millis() + OLED_NOTICE_DURATION_MS;
  }

  // Always unlatch and leave step-edit: a shuffled voice shows the grid, not a stale editor.
  endRandomizePress(voiceIndex, state);
  state.selectedStepForEdit = -1;
}

// Step the encoder to its next target (velocity -> filter -> ...); the next
// turn then edits the newly shown lane. Clears the accumulator so no jump carries over.
static void cycleEncoderParameter(UIState &uiState)
{
  VoiceEditor::clearEncoder();
  uiState.currentEncoderParameter = static_cast<EncoderParameterMode>(
      (static_cast<uint8_t>(uiState.currentEncoderParameter) + 1) %
      static_cast<uint8_t>(EncoderParameterMode::COUNT));

  uiState.lastEncoderButtonPressTime = millis();
}

// Toggle a timbre switch on the selected voice (envelope/drive/filter).
// paramIndex is the raw settings-pad index driving this toggle.
// Works on a copy, then publishes: Core 1 audio never sees a half-written config.
void handleVoiceParameterButton(int voiceIndex, int paramIndex, UIState &state)
{
  if (!voiceManager)
    return;
  if (voiceIndex < 0 || voiceIndex > 3)
    return;

  uint8_t currentVoiceId = voiceSystem.getVoiceId(voiceIndex);

  const VoiceConfig *liveCfg = voiceManager->getVoiceConfig(currentVoiceId);
  if (!liveCfg)
    return;
  // Work on a local copy: the UI thread must never mutate the live voice config.
  VoiceConfig config = *liveCfg;

  // Set UI state for voice parameter mode feedback
  UITransitions::showVoiceParameterFeedback(state, static_cast<uint8_t>(paramIndex), millis());

  uint8_t displayVoiceNumber = static_cast<uint8_t>(voiceIndex); // 0-based

  switch (paramIndex)
  {
  case 8: // Toggle hasEnvelope per voice
    config.hasEnvelope = !config.hasEnvelope;
    Serial.print("Voice ");
    Serial.print(displayVoiceNumber);
    Serial.print(" envelope ");
    Serial.println(config.hasEnvelope ? "ON" : "OFF");
    break;
  case 9: // Toggle hasOverdrive
    config.hasOverdrive = !config.hasOverdrive;
    Serial.print("Voice ");
    Serial.print(displayVoiceNumber);
    Serial.print(" overdrive ");
    Serial.println(config.hasOverdrive ? "ON" : "OFF");
    break;
  // Wavefolder toggle (pad 10) went away with the effect; pads 15-24 stay spare.
  case 11:
  { // Cycle through the shared filter-mode table (names and modes stay in sync)
    if (!config.hasFilter)
    {
      Serial.print("Voice ");
      Serial.print(displayVoiceNumber);
      Serial.println(" has no filter (waveguide preset); filter mode ignored");
      break;
    }
    int currentIndex = 0;
    for (int i = 0; i < voiceui::kFilterModeCount; ++i)
    {
      if (config.filterMode == voiceui::kFilterModes[i])
      {
        currentIndex = i;
        break;
      }
    }
    const int nextIndex = (currentIndex + 1) % voiceui::kFilterModeCount;
    config.filterMode = voiceui::kFilterModes[nextIndex];

    Serial.print("Voice ");
    Serial.print(displayVoiceNumber);
    Serial.print(" filter mode: ");
    Serial.println(voiceui::kFilterModeNames[nextIndex]);
  }
  break;
  case 12:
  { // Cycle through filter resonance amounts
    if (!config.hasFilter)
    {
      Serial.print("Voice ");
      Serial.print(displayVoiceNumber);
      Serial.println(" has no filter (waveguide preset); resonance ignored");
      break;
    }
    float currentResonance = config.filterRes;
    currentResonance += 0.1f;
    if (currentResonance > 1.0f)
      currentResonance = 0.0f;
    config.filterRes = currentResonance;

    Serial.print("Voice ");
    Serial.print(displayVoiceNumber);
    Serial.print(" filter resonance: ");
    Serial.println(currentResonance, 2);
  }
  break;

  default:
    // Spare pad: no timbre mapped yet, so nothing changes audibly.
    Serial.print("Voice parameter button ");
    Serial.print(paramIndex);
    Serial.println(" pressed (no action defined yet)");
    break;
  }

  // Publish the switched timbre to the audio core via the staged handoff.
  voiceManager->setVoiceConfig(currentVoiceId, config);
}

// One entry point for transport/mode tiles: play/stop, scale, theme, swing,
// slide, encoder target. Each branch is immediate except play/stop, which also
// opens/closes the preset browser so stopping invites sound selection.
void handleControlButton(int buttonId, UIState &state)
{
  switch (buttonId)
  {
  case BUTTON_SLIDE_MODE:
    handleSlideModePress(state);
    Serial.print("Slide mode ");
    Serial.println(state.slideMode ? "ON" : "OFF");
    break;

  case BUTTON_ENCODER_CONTROL:
    cycleEncoderParameter(state);
    break;

  case BUTTON_PLAY_STOP:
    if (isClockRunning)
    {
      uClock.stop();
      // Arp players need the chord and rhythm ready while stopped. Presets
      // remain available on Play-hold; the sequencer keeps stop-to-settings.
      if (!state.arp.active()) openSettingsMode(state);
    }
    else
    {
      uClock.start();
      // Starting again leaves the browser if it was open: back to the grid.
      if (state.settingsMode)
      {
        closeSettingsMode(state);
      }
    }
    break;

  case BUTTON_CHANGE_SCALE:
    currentScale = (currentScale + 1) % 13;
    state.arp.setScaleNotesPerOctave(
        scaleNotesPerOctave(scale[currentScale]));
    Serial.print("Scale changed to: ");
    Serial.print(currentScale);
    Serial.print(" (");
    Serial.print(scaleNames[currentScale]);
    Serial.println(")");
    break;

  case BUTTON_CHANGE_THEME:
    state.currentThemeIndex = (state.currentThemeIndex + 1) % static_cast<int>(LEDTheme::COUNT);
    setLEDTheme(static_cast<LEDTheme>(state.currentThemeIndex));
    break;

  case BUTTON_CHANGE_SWING_PATTERN:
  {
    state.currentShufflePatternIndex = (state.currentShufflePatternIndex + 1) % NUM_SHUFFLE_TEMPLATES;
    const ShuffleTemplate &currentTemplate = shuffleTemplates[state.currentShufflePatternIndex];
    // Apply shuffle template to uClock (index 0 = straight time).
    uClock.setShuffleTemplate(const_cast<int8_t *>(currentTemplate.ticks), SHUFFLE_TEMPLATE_SIZE);
    uClock.setShuffle(state.currentShufflePatternIndex > 0);
  }
  break;

  default:
    // Unknown/unused control button
    break;
  }
}
