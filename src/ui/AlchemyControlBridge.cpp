// AlchemyControlBridge.cpp — see AlchemyControlBridge.h for the contract.

#include "AlchemyControlBridge.h"
#include "../app/AppState.h"
#include "../app/Session.h"
#include "../app/StepPlayback.h"
#include "../app/VoiceEditor.h"

#include "ButtonHandlers.h"
#include "ButtonManager.h"
#include "UIConstants.h"
#include "UIEventHandler.h"
#include "../AlchemyUI/src/ButtonMap.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../pico2seq-core/sequencer/ShuffleTemplates.h"

#include <uClock.h>

namespace
{
// Why a banner: a physical strap flip has no on-screen affordance, so the
// OLED needs a short-lived announcement or the new button meanings look dead.
// OLED banner window after a mode flip.
constexpr unsigned long kModeBannerDurationMs = 600;

// Utility-fader ranges.
constexpr float kTempoMinBpm = 45.0f;
constexpr float kTempoMaxBpm = 200.0f;
constexpr int8_t kSwingMaxTicks = 45; // half of a 120-tick 16th at PPQN 480
} // namespace

// Why re-resolve instead of caching once: tile slots are scan order, so a tile
// that misses a probe (or is unplugged) shifts every slot after it. Looking up
// by TYPE_ID each pass keeps voice buttons vs. utility buttons routed to the
// right physical tile even on a half-populated rig.
void AlchemyControlBridge::resolveSlots()
{
  sliderSlot_ = panel_.tiles().sliderSlot();
  buttonSlot_ = panel_.tiles().firstSlotOfType(alchemy::kTypeButton4);
}

// Why a stand-in instead of a null/guard at every call site: edge tracking and
// hold timing run unconditionally, so a missing tile must still answer with a
// well-behaved "never held" level rather than forcing null checks (or OOB
// indexing) through all button handlers.
const TileButton &AlchemyControlBridge::buttonAt(int slot, uint8_t bit)
{
  // Stand-in for a tile that did not answer: never held, no edges, zero hold.
  static const TileButton kAbsentButton{};
  if (slot < 0)
  {
    return kAbsentButton;
  }
  return panel_.tiles().button(slot, bit);
}

// Why begin() seeds state from hardware instead of default-constructing it:
// a boot in Utility mode (or with a button held through reset) must not look
// like a fresh mode flip / fresh press on the first update(), or the rig would
// banner, latch, or trigger actions before the player touches anything.
void AlchemyControlBridge::begin(TwoWire &bankA, TwoWire *bankB, uint32_t nowMs)
{
  panel_.begin(bankA, bankB, nowMs);
  resolveSlots();

  // Seed the stabilizer with the strap so a boot in Utility mode does not
  // look like a flip on the first update().
  const bool rawHigh = digitalRead(modeSwitchPin_) == HIGH;
  const ControlSurface::Mode initial = (rawHigh == ControlSurface::kModeParamLevel)
                                           ? ControlSurface::Mode::Param
                                           : ControlSurface::Mode::Utility;
  mode_.begin(initial, nowMs);

  // Start edge tracking from the boot-time button levels so a button held
  // through reset does not fire a phantom press.
  const int roleSlot[kRoleCount] = {sliderSlot_, buttonSlot_};
  for (uint8_t role = 0; role < kRoleCount; ++role)
  {
    for (uint8_t bit = 0; bit < kButtonBits; ++bit)
    {
      buttonEdges_[role][bit].prevHeld_ = buttonAt(roleSlot[role], bit).held();
    }
  }
}

// Why update() is ordered poll -> remap -> suppress-or-dispatch: only one tile
// transaction fits per 1 ms slice, so fresh levels must land before any edge
// work; slot mapping must follow because a reprobe can flip present/absent;
// and the voice-editor / wait-release gate must swallow performance actions
// (while still advancing edge + fader history) so button releases inside the
// editor can never leak out as sequencer/mode actions afterwards.
void AlchemyControlBridge::update(uint32_t nowMs, UIState &uiState,
                                  const SequencerView &sequencers)
{
  // Poll due tiles first: one transaction pair at most per pass.
  panel_.update(nowMs);

  // Tiles can go offline and come back (kReprobeIntervalMs), which reshuffles
  // nothing but can turn a slot present/absent, so re-read the mapping.
  resolveSlots();

  uint8_t buttons=0, voices=0;
  for(uint8_t bit=0;bit<8;++bit) if(buttonAt(buttonSlot_,bit).held()) buttons|=1u<<bit;
  for(uint8_t bit=0;bit<4;++bit) if(buttonAt(sliderSlot_,bit).held()) voices|=1u<<bit;
  if(uiState.voiceEditor.active || uiState.controlsWaitRelease) {
    // Keep physical histories current even while their performance actions are
    // suppressed. No release can become a new action after leaving the editor.
    for(uint8_t bit=0;bit<8;++bit) {
      buttonEdges_[kButtonRole][bit].take(buttonAt(buttonSlot_,bit));
      buttonEdges_[kSliderRole][bit].take(buttonAt(sliderSlot_,bit));
    }
    for(uint8_t channel=0;channel<4;++channel)
      faders_.accept(channel,panel_.tiles().faderRaw(channel));
    latch_.reset(); playSettingsOpenedThisPress_=false;
    if(uiState.voiceEditor.active) VoiceEditor::buttons(buttons,voices,nowMs);
    else if(buttons==0 && voices==0) uiState.controlsWaitRelease=false;
    return;
  }

  handleModeStrap(nowMs, uiState);

  // Shift (bit 7 of the button tile) is a plain level in both modes.
  uiState.shiftHeld = buttonAt(buttonSlot_, 7).held();

  // SliderModule buttons: voice select, or transport chords with Shift —
  // identical in both modes.
  handleVoiceButtons(uiState);
  if(uiState.voiceEditor.active) return;

  if (uiState.alchemyMode == UIState::AlchemyMode::Param)
  {
    handleParamButtons(uiState);
  }
  else
  {
    handleUtilityButtons(nowMs, uiState, sequencers);
  }

  // Disarm the faders whenever what they edit changes: the selected voice,
  // or entering, leaving or moving Step Edit (ENV mode). A newly focused
  // step or voice must not snap to wherever the faders happen to rest.
  if (uiState.selectedVoiceIndex != lastVoiceIndex_ ||
      uiState.selectedStepForEdit != lastStepForEdit_)
  {
    lastVoiceIndex_ = uiState.selectedVoiceIndex;
    lastStepForEdit_ = uiState.selectedStepForEdit;
    faders_.resetDeadband();
  }

  handleFaders(uiState, sequencers);
}

// --- Mode strap ----------------------------------------------------------------

// Why debounce in software: the Param/Utility strap is a bare physical switch,
// so contact bounce would otherwise strobe the whole control surface between
// two button meanings. The stabilizer turns that into one settled flip that
// UIState can trust.
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

// Why a flip clears everything: holds, the shift latch, and fader engagement
// all mean different things per mode, so carrying them across would fire the
// new mode's actions from the old mode's fingers (and snap parameters when
// fader deadbands no longer match). The banner flag is raised here because
// this is the only point that knows a settled change just happened.
void AlchemyControlBridge::onModeFlip(uint32_t nowMs, UIState &uiState)
{
  // Nothing sticks across a mode change: drop the latch and every derived
  // hold, snap the fader deadband so the new mode's controls engage, and
  // raise the OLED banner flag.
  latch_.reset();
  latch_.applyTo(uiState.parameterButtonHeld, PARAM_ID_COUNT);
  uiState.latchedParameter = -1;
  uiState.shiftHeld = false;
  faders_.resetDeadband();
  uiState.alchemyModeBannerUntil = nowMs + kModeBannerDurationMs;
}

// --- SliderModule buttons --------------------------------------------------------

// Why voice buttons bypass the mode split: voice selection (and its Shift
// transport chords) must stay under muscle memory in both Param and Utility.
// Chords reuse the same ButtonHandlers entry points as the legacy matrix so
// there is only one transport/randomize/scale implementation to maintain.
void AlchemyControlBridge::handleVoiceButtons(UIState &uiState)
{
  const bool shift = uiState.shiftHeld;
  for (uint8_t voice = 0; voice < 4; ++voice)
  {
    ButtonEdges &edges = buttonEdges_[kSliderRole][voice];
    if (!edges.take(buttonAt(sliderSlot_, voice)) || !edges.pressEdge)
    {
      continue;
    }

    if (!shift)
    {
      // Direct voice select (also switches the pad banks via PadBank).
      selectVoice(uiState, voice);
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
      VoiceEditor::enter();
      return;
    default:
      break;
    }
  }
}

// --- ButtonModule8, Param mode ---------------------------------------------------

// Why Param buttons go through the shift latch: taps select a step parameter
// for the faders while Shift+taps audition/lock it, exactly like the legacy
// matrix — reusing the latch keeps tile and matrix editing semantics identical.
// Slide (bit 6) is exempt because it is a modal toggle with legacy side
// effects (clearing conflicting modes), not a latchable parameter.
void AlchemyControlBridge::handleParamButtons(UIState &uiState)
{
  // A slide transition from any entry point clears the logical latch. Consume
  // physical edges while sliding, but never rebuild parameter holds behind it.
  if (uiState.slideMode)
    latch_.reset();
  for (uint8_t bit = 0; bit < 7; ++bit) // bits 0-6; bit 7 is Shift (read above)
  {
    ButtonEdges &edges = buttonEdges_[kButtonRole][bit];
    if (!edges.take(buttonAt(buttonSlot_, bit)))
    {
      continue;
    }

    if (bit == 6)
    {
      // Shared slide transition; only physical latch history belongs here.
      if (edges.pressEdge)
      {
        const bool wasSlide = uiState.slideMode;
        handleSlideModePress(uiState);
        if (!wasSlide && uiState.slideMode)
        {
          // Slide entry cleared every hold; keep the latch coherent too.
          latch_.reset();
        }
      }
      continue;
    }

    if (uiState.slideMode)
      continue;

    // Bits 0-5 map straight onto ParamId Note..Octave (ButtonMap.h order).
    const uint8_t paramId = bit;
    latch_.onParamButton(paramId, edges.pressEdge, uiState.shiftHeld);
    latch_.applyTo(uiState.parameterButtonHeld, PARAM_ID_COUNT);
    uiState.latchedParameter = latch_.latched();

    handleParameterButtonById(paramId, edges.pressEdge, uiState);
  }
}

// --- ButtonModule8, Utility mode -------------------------------------------------

// Why Utility handling is edge-plus-hold instead of edge-only: Play, Session,
// and Randomize overload tap vs. long-press (settings toggle, load-vs-save,
// clear-one-vs-clear-all). Acting mid-hold on the held level is what makes the
// long-press reachable; per-press latch flags exist so the hold consuming the
// action still suppresses the matching release edge.
void AlchemyControlBridge::handleUtilityButtons(uint32_t nowMs, UIState &uiState,
                                                const SequencerView &sequencers)
{
  for (uint8_t bit = 0; bit < 7; ++bit) // bits 0-6; bit 7 is Shift (read above)
  {
    const TileButton &tileButton = buttonAt(buttonSlot_, bit);
    ButtonEdges &edges = buttonEdges_[kButtonRole][bit];
    // Play (0) and Session (1) also act part-way through a hold, on passes
    // with no edge. Skipping those passes made both long presses unreachable.
    // The Randomize Shift chord needs the same mid-hold passes for its
    // clear-all hold, but only while the chord is armed (Shift at press).
    const bool actsWhileHeld = bit <= 1 || (bit == 6 && uiState.shiftHeld);
    if (!edges.take(tileButton) && !(actsWhileHeld && tileButton.held()))
    {
      continue;
    }

    switch (bit)
    {
    case 0: // Play / Stop
      if (edges.pressEdge)
      {
        // Transport action is deferred to release/hold so a long-press can
        // toggle settings without ever stopping playback.
        playSettingsOpenedThisPress_ = false;
      }
      else if (tileButton.held() && !playSettingsOpenedThisPress_ &&
               tileButton.heldMilliseconds(nowMs) >= UITimingConstants::LONG_PRESS_THRESHOLD_MS)
      {
        // Long-press (any clock state): toggle settings open/closed. Entering
        // while running keeps the transport playing — preset apply is staged
        // and click-safe.
        playSettingsOpenedThisPress_ = true;
        if (uiState.settingsMode)
        {
          closeSettingsMode(uiState);
        }
        else
        {
          openSettingsMode(uiState);
        }
      }
      else if (edges.releaseEdge)
      {
        if (!playSettingsOpenedThisPress_)
        {
          if (uiState.settingsMode && isClockRunning)
          {
            // Short-press while running inside settings: exit settings only,
            // keep the transport playing.
            closeSettingsMode(uiState);
          }
          else
          {
            handleControlButton(BUTTON_PLAY_STOP, uiState); // stop+settings / start
          }
        }
        playSettingsOpenedThisPress_ = false;
      }
      break;

    case 1: // Session save (tap) / load last saved (long-press)
      if (edges.pressEdge)
      {
        saveLoadLatch_ = false;
      }
      else if (tileButton.held() && !saveLoadLatch_ &&
               tileButton.heldMilliseconds(nowMs) >= UITimingConstants::LONG_PRESS_THRESHOLD_MS)
      {
        saveLoadLatch_ = true; // consume the hold; release must not re-trigger
        Session::requestLoad();
      }
      else if (edges.releaseEdge && !saveLoadLatch_)
      {
        Session::requestSave();
      }
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

    case 6: // Randomize selected voice (long-press reset via pollUIHeldButtons).
      // Shift chords: tap clears the selected voice's whole pattern, hold
      // clears every voice. A chord press never begins a randomize press, so
      // the poll-driven long-press reset cannot also fire from it.
      if (edges.pressEdge)
      {
        clearChordThisPress_ = uiState.shiftHeld;
        if (clearChordThisPress_)
        {
          clearAllLatch_ = false;
        }
        else
        {
          beginRandomizePress(uiState.selectedVoiceIndex, uiState);
        }
      }
      else if (clearChordThisPress_)
      {
        if (tileButton.held() && !clearAllLatch_ &&
            tileButton.heldMilliseconds(nowMs) >= UITimingConstants::LONG_PRESS_THRESHOLD_MS)
        {
          clearAllLatch_ = true; // consume the hold; release must not also clear
          clearAllSequencerVoices(uiState, sequencers);
        }
        else if (edges.releaseEdge && !clearAllLatch_)
        {
          if (Sequencer *selected = sequencers.get(uiState.selectedVoiceIndex))
          {
            clearSequencerVoice(uiState, *selected,
                                uiState.selectedVoiceIndex);
          }
        }
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

// Why faders fan out by assignment instead of by channel: the same four
// physical faders mean Tempo/Swing/Volume/Gate in both strap positions, but the
// selected step's Attack/Decay/Sustain/Release in Step Edit (ENV mode). The
// deadband gate (accept()) stops a newly selected voice or step from
// snapping to a stale fader position, and the shuffle buffer is static
// because uClock retains the pointer for ISR ticks.
void AlchemyControlBridge::handleFaders(UIState &uiState,
                                        const SequencerView &sequencers)
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
        ControlSurface::FaderMap::assignmentFor(uiState.selectedStepForEdit >= 0, channel);

    switch (assignment.target)
    {
    case ControlSurface::FaderTarget::None:
      break;

    case ControlSurface::FaderTarget::EnvLane:
      // The fader's position is the step's absolute value; Shift + move
      // hands the lane back to the patch value instead.
      if (uiState.shiftHeld)
        resetStepToPatch(assignment.paramId);
      else
        recordParameter(assignment.paramId, normalized);
      uiState.envFaderLane = assignment.paramId;
      uiState.envViewUntil = millis() + ENCODER_BASE_VIEW_MS;
      break;

    case ControlSurface::FaderTarget::MasterVolume:
      // VoiceManager applies this lock-free gain on Core 1's final mix. The
      // session captures it, so the fader position survives a save/load.
      if (voiceManager)
      {
        voiceManager->setGlobalVolume(normalized);
      }
      break;

    case ControlSurface::FaderTarget::Tempo:
      uClock.setTempo(kTempoMinBpm +
                      normalized * (kTempoMaxBpm - kTempoMinBpm));
      break;

    case ControlSurface::FaderTarget::SwingAmount:
    {
      // Continuous shuffle: delay every odd 16th by up to half a step.
      // Use static storage duration because uClock stores this pointer for Core 0 ISR ticks.
      static int8_t continuousShuffleTicks[SHUFFLE_TEMPLATE_SIZE];
      const int8_t offset = static_cast<int8_t>(lroundf(normalized * kSwingMaxTicks));
      for (int i = 0; i < SHUFFLE_TEMPLATE_SIZE; ++i)
      {
        continuousShuffleTicks[i] = (i % 2 == 1) ? offset : 0;
      }
      uClock.setShuffleTemplate(continuousShuffleTicks, SHUFFLE_TEMPLATE_SIZE);
      uClock.setShuffle(offset > 0);
      break;
    }

    case ControlSurface::FaderTarget::GateLength:
    {
      Sequencer *selectedSequencer = sequencers.get(uiState.selectedVoiceIndex);
      if (selectedSequencer)
      {
        const float gateLengthValue =
            mapNormalizedValueToParamRange(ParamId::GateLength, normalized);
        const uint8_t gateLenSteps = selectedSequencer->getParameterStepCount(ParamId::GateLength);
        for (uint8_t step = 0; step < gateLenSteps; ++step)
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
