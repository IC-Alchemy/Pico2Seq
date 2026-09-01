// AlchemyControlBridge.cpp — see AlchemyControlBridge.h for the contract.

#include "AlchemyControlBridge.h"

#include "ButtonHandlers.h"
#include "ButtonManager.h"
#include "UIConstants.h"
#include "UIEventHandler.h"
#include "../AlchemyUI/src/ButtonMap.h"
#include "../midi/MidiManager.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../pico2seq-core/sequencer/ShuffleTemplates.h"

#include <uClock.h>

// Globals from the main sketch the bridge feeds (same externs the matrix
// handlers used).
extern bool isClockRunning;
extern float feedbackAmmount;

// Shared lidar/fader step-recording path (implemented in Pico2Seq.ino).
extern void updateParametersForStepNormalized(uint8_t stepToUpdate,
                                              float normalizedValue);

namespace
{
// OLED banner window after a mode flip.
constexpr unsigned long kModeBannerDurationMs = 800;

// Utility-fader ranges.
constexpr float kTempoMinBpm = 45.0f;
constexpr float kTempoMaxBpm = 200.0f;
constexpr int8_t kSwingMaxTicks = 60; // half of a 120-tick 16th at PPQN 480
constexpr float kDelayFeedbackMax = 0.91f;

ControlSurface::Mode bridgeMode(UIState::AlchemyMode mode)
{
  return (mode == UIState::AlchemyMode::Param) ? ControlSurface::Mode::Param
                                               : ControlSurface::Mode::Utility;
}
} // namespace

void AlchemyControlBridge::begin(TwoWire &bankA, TwoWire *bankB, uint32_t nowMs)
{
  panel_.begin(bankA, bankB, nowMs);

  // Seed the stabilizer with the strap so a boot in Utility mode does not
  // look like a flip on the first update().
  const bool rawHigh = digitalRead(modeSwitchPin_) == HIGH;
  const ControlSurface::Mode initial = (rawHigh == ControlSurface::kModeParamLevel)
                                           ? ControlSurface::Mode::Param
                                           : ControlSurface::Mode::Utility;
  mode_.begin(initial, nowMs);

  // Start edge tracking from the boot-time button levels so a button held
  // through reset does not fire a phantom press.
  for (uint8_t slot = 0; slot < 2; ++slot)
  {
    for (uint8_t bit = 0; bit < kButtonBits; ++bit)
    {
      buttonEdges_[slot][bit].prevHeld_ = panel_.tiles().button(slot, bit).held();
    }
  }
}

void AlchemyControlBridge::update(uint32_t nowMs, UIState &uiState,
                                  Sequencer *const *sequencers, size_t sequencerCount,
                                  MidiNoteManager &midiNoteManager)
{
  // Poll due tiles first: one transaction pair at most per pass.
  panel_.update(nowMs);

  handleModeStrap(nowMs, uiState);

  // Shift (bit 7 of the button tile) is a plain level in both modes.
  uiState.shiftHeld = panel_.tiles().button(kButtonTileSlot, 7).held();

  // SliderModule buttons: voice select, or transport chords with Shift —
  // identical in both modes.
  handleVoiceButtons(uiState, midiNoteManager, sequencers, sequencerCount);

  if (uiState.alchemyMode == UIState::AlchemyMode::Param)
  {
    handleParamButtons(uiState);
  }
  else
  {
    handleUtilityButtons(nowMs, uiState);
  }

  handleFaders(uiState, sequencers, sequencerCount);
}

// --- Mode strap ----------------------------------------------------------------

void AlchemyControlBridge::handleModeStrap(uint32_t nowMs, UIState &uiState)
{
  const bool rawHigh = digitalRead(modeSwitchPin_) == HIGH;
  mode_.update(rawHigh, nowMs);

  uiState.alchemyMode = (mode_.mode() == ControlSurface::Mode::Param)
                            ? UIState::AlchemyMode::Param
                            : UIState::AlchemyMode::Utility;

  if (mode_.tookChange())
  {
    mode_.clearChange();
    onModeFlip(nowMs, uiState);
  }
}

void AlchemyControlBridge::onModeFlip(uint32_t nowMs, UIState &uiState)
{
  // Nothing sticks across a mode change: drop the latch and every derived
  // hold, snap the fader deadband so the new mode's controls engage, flash a
  // control LED and raise the OLED banner flag.
  latch_.reset();
  latch_.applyTo(uiState.parameterButtonHeld, PARAM_ID_COUNT);
  uiState.latchedParameter = -1;
  uiState.shiftHeld = false;
  faders_.resetDeadband();
  uiState.alchemyModeBannerUntil = nowMs + kModeBannerDurationMs;
  uiState.flash31Until = nowMs + CONTROL_LED_FLASH_DURATION_MS;
}

// --- SliderModule buttons --------------------------------------------------------

void AlchemyControlBridge::handleVoiceButtons(UIState &uiState,
                                              MidiNoteManager &midiNoteManager,
                                              Sequencer *const *sequencers,
                                              size_t sequencerCount)
{
  (void)sequencers;
  (void)sequencerCount;

  const bool shift = uiState.shiftHeld;
  for (uint8_t voice = 0; voice < 4; ++voice)
  {
    ButtonEdges &edges = buttonEdges_[kSliderSlot][voice];
    if (!edges.take(panel_.tiles().button(kSliderSlot, voice)) || !edges.pressEdge)
    {
      continue;
    }

    if (!shift)
    {
      // Direct voice select (also switches the pad banks via PadBank).
      selectVoice(uiState, midiNoteManager, voice);
      continue;
    }

    // Shift chords (taps, both modes): Play/Stop, Randomize selected voice,
    // Scale cycle, Delay toggle.
    switch (voice)
    {
    case 0:
      handleControlButton(BUTTON_PLAY_STOP, uiState);
      break;
    case 1:
    {
      // Short-press randomize only: begin+handle in the same pass so the
      // poll-driven long-press reset can never trigger from a chord.
      const uint8_t target = uiState.selectedVoiceIndex;
      beginRandomizePress(target, uiState);
      handleRandomizeButton(target, uiState);
      break;
    }
    case 2:
      handleControlButton(BUTTON_CHANGE_SCALE, uiState);
      break;
    case 3:
      handleControlButton(BUTTON_TOGGLE_DELAY, uiState);
      break;
    default:
      break;
    }
  }
}

// --- ButtonModule8, Param mode ---------------------------------------------------

void AlchemyControlBridge::handleParamButtons(UIState &uiState)
{
  for (uint8_t bit = 0; bit < 7; ++bit) // bits 0-6; bit 7 is Shift (read above)
  {
    ButtonEdges &edges = buttonEdges_[kButtonTileSlot][bit];
    if (!edges.take(panel_.tiles().button(kButtonTileSlot, bit)))
    {
      continue;
    }

    if (bit == 6)
    {
      // Slide button: exact legacy behavior, incl. clearing conflicting
      // modes when slide engages.
      if (edges.pressEdge)
      {
        const bool wasSlide = uiState.slideMode;
        handleSlideModePress(uiState);
        if (!wasSlide && uiState.slideMode)
        {
          // Slide entry cleared every hold; keep the latch coherent too.
          latch_.reset();
          uiState.latchedParameter = -1;
        }
      }
      continue;
    }

    // Bits 0-5 map straight onto ParamId Note..Octave (ButtonMap.h order).
    const uint8_t paramId = bit;
    latch_.onParamButton(paramId, edges.pressEdge, uiState.shiftHeld);
    latch_.applyTo(uiState.parameterButtonHeld, PARAM_ID_COUNT);
    uiState.latchedParameter = latch_.latched();

    handleParameterButtonById(paramId, edges.pressEdge, uiState);
  }
}

// --- ButtonModule8, Utility mode -------------------------------------------------

void AlchemyControlBridge::handleUtilityButtons(uint32_t nowMs, UIState &uiState)
{
  for (uint8_t bit = 0; bit < 7; ++bit) // bits 0-6; bit 7 is Shift (read above)
  {
    const TileButton &tileButton = panel_.tiles().button(kButtonTileSlot, bit);
    ButtonEdges &edges = buttonEdges_[kButtonTileSlot][bit];
    if (!edges.take(tileButton))
    {
      continue;
    }

    switch (bit)
    {
    case 0: // Play / Stop
      if (edges.pressEdge)
      {
        playSettingsOpenedThisPress_ = false;
        if (isClockRunning)
        {
          // Running: stop immediately (entering settings, as ever).
          handleControlButton(BUTTON_PLAY_STOP, uiState);
        }
        // Stopped: defer to release so a long-press can open settings.
      }
      else if (edges.releaseEdge)
      {
        if (!playSettingsOpenedThisPress_)
        {
          handleControlButton(BUTTON_PLAY_STOP, uiState); // start
        }
        playSettingsOpenedThisPress_ = false;
      }
      else if (tileButton.held() && !isClockRunning && !playSettingsOpenedThisPress_ &&
               tileButton.heldMilliseconds(nowMs) >= UITimingConstants::LONG_PRESS_THRESHOLD_MS)
      {
        // Long-press while stopped opens settings (preserved behavior).
        playSettingsOpenedThisPress_ = true;
        uiState.settingsMode = true;
        uiState.currentSubMode = UIState::SettingsSubMode::PRESET_SELECTION;
        uiState.inPresetSelection = true;
      }
      break;

    case 1: // Delay on/off toggle
      if (edges.pressEdge)
        handleControlButton(BUTTON_TOGGLE_DELAY, uiState);
      break;

    case 2: // Scale cycle
      if (edges.pressEdge)
        handleControlButton(BUTTON_CHANGE_SCALE, uiState);
      break;

    case 3: // Swing template cycle
      if (edges.pressEdge)
        handleControlButton(BUTTON_CHANGE_SWING_PATTERN, uiState);
      break;

    case 4: // Theme cycle
      if (edges.pressEdge)
        handleControlButton(BUTTON_CHANGE_THEME, uiState);
      break;

    case 5: // Encoder-control target cycle; hold enters gate seq length mode
      if (edges.pressEdge)
      {
        beginEncoderControlHold(uiState);
      }
      else if (edges.releaseEdge)
      {
        endEncoderControlHold(uiState);
      }
      break;

    case 6: // Randomize selected voice (long-press reset via pollUIHeldButtons)
      if (edges.pressEdge)
      {
        beginRandomizePress(uiState.selectedVoiceIndex, uiState);
      }
      else if (edges.releaseEdge)
      {
        handleRandomizeButton(uiState.selectedVoiceIndex, uiState);
      }
      break;

    default:
      break;
    }
  }
}

// --- Faders ----------------------------------------------------------------------

void AlchemyControlBridge::handleFaders(UIState &uiState,
                                        Sequencer *const *sequencers,
                                        size_t sequencerCount)
{
  for (uint8_t channel = 0; channel < ControlSurface::FaderMap::kChannelCount; ++channel)
  {
    const uint16_t rawCounts = panel_.tiles().faderRaw(channel);
    if (!faders_.accept(channel, rawCounts))
    {
      continue;
    }
    const float normalized = ControlSurface::FaderMap::normalize(rawCounts);
    const ControlSurface::FaderAssignment assignment =
        ControlSurface::FaderMap::assignmentFor(bridgeMode(uiState.alchemyMode), channel);

    switch (assignment.target)
    {
    case ControlSurface::FaderTarget::StepParam:
      // Same recording path as the lidar: records into the step in edit when
      // this fader's parameter is the armed/held one.
      if (uiState.selectedStepForEdit >= 0 &&
          getHeldParameterParamId(uiState) == assignment.paramId)
      {
        updateParametersForStepNormalized(
            static_cast<uint8_t>(uiState.selectedStepForEdit), normalized);
      }
      break;

    case ControlSurface::FaderTarget::Tempo:
      uClock.setTempo(kTempoMinBpm +
                      normalized * (kTempoMaxBpm - kTempoMinBpm));
      break;

    case ControlSurface::FaderTarget::SwingAmount:
    {
      // Continuous shuffle: delay every odd 16th by up to half a step.
      int8_t ticks[SHUFFLE_TEMPLATE_SIZE];
      const int8_t offset = static_cast<int8_t>(lroundf(normalized * kSwingMaxTicks));
      for (int i = 0; i < SHUFFLE_TEMPLATE_SIZE; ++i)
      {
        ticks[i] = (i % 2 == 1) ? offset : 0;
      }
      uClock.setShuffleTemplate(ticks, SHUFFLE_TEMPLATE_SIZE);
      uClock.setShuffle(offset > 0);
      break;
    }

    case ControlSurface::FaderTarget::DelayMix:
      feedbackAmmount = normalized * kDelayFeedbackMax;
      break;

    case ControlSurface::FaderTarget::GateLength:
    {
      Sequencer *selectedSequencer = nullptr;
      if (sequencers && uiState.selectedVoiceIndex < sequencerCount)
      {
        selectedSequencer = sequencers[uiState.selectedVoiceIndex];
      }
      if (selectedSequencer)
      {
        const float gateLengthValue =
            mapNormalizedValueToParamRange(ParamId::GateLength, normalized);
        for (uint8_t step = 0; step < NUMBER_OF_STEP_BUTTONS; ++step)
        {
          selectedSequencer->setStepParameterValue(ParamId::GateLength, step,
                                                  gateLengthValue);
        }
      }
      break;
    }
    }
  }
}
