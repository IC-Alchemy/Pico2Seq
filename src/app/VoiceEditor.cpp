#include "VoiceEditor.h"
#include "../sensors/EncoderManager.h"
#include "../voice/VoicePresets.h"
#include "AppState.h"
#include "ClockService.h"
#include <uClock.h>

namespace {
void clearPerformanceControls() {
  for (auto &held : uiState.parameterButtonHeld)
    held = false;
  for (auto &held : uiState.randomizeWasPressed)
    held = false;
  for (auto &held : uiState.randomizeResetTriggered)
    held = false;
  for (auto &timestamp : uiState.padPressTimestamps)
    timestamp = 0;
  uiState.settingsMode = uiState.inPresetSelection =
      uiState.inVoiceParameterMode = false;
  uiState.encoderControlWasPressed = uiState.gateSeqLengthMode = false;
  uiState.modGateParamSeqLengthsMode = uiState.slideMode = false;
  uiState.playStopWasPressed = uiState.voiceSwitchWasPressed = false;
  uiState.selectedStepForEdit = -1;
  uiState.currentEditParameter = ParamId::Count;
  uiState.latchedParameter = -1;
  uiState.shiftHeld = false;
  uiState.alchemyModeBannerUntil = uiState.oledNoticeUntil = 0;
  magEncoder.clearPendingTicks();
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
void publish(uint8_t index, const VoiceConfig &config) {
  if (!voiceManager || index >= VoiceSystem::MAX_VOICES)
    return;
  const auto id = voiceSystem.getVoiceId(index);
  VoiceConfig next = config;
  VoiceEdit::enablePatch(next);
  voiceManager->setVoiceConfig(id, next);
  voiceManager->setVoiceSlide(id, next.slideSeconds);
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
    magEncoder.clearPendingTicks();
  if (input.voice >= 0) {
    uiState.selectedVoiceIndex = static_cast<uint8_t>(input.voice);
    uiState.isVoice2Mode = input.voice == 1;
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
void encoder(float delta) {
  if (!voiceManager || uiState.voiceEditor.waitRelease)
    return;
  const auto index = uiState.selectedVoiceIndex;
  if (index >= 4)
    return;
  const auto *requested =
      voiceManager->getVoiceConfig(voiceSystem.getVoiceId(index));
  if (!requested)
    return;
  VoiceConfig next = *requested;
  const auto id = uiState.voiceEditor.cursor[index];
  if (!VoiceEdit::available(id, next))
    return;
  const float before = VoiceEdit::value(id, next);
  VoiceEdit::adjust(uiState.voiceEditor.cursor[index], next,
                    delta * (uiState.voiceEditor.fine ? 0.1f : 1.0f));
  if (VoiceEdit::value(id, next) == before)
    return;
  publish(index, next);
}
VoiceEdit::Id encoderTarget() {
  using Id = VoiceEdit::Id;
  switch (uiState.currentEncoderParameter) {
  case EncoderParameterMode::Note:
    return Id::Note;
  case EncoderParameterMode::Velocity:
    return Id::Velocity;
  case EncoderParameterMode::Filter:
    return Id::Cutoff;
  case EncoderParameterMode::Attack:
    return Id::Attack;
  case EncoderParameterMode::Decay:
    return Id::Decay;
  case EncoderParameterMode::Octave:
    return Id::Octave;
  case EncoderParameterMode::SlideTime:
    return Id::SlideTime;
  default:
    return Id::Velocity;
  }
}
} // namespace VoiceEditor
