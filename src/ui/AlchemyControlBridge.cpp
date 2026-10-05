// AlchemyControlBridge.cpp — see AlchemyControlBridge.h for the contract.

#include "AlchemyControlBridge.h"
#include "../app/AppState.h"
#include "../app/ArpPlayback.h"
#include "../app/ReverbEditor.h"
#include "../app/Session.h"
#include "../app/StepPlayback.h"
#include "../app/VoiceDump.h"
#include "../app/VoiceEditor.h"
#include "../app/VoiceEnvelope.h"

#include "ButtonHandlers.h"
#include "ButtonManager.h"
#include "UIConstants.h"
#include "UIEventHandler.h"
#include "UITransitions.h"
#include "../AlchemyUI/src/ButtonMap.h"
#include "../pico2seq-core/arpeggiator/Arpeggiator.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../voice/DelayTiming.h"

#include <uClock.h>

namespace
{
// Why a banner: a physical strap flip has no on-screen affordance, so the
// OLED needs a short-lived announcement or the new button meanings look dead.
// OLED banner window after a mode flip.
constexpr unsigned long kModeBannerDurationMs = 600;

// Notes in a random arp chord: four is enough for a seventh chord, which is
// the widest chord the four voices can voice in Chord pattern.
constexpr uint8_t kRandomChordNotes = 4;

// Utility-fader ranges.
constexpr float kTempoMinBpm = 45.0f;
constexpr float kTempoMaxBpm = 200.0f;
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

  // Same for the loop button: held through reset is not a press.
  loopButton_.begin(digitalRead(loopButtonPin_) == LOW, nowMs);

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
  // The loop button is its own pin: debounce it every pass, whatever screen is up.
  const auto loopEvent = loopButton_.update(digitalRead(loopButtonPin_) == LOW, nowMs,
                                            (buttons & LoopPage::Controls::kShift) != 0);
  if(uiState.voiceEditor.active || uiState.controlsWaitRelease) {
    UITransitions::cancelGateLengthHold(uiState);
    // Keep physical histories current even while their performance actions are
    // suppressed. No release can become a new action after leaving the editor.
    for(uint8_t bit=0;bit<8;++bit) {
      buttonEdges_[kButtonRole][bit].take(buttonAt(buttonSlot_,bit));
      buttonEdges_[kSliderRole][bit].take(buttonAt(sliderSlot_,bit));
    }
    if (panel_.tiles().sliderFrameChanged()) {
      for(uint8_t channel=0;channel<4;++channel)
        faders_.accept(channel,panel_.tiles().faderRaw(channel));
    }
    latch_.reset(); playSettingsOpenedThisPress_=false;
    uiState.reverbPage.observe(buttons, voices); // a button held across the editor is not a new press
    uiState.loopPage.observe(buttons, voices);
    uiState.tuningPage.observe(buttons, voices);
    if(uiState.voiceEditor.active) VoiceEditor::buttons(buttons,voices,nowMs);
    else if(buttons==0 && voices==0) uiState.controlsWaitRelease=false;
    return;
  }

  handleModeStrap(nowMs, uiState);

  // The loop button acts on every screen (a take can start while the reverb is being set).
  // A chord press is not an action: it only opens the page, below.
  handleLoopButton(loopEvent, nowMs, uiState);
  announceLoopPhase(nowMs, uiState);

  // The Loop Settings page is decided before the other pages: it is the only one whose chord
  // starts on the loop pin, so nothing below can claim it. While it is open or draining it
  // owns every tile button, fader and pad.
  if (handleLoopPage(buttons, voices, loopEvent, uiState))
    return;

  // The Tuning page (Shift + Utility 3) is decided first: nothing else claims that chord,
  // and while it is open or draining it owns every button, fader and pad.
  if (handleTuningPage(buttons, voices, nowMs, uiState))
    return;

  // The Reverb page is the third key of the Shift + 6 chord, so it is decided before
  // the ADSR chord below claims that modifier pair.
  if (handleReverbPage(buttons, voices, uiState))
    return;

  // Reserve Shift + button 6 before any single-button or Shift+voice action.
  // Keep histories current through the complete chord and its release tail.
  const auto envelopeInput = uiState.voiceEnvelope.poll(buttons, voices);
  if (envelopeInput.consumed) {
    for (uint8_t bit = 0; bit < 8; ++bit) {
      buttonEdges_[kButtonRole][bit].take(buttonAt(buttonSlot_, bit));
      buttonEdges_[kSliderRole][bit].take(buttonAt(sliderSlot_, bit));
    }
    if (uiState.voiceEnvelope.active || envelopeInput.exit) latch_.reset();
    for (auto &held : uiState.parameterButtonHeld) held = false;
    uiState.latchedParameter = -1;
    UITransitions::cancelGateLengthHold(uiState);
    for (auto &held : uiState.randomizeWasPressed) held = false;
    playSettingsOpenedThisPress_ = saveLoadLatch_ = delayTogglePress_ = false;
    clearChordThisPress_ = clearAllLatch_ = false;
    editorHoldArmed_ = editorHoldFired_ = false;
    uiState.shiftHeld = shiftWasHeld_ = (buttons & VoiceEnvelope::Controls::kShift) != 0;
    if (envelopeInput.voice >= 0) {
      UITransitions::openVoiceEnvelope(uiState, static_cast<uint8_t>(envelopeInput.voice));
      VoiceEditor::clearEncoder();
      faders_.resetDeadband();
    }
    if (envelopeInput.exit) {
      uiState.envFaderLane = ParamId::Count;
      uiState.resetStepsLightsFlag = uiState.voiceSwitchTriggered = true;
      VoiceEditor::clearEncoder();
    }
    if (envelopeInput.modifierTap) {
      // Shift+6 alone still selects rhythm 6 / re-syncs the arp, latches
      // Octave, or cycles the encoder. Defer it until the chord is ruled out.
      if (uiState.arp.active()) {
        if (uiState.alchemyMode == UIState::AlchemyMode::Param) {
          uiState.arp.setRhythmPreset(5);
          uiState.showArpControl(UIState::ArpControl::Rhythm, nowMs);
        } else {
          uiState.arp.restart();
          uiState.showArpControl(UIState::ArpControl::Restart, nowMs);
        }
      } else if (uiState.alchemyMode == UIState::AlchemyMode::Param && !uiState.slideMode) {
        const auto lane = static_cast<uint8_t>(ControlSurface::recordParamForButtonBit(5));
        latch_.onParamButton(lane, true, true);
        latch_.onParamButton(lane, false, false);
        latch_.applyTo(uiState.parameterButtonHeld, PARAM_ID_COUNT);
        uiState.latchedParameter = latch_.latched();
        handleParameterButtonById(lane, true, uiState);
      } else if (uiState.alchemyMode == UIState::AlchemyMode::Utility) {
        handleEncoderControlPress(uiState);
      }
    }
    if (!uiState.voiceEnvelope.active || uiState.voiceEnvelope.waitRelease ||
        uiState.voiceEnvelope.chordPending) {
      faders_.resetDeadband();
      return;
    }
    handleFaders(uiState, sequencers);
    return;
  }

  // Shift (bit 7 of the button tile) is a plain level in both modes.
  uiState.shiftHeld = buttonAt(buttonSlot_, 7).held();
  // Shift changes tempo to feedback, delay mix to time and volume to macro.
  // Re-arm all three on either edge so their other targets keep their values.
  if (uiState.shiftHeld != shiftWasHeld_)
  {
    shiftWasHeld_ = uiState.shiftHeld;
    faders_.resetShiftTargets();
  }

  // SliderModule buttons: voice select/length hold, or transport chords with Shift —
  // identical in both modes.
  handleVoiceButtons(nowMs, uiState);
  if(uiState.voiceEditor.active) return;

  if (uiState.alchemyMode == UIState::AlchemyMode::Param)
  {
    if (uiState.arp.active())
      handleArpPatternButtons(uiState);
    else
      handleParamButtons(uiState);
  }
  else
  {
    if (uiState.arp.active())
      handleArpUtilityButtons(nowMs, uiState);
    else
      handleUtilityButtons(nowMs, uiState, sequencers);
  }

  // Disarm the faders whenever what they edit changes: the selected voice, or
  // entering, leaving or moving Step Edit (ENV mode). A newly focused step or
  // voice must not snap to wherever the faders happen to rest. Arpeggiator mode
  // swaps its two fader layers on Shift; both edges must re-arm from rest.
  const int stepForEdit = uiState.arp.active() ? lastStepForEdit_ : uiState.selectedStepForEdit;
  const bool arpShift = uiState.arp.active() && uiState.shiftHeld;
  if (uiState.selectedVoiceIndex != lastVoiceIndex_ || stepForEdit != lastStepForEdit_ ||
      arpShift != lastArpShift_ || uiState.arp.active() != lastArpActive_)
  {
    if (uiState.selectedVoiceIndex != lastVoiceIndex_)
      uiState.arpControl = UIState::ArpControl::None;
    lastVoiceIndex_ = uiState.selectedVoiceIndex;
    lastStepForEdit_ = stepForEdit;
    lastArpShift_ = arpShift;
    lastArpActive_ = uiState.arp.active();
    faders_.resetDeadband();
  }

  handleFaders(uiState, sequencers);
}

// --- Reverb page ---------------------------------------------------------------

// Why this runs before the ADSR chord: Shift + 6 is that chord's modifier pair, and
// its deferred "Shift+6 alone" action would otherwise fire when the page closes.
// Opening resets the ADSR chord state for the same reason. While the page is open
// or draining its release tail every physical history keeps advancing, so no
// release can become an action once normal input resumes.
bool AlchemyControlBridge::handleReverbPage(uint8_t buttons, uint8_t voices, UIState &uiState)
{
  const bool canOpen = !uiState.voiceEnvelope.active && !uiState.voiceEnvelope.waitRelease &&
                       !uiState.gateSeqLengthMode;
  const auto input = uiState.reverbPage.poll(buttons, voices, canOpen);
  if (!input.consumed)
    return false;

  for (uint8_t bit = 0; bit < 8; ++bit)
  {
    buttonEdges_[kButtonRole][bit].take(buttonAt(buttonSlot_, bit));
    buttonEdges_[kSliderRole][bit].take(buttonAt(sliderSlot_, bit));
  }
  latch_.reset();
  for (auto &held : uiState.parameterButtonHeld) held = false;
  uiState.latchedParameter = -1;
  UITransitions::cancelGateLengthHold(uiState);
  for (auto &held : uiState.randomizeWasPressed) held = false;
  playSettingsOpenedThisPress_ = saveLoadLatch_ = delayTogglePress_ = false;
  clearChordThisPress_ = clearAllLatch_ = false;
  editorHoldArmed_ = editorHoldFired_ = false;
  uiState.shiftHeld = shiftWasHeld_ = (buttons & ReverbPage::Controls::kShift) != 0;

  if (input.open)
  {
    UITransitions::openReverbPage(uiState); // also drops any half-finished ADSR chord
    VoiceEditor::clearEncoder();
    faders_.resetDeadband(); // faders must be moved to engage: no snapping to rest positions
  }
  if (input.exit)
  {
    UITransitions::closeReverbPage(uiState);
    VoiceEditor::clearEncoder();
    faders_.resetDeadband();
  }
  if (input.toggleFreeze)
    ReverbEditor::toggleFreeze();
  if (input.toggleLayer)
    faders_.resetDeadband(); // the same physical faders now mean other settings

  if (uiState.reverbPage.active && !uiState.reverbPage.waitRelease)
    handleReverbFaders(uiState);
  return true;
}

void AlchemyControlBridge::handleReverbFaders(UIState &uiState)
{
  if (!panel_.tiles().sliderFrameChanged())
    return;
  for (uint8_t channel = 0; channel < ControlSurface::FaderMap::kChannelCount; ++channel)
  {
    if (!faders_.accept(channel, panel_.tiles().faderRaw(channel)))
      continue;
    const auto control = ControlSurface::reverbControlForFader(uiState.reverbPage.layer, channel);
    ReverbEditor::setFromFader(control,
                               ControlSurface::FaderMap::normalize(faders_.filtered(channel)));
  }
}

// --- Loop button and Loop Settings page ----------------------------------------

// Why taps and holds are handled here, outside the page: the loop button is a pin of its
// own, so it needs no tile and no Shift. Tap and hold keep their meaning on the Loop page,
// which lets a take be started while the levels are being set.
void AlchemyControlBridge::handleLoopButton(LoopPage::Button::Event event, uint32_t nowMs,
                                            UIState &uiState)
{
  using Event = LoopPage::Button::Event;
  if (event != Event::Tap && event != Event::Hold)
    return;
  // The page's own opening/closing release tail is not an action.
  if (uiState.loopPage.waitRelease)
    return;
  if (loopController.phase() == LoopController::Phase::Unavailable)
  {
    uiState.oledNoticeKind = UIState::OledNoticeKind::LoopUnavailable;
    uiState.oledNoticeUntil = nowMs + OLED_NOTICE_DURATION_MS;
    return;
  }
  if (event == Event::Tap)
    loopController.tap(uClock.getTempo());
  else
    loopController.clear();
}

// One short banner per change of state ("LOOP ARMED" -> "LOOP REC" -> "LOOP PLAY"), so the
// player is never guessing what a tap did. The Loop page shows the live state instead.
void AlchemyControlBridge::announceLoopPhase(uint32_t nowMs, UIState &uiState)
{
  using Phase = LoopController::Phase;
  const Phase phase = loopController.phase();
  if (phase == lastLoopPhase_)
    return;
  lastLoopPhase_ = phase;
  UIState::OledNoticeKind kind = UIState::OledNoticeKind::None;
  switch (phase)
  {
  case Phase::Armed: kind = UIState::OledNoticeKind::LoopArmed; break;
  case Phase::Recording: kind = UIState::OledNoticeKind::LoopRecording; break;
  case Phase::Playing: kind = UIState::OledNoticeKind::LoopPlaying; break;
  case Phase::Overdubbing: kind = UIState::OledNoticeKind::LoopOverdub; break;
  case Phase::Empty: kind = UIState::OledNoticeKind::LoopCleared; break;
  case Phase::Unavailable: break;
  }
  if (kind == UIState::OledNoticeKind::None)
    return;
  uiState.oledNoticeKind = kind;
  uiState.oledNoticeValue = phase == Phase::Armed ? loopController.armedSteps() : loopController.loopSteps();
  uiState.oledNoticeUntil = nowMs + OLED_NOTICE_DURATION_MS;
}

// Why this mirrors the Reverb page: the page is live (the transport and the loop keep
// running), so every physical history keeps advancing while it is open or draining its
// release tail and no release can become an action once normal input resumes.
bool AlchemyControlBridge::handleLoopPage(uint8_t buttons, uint8_t voices,
                                          LoopPage::Button::Event event, UIState &uiState)
{
  const bool canOpen = !uiState.voiceEnvelope.active && !uiState.voiceEnvelope.chordPending &&
                       !uiState.voiceEnvelope.waitRelease && !uiState.gateSeqLengthMode &&
                       !uiState.reverbPage.active && !uiState.reverbPage.waitRelease &&
                       !uiState.tuningPage.active && !uiState.tuningPage.waitRelease;
  const auto input = uiState.loopPage.poll(buttons, voices, loopButton_.held(),
                                           event == LoopPage::Button::Event::ChordPress, canOpen);
  if (!input.consumed)
    return false;

  for (uint8_t bit = 0; bit < 8; ++bit)
  {
    buttonEdges_[kButtonRole][bit].take(buttonAt(buttonSlot_, bit));
    buttonEdges_[kSliderRole][bit].take(buttonAt(sliderSlot_, bit));
  }
  // The pages that did not run this pass must not read what was held across it as new.
  uiState.reverbPage.observe(buttons, voices);
  uiState.tuningPage.observe(buttons, voices);
  latch_.reset();
  for (auto &held : uiState.parameterButtonHeld) held = false;
  uiState.latchedParameter = -1;
  UITransitions::cancelGateLengthHold(uiState);
  for (auto &held : uiState.randomizeWasPressed) held = false;
  playSettingsOpenedThisPress_ = saveLoadLatch_ = delayTogglePress_ = false;
  clearChordThisPress_ = clearAllLatch_ = false;
  editorHoldArmed_ = editorHoldFired_ = false;
  uiState.shiftHeld = shiftWasHeld_ = (buttons & LoopPage::Controls::kShift) != 0;

  if (input.open)
  {
    UITransitions::openLoopPage(uiState);
    VoiceEditor::clearEncoder();
    faders_.resetDeadband(); // faders must be moved to engage: no snapping to rest positions
  }
  if (input.exit)
  {
    UITransitions::closeLoopPage(uiState);
    VoiceEditor::clearEncoder();
    faders_.resetDeadband();
  }

  if (uiState.loopPage.active && !uiState.loopPage.waitRelease)
    handleLoopFaders(uiState);
  return true;
}

void AlchemyControlBridge::handleLoopFaders(UIState &uiState)
{
  if (!panel_.tiles().sliderFrameChanged() || !voiceManager)
    return;
  for (uint8_t channel = 0; channel < ControlSurface::FaderMap::kChannelCount; ++channel)
  {
    if (!faders_.accept(channel, panel_.tiles().faderRaw(channel)))
      continue;
    const float position = ControlSurface::FaderMap::normalize(faders_.filtered(channel));
    const ControlSurface::LoopControl control = ControlSurface::loopControlForFader(channel);
    switch (control)
    {
    case ControlSurface::LoopControl::LoopVolume:
      voiceManager->loop().setLoopVolume(ControlSurface::loopVolumeForFader(position));
      break;
    case ControlSurface::LoopControl::LoopLength:
      // The length of the NEXT take; a loop already recorded keeps its own.
      loopController.setSizeIndex(ControlSurface::loopSizeIndexForFader(position));
      break;
    case ControlSurface::LoopControl::SequencerVolume:
      voiceManager->loop().setSequencerVolume(ControlSurface::sequencerVolumeForFader(position));
      break;
    case ControlSurface::LoopControl::Regen:
      voiceManager->loop().setRegen(ControlSurface::loopRegenForFader(position));
      break;
    case ControlSurface::LoopControl::Count:
      continue;
    }
    uiState.loopPage.lastControl = static_cast<uint8_t>(control); // the OLED highlights it
  }
}

// --- Tuning page ---------------------------------------------------------------

// Why this mirrors the Reverb page: the page is live (the transport keeps running), so
// every physical history keeps advancing while it is open or draining its release tail and
// no release can become an action once normal input resumes. Pad presses arrive through
// UIEventHandler. A tuning change also moves currentScale to a scale of the new tuning
// (tuning/TuningScales.h); the voices sample both on their own control pass, so for at most
// one pass a voice can see the new tuning with the old scale - momentary and harmless,
// because every pitch is clamped to the audible range.
bool AlchemyControlBridge::handleTuningPage(uint8_t buttons, uint8_t voices, uint32_t nowMs,
                                            UIState &uiState)
{
  const bool canOpen = uiState.alchemyMode == UIState::AlchemyMode::Utility &&
                       !uiState.voiceEnvelope.active && !uiState.voiceEnvelope.chordPending &&
                       !uiState.voiceEnvelope.waitRelease && !uiState.gateSeqLengthMode &&
                       !uiState.reverbPage.active && !uiState.reverbPage.waitRelease;
  const auto input = uiState.tuningPage.poll(buttons, voices, canOpen, nowMs);
  if (!input.consumed)
    return false;

  for (uint8_t bit = 0; bit < 8; ++bit)
  {
    buttonEdges_[kButtonRole][bit].take(buttonAt(buttonSlot_, bit));
    buttonEdges_[kSliderRole][bit].take(buttonAt(sliderSlot_, bit));
  }
  uiState.reverbPage.observe(buttons, voices);
  latch_.reset();
  for (auto &held : uiState.parameterButtonHeld) held = false;
  uiState.latchedParameter = -1;
  UITransitions::cancelGateLengthHold(uiState);
  for (auto &held : uiState.randomizeWasPressed) held = false;
  playSettingsOpenedThisPress_ = saveLoadLatch_ = delayTogglePress_ = false;
  clearChordThisPress_ = clearAllLatch_ = false;
  editorHoldArmed_ = editorHoldFired_ = false;
  uiState.shiftHeld = shiftWasHeld_ = (buttons & TuningPage::Controls::kShift) != 0;

  if (input.open)
  {
    UITransitions::openTuningPage(uiState);
    // A pad held in Arpeggiator mode belongs to the chord; its release is swallowed by the
    // page, so drop it now instead of leaving the note stuck.
    uiState.arp.releaseAllHeldPads();
    VoiceEditor::clearEncoder();
    faders_.resetDeadband(); // faders must be moved to engage: no snapping to rest positions
  }
  if (input.exit)
  {
    UITransitions::closeTuningPage(uiState);
    VoiceEditor::clearEncoder();
    faders_.resetDeadband();
  }

  if (input.scaleSlot >= 0)
    UITransitions::showTuningNotice(
        uiState, TuningPage::scaleButton(static_cast<uint8_t>(input.scaleSlot), tuningSelection, currentScale),
        nowMs);
  if (input.swap)
    UITransitions::showTuningNotice(
        uiState, TuningPage::swapAB(tuningSelection, tuningBank, currentScale), nowMs);
  if (input.recallSlot >= 0)
    UITransitions::showTuningNotice(
        uiState,
        TuningPage::hotRecall(static_cast<uint8_t>(input.recallSlot), tuningSelection, tuningBank,
                              currentScale),
        nowMs);
  if (input.storeSlot >= 0)
    UITransitions::showTuningNotice(
        uiState, TuningPage::hotStore(static_cast<uint8_t>(input.storeSlot), tuningSelection, tuningBank),
        nowMs);

  if (uiState.tuningPage.active && !uiState.tuningPage.waitRelease)
    handleTuningFaders(uiState);

  // The scale may have moved (a new tuning brings its own): the arpeggiator lays its pads out
  // by the notes per octave of whatever is playing.
  uiState.arp.setScaleNotesPerOctave(currentScaleNotesPerOctave());
  return true;
}

void AlchemyControlBridge::handleTuningFaders(UIState &uiState)
{
  if (!panel_.tiles().sliderFrameChanged())
    return;
  for (uint8_t channel = 0; channel < ControlSurface::FaderMap::kChannelCount; ++channel)
  {
    if (!faders_.accept(channel, panel_.tiles().faderRaw(channel)))
      continue;
    const TuningPage::Fader fader = TuningPage::faderForChannel(channel);
    if (fader == TuningPage::Fader::Unassigned)
      continue;
    uiState.tuningPage.lastControl = static_cast<uint8_t>(fader); // the OLED highlights it
    const float position = ControlSurface::FaderMap::normalize(faders_.filtered(channel));
    // The scale fader spreads over the playing tuning's own scales; the arp layout is
    // refreshed by handleTuningPage once per pass.
    TuningPage::applyFader(fader, position, tuningSelection, currentScale);
  }
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
  UITransitions::cancelGateLengthHold(uiState);
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
// Why Voice 4 defers its action to release/hold: with Shift it carries two
// commands on one button -- a tap opens the voice editor, a hold toggles
// Arpeggiator mode -- so the tap can only be decided once the finger is up (the
// same reason the Play button defers its transport action to release).
void AlchemyControlBridge::handleVoiceButtons(uint32_t nowMs, UIState &uiState)
{
  const bool shift = uiState.shiftHeld;
  for (uint8_t voice = 0; voice < 4; ++voice)
  {
    const TileButton &tileButton = buttonAt(sliderSlot_, voice);
    ButtonEdges &edges = buttonEdges_[kSliderRole][voice];
    const bool changed = edges.take(tileButton);
    // Plain voice holds run in both panel modes, including passes with no
    // edge. Release (or a missing tile) cancels only that voice's own hold.
    if (UITransitions::updateGateLengthHold(uiState, voice, tileButton.held(),
          tileButton.heldMilliseconds(nowMs), UITimingConstants::LONG_PRESS_THRESHOLD_MS))
      latch_.reset();

    // Shift+Voice 4 retains its editor tap / arpeggiator hold gesture.
    const bool actsWhileHeld = voice == 3 && editorHoldArmed_ && !editorHoldFired_;
    if (!changed && !(actsWhileHeld && tileButton.held()))
    {
      continue;
    }

    if (voice == 3 && ((edges.pressEdge && shift) || editorHoldArmed_))
    {
      if (edges.pressEdge)
      {
        editorHoldArmed_ = true;
        editorHoldFired_ = false;
      }
      else if (tileButton.held() && editorHoldArmed_ && !editorHoldFired_ &&
               tileButton.heldMilliseconds(nowMs) >= UITimingConstants::LONG_PRESS_THRESHOLD_MS)
      {
        // Hold consumed: the release must not also open the editor.
        editorHoldFired_ = true;
        arpModeToggle(uiState);
        // What the faders edit changed with the mode: re-arm the deadband so a
        // resting fader cannot snap an arp setting the moment the mode flips.
        faders_.resetDeadband();
      }
      else if (edges.releaseEdge)
      {
        if (!editorHoldFired_)
          VoiceEditor::enter();
        editorHoldArmed_ = false;
        editorHoldFired_ = false;
      }
      continue;
    }

    if (!edges.pressEdge)
    {
      continue;
    }

    if (!shift)
    {
      // Direct voice select (also switches the pad banks via PadBank).
      selectVoice(uiState, voice);
      UITransitions::beginGateLengthHold(uiState, voice);
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

    // Length entry owns the pads and OLED until the voice button is released.
    // Still consume tile edges so no parameter press leaks out afterwards.
    if (uiState.gateSeqLengthMode)
      continue;

    // Bits 0-5 follow ButtonMap.h order. The 5th button is silkscreened Decay
    // but records Release: Decay is a timbre lane on most presets, while
    // Release shapes the tail on all of them.
    const ParamId paramId = ControlSurface::recordParamForButtonBit(bit);
    if (paramId == ParamId::Count)
      continue;
    const uint8_t paramIdValue = static_cast<uint8_t>(paramId);
    latch_.onParamButton(paramIdValue, edges.pressEdge, uiState.shiftHeld);
    latch_.applyTo(uiState.parameterButtonHeld, PARAM_ID_COUNT);
    uiState.latchedParameter = latch_.latched();

    handleParameterButtonById(paramIdValue, edges.pressEdge, uiState);
  }
}

// --- ButtonModule8: transport and session (shared by both modes) -----------------

// Why these two live outside the per-mode panels: Play/Stop and Session must
// keep exactly one meaning in the sequencer and in Arpeggiator mode, so both
// utility panels call the same code. Everything else on the panel is allowed to
// differ per mode.
void AlchemyControlBridge::handleTransportButton(const ButtonState &button, UIState &uiState)
{
  if (button.pressEdge)
  {
    // Transport action is deferred to release/hold so a long-press can toggle
    // settings without ever stopping playback.
    playSettingsOpenedThisPress_ = false;
  }
  else if (button.held && !playSettingsOpenedThisPress_ &&
           button.heldMs >= UITimingConstants::LONG_PRESS_THRESHOLD_MS)
  {
    // Long-press (any clock state): toggle settings open/closed. Entering
    // while running keeps the transport playing -- preset apply is staged
    // and click-safe.
    playSettingsOpenedThisPress_ = true;
    if (uiState.settingsMode)
      closeSettingsMode(uiState);
    else
      openSettingsMode(uiState);
  }
  else if (button.releaseEdge)
  {
    if (!playSettingsOpenedThisPress_)
    {
      if (uiState.settingsMode && isClockRunning)
      {
        // Short-press while running inside settings: exit settings only, keep
        // the transport playing.
        closeSettingsMode(uiState);
      }
      else
      {
        handleControlButton(BUTTON_PLAY_STOP, uiState); // stop+settings / start
      }
    }
    playSettingsOpenedThisPress_ = false;
  }
}

void AlchemyControlBridge::handleSessionButton(const ButtonState &button)
{
  if (button.pressEdge)
  {
    saveLoadLatch_ = false;
  }
  else if (button.held && !saveLoadLatch_ &&
           button.heldMs >= UITimingConstants::LONG_PRESS_THRESHOLD_MS)
  {
    saveLoadLatch_ = true; // consume the hold; release must not re-trigger
    Session::requestLoad();
  }
  else if (button.releaseEdge && !saveLoadLatch_)
  {
    Session::requestSave();
  }
}

void AlchemyControlBridge::handleSessionOrDelayButton(const ButtonState &button,
                                                      UIState &uiState)
{
  if (button.pressEdge)
  {
    delayTogglePress_ = uiState.shiftHeld;
    if (delayTogglePress_)
    {
      uiState.delaySynced = !uiState.delaySynced;
      if (voiceManager)
      {
        voiceManager->setDelayNoteIndex(uiState.delayNoteIndex);
        voiceManager->setDelaySynced(uiState.delaySynced);
      }
      uiState.oledNoticeKind = uiState.delaySynced
                                   ? UIState::OledNoticeKind::DelaySync
                                   : UIState::OledNoticeKind::DelayMsMode;
      uiState.oledNoticeValue = voiceManager
                                    ? static_cast<uint16_t>(lroundf(voiceManager->getDelayTime() * 1000.0f))
                                    : 300;
      uiState.oledNoticeUntil = millis() + OLED_NOTICE_DURATION_MS;
      faders_.resetShiftTargets();
      return;
    }
  }
  if (delayTogglePress_)
  {
    if (button.releaseEdge) delayTogglePress_ = false;
    return;
  }
  handleSessionButton(button);
}

// --- ButtonModule8, Arpeggiator mode --------------------------------------------

// Why patterns are a single-select group rather than latchable holds: a step
// parameter is held while a hand writes it, but a pattern is a mode the arp
// stays in -- the button is not tracked afterwards. Latch (bit 6) is the one
// toggle on this panel, and it is reachable from the Utility panel as well so a
// player can latch without touching the strap.
void AlchemyControlBridge::handleArpPatternButtons(UIState &uiState)
{
  for (uint8_t bit = 0; bit < 7; ++bit) // bits 0-6; bit 7 is Shift (read above)
  {
    ButtonEdges &edges = buttonEdges_[kButtonRole][bit];
    if (!edges.take(buttonAt(buttonSlot_, bit)) || !edges.pressEdge)
    {
      continue;
    }

    if (bit == 6)
    {
      if (uiState.shiftHeld) uiState.arp.restart();
      else uiState.arp.toggleLatch();
      uiState.showArpControl(uiState.shiftHeld ? UIState::ArpControl::Restart : UIState::ArpControl::Latch, millis());
      continue;
    }

    if (uiState.shiftHeld)
    {
      uiState.arp.setRhythmPreset(bit);
      uiState.showArpControl(UIState::ArpControl::Rhythm, millis());
      faders_.resetDeadband();
      continue;
    }
    const Arpeggiator::Pattern pattern = Arpeggiator::patternForButtonBit(bit);
    if (pattern != Arpeggiator::Pattern::Count)
    {
      uiState.arp.setPattern(pattern);
      uiState.arpControl = UIState::ArpControl::None;
    }
  }
}

// Why the step-only slots are replaced instead of shadowed: swing templates,
// encoder-target cycling and pattern clearing all drive the step sequencer, and
// in Arpeggiator mode those buttons would be dead. Octave range, re-sync and
// chord randomize are their arp counterparts, while Play, Session, Scale and
// Theme keep their positions because they mean the same thing in both modes.
void AlchemyControlBridge::handleArpUtilityButtons(uint32_t nowMs, UIState &uiState)
{
  for (uint8_t bit = 0; bit < 7; ++bit) // bits 0-6; bit 7 is Shift (read above)
  {
    const TileButton &tileButton = buttonAt(buttonSlot_, bit);
    ButtonEdges &edges = buttonEdges_[kButtonRole][bit];
    const bool actsWhileHeld = bit <= 1; // Play and Session keep their holds
    if (!edges.take(tileButton) && !(actsWhileHeld && tileButton.held()))
    {
      continue;
    }

    const ButtonState state{edges.pressEdge, edges.releaseEdge, tileButton.held(),
                            tileButton.heldMilliseconds(nowMs)};
    switch (bit)
    {
    case 0: // Play / Stop
      handleTransportButton(state, uiState);
      break;

    case 1: // Session; Shift + Delay toggles ms / tempo sync.
      handleSessionOrDelayButton(state, uiState);
      break;

    case 2: // Scale cycle: the arp plays the same scale table as the sequencer.
      if (edges.pressEdge)
        handleControlButton(BUTTON_CHANGE_SCALE, uiState);
      break;

    case 3: // Octave range cycle 1..4. The step sequencer's swing templates do
            // not apply here: the arp swings itself (Arpeggiator::scheduleNext).
      if (edges.pressEdge) {
        uiState.arp.cycleOctaves();
        uiState.showArpControl(UIState::ArpControl::Octaves, nowMs);
        faders_.resetDeadband();
      }
      break;

    case 4: // Theme cycle: the arp panel is painted from the same theme table.
      if (edges.pressEdge)
        handleControlButton(BUTTON_CHANGE_THEME, uiState);
      break;

    case 5: // Latch (the same toggle as the Param panel, so a chord can be held
            // without changing the strap). Shift + tap re-syncs the walk to the
            // chord's root on the next tick.
      if (!edges.pressEdge)
        break;
      if (uiState.shiftHeld)
        uiState.arp.restart();
      else
        uiState.arp.toggleLatch();
      uiState.showArpControl(uiState.shiftHeld ? UIState::ArpControl::Restart : UIState::ArpControl::Latch, nowMs);
      break;

    case 6: // Randomize the chord (tap); Shift + tap clears it.
      if (!edges.pressEdge)
        break;
      if (uiState.shiftHeld)
      {
        uiState.arp.clearChord();
        uiState.arp.setLatch(false);
        uiState.oledNoticeKind = UIState::OledNoticeKind::VoiceCleared;
        uiState.oledNoticeUntil = nowMs + OLED_NOTICE_DURATION_MS;
      }
      else
      {
        // Seeded from the clock so two taps in a row cannot draw the same chord.
        uiState.arp.randomizeChord(kRandomChordNotes,
                                   static_cast<uint32_t>(nowMs) * 2654435761u + 1u);
        uiState.oledNoticeKind = UIState::OledNoticeKind::Randomized;
        uiState.oledNoticeUntil = nowMs + OLED_NOTICE_DURATION_MS;
      }
      break;

    default:
      break;
    }
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

    const ButtonState state{edges.pressEdge, edges.releaseEdge, tileButton.held(),
                            tileButton.heldMilliseconds(nowMs)};
    switch (bit)
    {
    case 0: // Play / Stop
      handleTransportButton(state, uiState);
      break;

    case 1: // Session; Shift + Delay toggles ms / tempo sync.
      handleSessionOrDelayButton(state, uiState);
      break;

    case 2: // Scale cycle
      if (edges.pressEdge)
        handleControlButton(BUTTON_CHANGE_SCALE, uiState);
      break;

    case 3: // Swing template cycle
      if (edges.pressEdge)
        handleControlButton(BUTTON_CHANGE_SWING_PATTERN, uiState);
      break;

    case 4: // Theme cycle; Shift + Theme prints every voice's values to Serial
      if (edges.pressEdge)
      {
        if (uiState.shiftHeld)
          printAllVoiceValues();
        else
          handleControlButton(BUTTON_CHANGE_THEME, uiState);
      }
      break;

    case 5: // Encoder target / Settings page; length entry uses voice holds.
      if (edges.pressEdge)
        handleEncoderControlPress(uiState);
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
// physical faders mean Tempo/DelayMix/Volume/Gate in both strap positions, but the
// selected step's Attack/Decay/Sustain/Release in Step Edit (ENV mode). The
// The median + deadband gate (accept()) stops a newly selected voice or step
// from snapping to a stale fader position. Feed it only a fresh checksum-valid
// slider frame, then use its median-filtered value rather than the raw sample.
void AlchemyControlBridge::handleFaders(UIState &uiState,
                                        const SequencerView &sequencers)
{
  if (!panel_.tiles().sliderFrameChanged())
  {
    return;
  }

  for (uint8_t channel = 0; channel < ControlSurface::FaderMap::kChannelCount; ++channel)
  {
    const uint16_t rawCounts = panel_.tiles().faderRaw(channel);
    if (!faders_.accept(channel, rawCounts))
    {
      continue;
    }
    const float normalized =
        ControlSurface::FaderMap::normalize(faders_.filtered(channel));
    if (uiState.voiceEnvelope.active) {
      if (VoiceEnvelope::set(uiState.selectedVoiceIndex, channel, normalized, uiState.arp.active()))
        uiState.envFaderLane = ControlSurface::FaderMap::assignmentFor(true, channel).paramId;
      continue;
    }
    // Arpeggiator mode replaces the whole fader set: no steps are edited there,
    // so unshifted faders carry rhythm and Shift carries range, gate, tone.

    const ControlSurface::FaderAssignment assignment =
        uiState.arp.active()
            ? ControlSurface::FaderMap::arpAssignmentFor(channel, uiState.shiftHeld)
            : ControlSurface::FaderMap::assignmentFor(uiState.selectedStepForEdit >= 0, channel);

    switch (assignment.target)
    {
    case ControlSurface::FaderTarget::None:
      break;

    case ControlSurface::FaderTarget::ArpOctaves:
      uiState.arp.setOctaves(Arpeggiator::octavesForFader(normalized));
      uiState.showArpControl(UIState::ArpControl::Octaves, millis());
      break;

    case ControlSurface::FaderTarget::ArpGate:
      uiState.arp.setGate(normalized);
      uiState.showArpControl(UIState::ArpControl::Gate, millis());
      break;

    case ControlSurface::FaderTarget::ArpSwing:
      uiState.arp.setSwing(normalized);
      uiState.showArpControl(UIState::ArpControl::Swing, millis());
      break;

    case ControlSurface::FaderTarget::ArpFilter:
      uiState.arp.setFilter(normalized);
      uiState.showArpControl(UIState::ArpControl::Filter, millis());
      break;

    case ControlSurface::FaderTarget::ArpHits:
    case ControlSurface::FaderTarget::ArpLength:
    case ControlSurface::FaderTarget::ArpRotate:
    case ControlSurface::FaderTarget::ArpAccent:
    {
      uiState.arp.setRhythmFader(channel, normalized);
      static constexpr UIState::ArpControl controls[] = {
          UIState::ArpControl::Hits, UIState::ArpControl::Length,
          UIState::ArpControl::Rotate, UIState::ArpControl::Accent};
      uiState.showArpControl(controls[channel], millis());
      break;
    }

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
      // Plain move: lock-free global gain on Core 1's final mix (captured
      // by the session). Shift + move: master macro knob instead — one
      // 0..1 morph across the master-bus compressor's Warm/Glue/Punch curve.
      if (voiceManager)
      {
        if (ControlSurface::masterFaderAction(uiState.shiftHeld) ==
            ControlSurface::MasterFaderAction::Macro)
        {
          voiceManager->setMasterMacro(normalized);
          uiState.oledNoticeKind = UIState::OledNoticeKind::Macro;
          uiState.macroNoticePercent =
              static_cast<uint8_t>(lroundf(normalized * 100.0f));
          uiState.oledNoticeUntil = millis() + OLED_NOTICE_DURATION_MS;
        }
        else
        {
          voiceManager->setGlobalVolume(normalized);
        }
      }
      break;

    case ControlSurface::FaderTarget::Tempo:
      if (ControlSurface::tempoFaderAction(uiState.shiftHeld) ==
          ControlSurface::TempoFaderAction::DelayFeedback)
      {
        if (voiceManager)
          voiceManager->setDelayFeedback(normalized);
        uiState.oledNoticeKind = UIState::OledNoticeKind::DelayFeedback;
        uiState.oledNoticeValue =
            static_cast<uint16_t>(lroundf(normalized * 100.0f));
        uiState.oledNoticeUntil = millis() + OLED_NOTICE_DURATION_MS;
      }
      else
      {
        uClock.setTempo(kTempoMinBpm +
                        normalized * (kTempoMaxBpm - kTempoMinBpm));
      }
      break;

    case ControlSurface::FaderTarget::DelayMix:
      // One fader, two jobs: the wet mix on its own, and with Shift held the
      // same fader sweeps delay time instead (the same move-time retarget
      // shape as the ENV lanes' Shift reset). Both report on the OLED.
      if (uiState.shiftHeld)
      {
        if (uiState.delaySynced)
        {
          uiState.delayNoteIndex = DelayTiming::indexForFader(normalized);
          if (voiceManager)
            voiceManager->setDelayNoteIndex(uiState.delayNoteIndex);
          uiState.oledNoticeKind = UIState::OledNoticeKind::DelaySync;
        }
        else
        {
          const float seconds = ControlSurface::delaySecondsForFader(normalized);
          if (voiceManager)
            voiceManager->setDelayTime(seconds);
          uiState.oledNoticeKind = UIState::OledNoticeKind::DelayTime;
          uiState.oledNoticeValue =
              static_cast<uint16_t>(lroundf(seconds * 1000.0f));
        }
      }
      else
      {
        if (voiceManager)
          voiceManager->setDelayMix(normalized);
        uiState.oledNoticeKind = UIState::OledNoticeKind::DelayMix;
        uiState.oledNoticeValue =
            static_cast<uint16_t>(lroundf(normalized * 100.0f));
      }
      uiState.oledNoticeUntil = millis() + OLED_NOTICE_DURATION_MS;
      break;

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
