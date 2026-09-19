#include "VoiceEditor.h"
#include "../sensors/EncoderManager.h"
#include "../voice/VoicePresets.h"
#include "AppState.h"
#include "ClockService.h"
#include "StepPlayback.h"
#include "../ui/ControlSurfaceLogic.h"
#include "../ui/UIConstants.h"
#include "../ui/UITransitions.h"
#include "../ui/ParameterEditing.h"
#include <cstdlib>
#include <uClock.h>

namespace {
void clearPerformanceControls() {
  UITransitions::clearParameterHolds(uiState);
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
  // Glide time rides its own control update, so only send it when it moved.
  // Every other field travels inside the config push below.
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
    // Editor focus keeps per-voice cursors and does not trigger performance
    // note cleanup or the performance OLED voice-switch notification.
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
    // Reset a field in a changed engine using defaults in that engine's units.
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
  uiState.editGesture.reset();
}
void encoder(float delta) {
  const auto index = uiState.selectedVoiceIndex;
  if (!voiceManager || index >= VoiceSystem::MAX_VOICES) return;
  const auto *requested = voiceManager->getVoiceConfig(voiceSystem.getVoiceId(index));
  if (!requested) return;
  VoiceConfig next = *requested;
  const auto result = ParameterEditing::encoder(uiState, *AppState::sequencers[index], next, delta);
  if (result.write.changed())
    updateActiveVoiceState(static_cast<uint8_t>(uiState.selectedStepForEdit), *AppState::sequencers[index]);
  if (!result.patchChanged) return;
  publish(index, next);
  if (!uiState.voiceEditor.active) {
    updateActiveVoiceState(UINT8_MAX, *AppState::sequencers[index]);
    uiState.encoderBaseViewUntil = millis() + ENCODER_BASE_VIEW_MS;
  }
}
VoiceEdit::Id encoderTarget() {
  return ParameterEditing::encoderTarget(uiState);
}
} // namespace VoiceEditor
