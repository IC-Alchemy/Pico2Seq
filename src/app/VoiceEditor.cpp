#include "VoiceEditor.h"
#include "../sensors/EncoderManager.h"
#include "../voice/VoicePresets.h"
#include "AppState.h"
#include "ClockService.h"
#include "StepPlayback.h"
#include "../ui/ControlSurfaceLogic.h"
#include "../ui/UIConstants.h"
#include "../ui/UITransitions.h"
#include <cstdlib>
#include <uClock.h>

// Patch-editing mode: transport parking, knob/button handling, and publishing.
// Entering clears performance holds so a stuck gate never drones under the editor.

namespace {
ControlSurface::EncoderMotion encoderMotion;
// Pending knob turn belongs to one voice/parameter/mode; switching resets it
// so motion never leaks into a different target.
struct EncoderTurn {
  bool editor = false;
  uint8_t voice = 0;
  VoiceEdit::Id id = VoiceEdit::Id::Count;
};
EncoderTurn encoderTurn;

void clearPerformanceControls() {
  for (auto &held : uiState.parameterButtonHeld)
    held = false;
  for (auto &held : uiState.randomizeWasPressed)
    held = false;
  for (auto &held : uiState.randomizeResetTriggered)
    held = false;
  for (auto &timestamp : uiState.padPressTimestamps)
    timestamp = 0;
  UITransitions::closeSettings(uiState);
  uiState.encoderControlWasPressed = uiState.gateSeqLengthMode = false;
  uiState.modGateParamSeqLengthsMode = uiState.slideMode = false;
  uiState.playStopWasPressed = uiState.voiceSwitchWasPressed = false;
  uiState.selectedStepForEdit = -1;
  uiState.currentEditParameter = ParamId::Count;
  uiState.latchedParameter = -1;
  uiState.shiftHeld = false;
  uiState.alchemyModeBannerUntil = uiState.oledNoticeUntil = 0;
  uiState.encoderBaseViewUntil = 0;
  VoiceEditor::clearEncoder();
}
} // namespace
namespace VoiceEditor {
void enter() {
  stopClockForEditor();
  clearPerformanceControls();
  uiState.voiceEditor.enter();
}
void exit() {
  uiState.voiceEditor.active = false;
  uiState.voiceEditor.fine = false;
  uiState.controlsWaitRelease = true;
  clearPerformanceControls();
}
void publish(uint8_t index, VoiceConfig &config) {
  if (!voiceManager || index >= VoiceSystem::MAX_VOICES)
    return;
  const auto id = voiceSystem.getVoiceId(index);
  // Glide rides its own update, so send it only when it moved; the rest ships
  // in the config push below.
  const auto *applied = voiceManager->getVoiceConfig(id);
  const bool slideMoved =
      !applied || applied->slideSeconds != config.slideSeconds;
  VoiceEdit::enablePatch(config);
  voiceManager->setVoiceConfig(id, config);
  if (slideMoved)
    voiceManager->setVoiceSlide(id, config.slideSeconds);
  uiState.voiceEditor.changed[index] = true;
}
void buttons(uint8_t buttons, uint8_t voices, uint32_t now) {
  auto &editor = uiState.voiceEditor;
  const auto input = editor.poll(buttons, voices, now);
  if (input.exit) {
    exit();
    return;
  }
  if (input.clearEncoder)
    clearEncoder();
  if (input.voice >= 0) {
    uiState.selectedVoiceIndex = static_cast<uint8_t>(input.voice);
    // Editor focus only switches the cursor bank: no note cleanup, no OLED fanfare.
  }
  const uint8_t index = uiState.selectedVoiceIndex;
  if (!voiceManager || index >= 4)
    return;
  const auto *requested =
      voiceManager->getVoiceConfig(voiceSystem.getVoiceId(index));
  if (!requested)
    return;
  auto &cursor = editor.cursor[index];
  if (!VoiceEdit::available(cursor, *requested))
    cursor = VoiceEdit::nextParameter(cursor, 1, *requested, true);
  if (input.group)
    cursor = VoiceEdit::nextParameter(cursor, input.group, *requested, true);
  if (input.parameter)
    cursor =
        VoiceEdit::nextParameter(cursor, input.parameter, *requested, false);
  if (input.reset) {
    VoiceConfig next = *requested;
    VoiceConfig defaults =
        VoicePresets::getPresetConfig(uiState.voicePresetIndices[index]);
    // Reset one field in the running engine's own units (engines differ).
    if (defaults.engine != next.engine)
      VoiceEdit::setValue(VoiceEdit::Id::Engine, defaults, next.engine);
    if (next.engine == ENGINE_RECIPE)
      VoiceEdit::setValue(VoiceEdit::Id::Recipe, defaults,
                          VoiceEdit::value(VoiceEdit::Id::Recipe, next));
    VoiceEdit::setValue(cursor, next, VoiceEdit::value(cursor, defaults));
    publish(index, next);
  }
}
void clearEncoder() {
  magEncoder.clearPendingTicks();
  encoderMotion.reset();
}
void encoder(float delta) {
  const auto &editor = uiState.voiceEditor;
  const auto index = uiState.selectedVoiceIndex;
  if (!voiceManager || index >= VoiceSystem::MAX_VOICES ||
      (editor.active && editor.waitRelease))
    return;
  const auto id = editor.active ? editor.cursor[index] : encoderTarget();
  if (editor.active != encoderTurn.editor || index != encoderTurn.voice ||
      id != encoderTurn.id) {
    encoderMotion.reset();
    encoderTurn = {editor.active, index, id};
  }
  encoderMotion.add(editor.active && editor.fine ? delta * 0.1f : delta);
  const auto *requested =
      voiceManager->getVoiceConfig(voiceSystem.getVoiceId(index));
  if (!requested || !VoiceEdit::available(id, *requested)) {
    encoderMotion.reset(); // no hidden motion lands when it reappears
    return;
  }
  VoiceConfig next = *requested;
  const float before = VoiceEdit::value(id, next);
  if (VoiceEdit::stepped(id)) {
    const int steps = encoderMotion.takeSteps(
        SensorConstants::MagneticEncoder::STEPPED_VALUE_DETENT);
    for (int i = 0; i < std::abs(steps); ++i)
      VoiceEdit::adjust(id, next, steps > 0 ? 1.0f : -1.0f);
  } else {
    VoiceEdit::adjust(id, next,
                      encoderMotion.takeContinuous(
                          SensorConstants::MagneticEncoder::MINIMUM_INCREMENT_THRESHOLD));
  }
  // A knob pinned at its limit must not republish (avoids queue churn).
  if (VoiceEdit::value(id, next) == before)
    return;
  publish(index, next);
  // While playing, the sounding note takes the new base without retrigger, so
  // the OLED value matches what is heard.
  if (!editor.active)
    updateActiveVoiceState(0, *AppState::sequencers[index]);
  // Outside the editor the OLED shows step values, which can mask a base change;
  // flash the base while the knob turns.
  if (!editor.active)
    uiState.encoderBaseViewUntil = millis() + ENCODER_BASE_VIEW_MS;
}
VoiceEdit::Id encoderTarget() {
  using Id = VoiceEdit::Id;
  if (uiState.currentEncoderParameter == EncoderParameterMode::SlideTime)
    return Id::SlideTime; // Voice-only knob target, not the sequencer Slide toggle.
  const ParamId lane = parameterForEncoderMode(uiState.currentEncoderParameter);
  // VoiceEdit's leading IDs mirror ParamId by design (see sequenceLane());
  // keep the bridge here so core descriptors stay editor-agnostic.
  static_assert(static_cast<uint8_t>(Id::Note) == static_cast<uint8_t>(ParamId::Note) &&
                static_cast<uint8_t>(Id::Velocity) == static_cast<uint8_t>(ParamId::Velocity) &&
                static_cast<uint8_t>(Id::Cutoff) == static_cast<uint8_t>(ParamId::Filter) &&
                static_cast<uint8_t>(Id::Attack) == static_cast<uint8_t>(ParamId::Attack) &&
                static_cast<uint8_t>(Id::Decay) == static_cast<uint8_t>(ParamId::Decay) &&
                static_cast<uint8_t>(Id::Octave) == static_cast<uint8_t>(ParamId::Octave));
  return lane == ParamId::Count ? Id::Velocity : static_cast<Id>(lane);
}
} // namespace VoiceEditor
