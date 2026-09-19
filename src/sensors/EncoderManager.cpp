#include "EncoderManager.h"
#include "../app/AppState.h"
#include "../app/StepPlayback.h"
#include "../app/VoiceEditor.h"
#include "../voice/VoicePresets.h"
#include <Arduino.h>
#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../ui/UIState.h"
#include "../ui/ControlSurfaceLogic.h"
#include "../ui/ButtonManager.h"
#include "../ui/ParameterEditing.h"
#include <algorithm>
#include <cmath>
#include "../voice/VoiceManager.h"
#include "../voice/VoiceSystem.h" // VoiceSystem::MAX_VOICES

// =======================
//   MAGNETIC ENCODER GLOBALS
// =======================

namespace
{
// This Pico2Seq unit uses a TMAG5273A, whose factory-programmed address is
// 0x35.  Set it explicitly: MagEncoder's zero-address sentinel resolves to
// the TMAG5273B default (0x22).
MagEncoder::Config makeMagEncoderConfig()
{
  MagEncoder::Config cfg;
  cfg.sensor = MagEncoder::Sensor::TMAG5273;
  cfg.i2cAddress = TMAG5273::ADDRESS_A;
  cfg.minScale = SensorConstants::MagneticEncoder::SLOW_TURN_SCALE;
  return cfg;
}

} // namespace

// The magnetic encoder driver for the TMAG5273A Velocity Encoder board.
MagEncoder magEncoder(makeMagEncoderConfig());

// Note: currentEncoderParameter is accessed via uiState.currentEncoderParameter

void updateEncoderBaseValues(UIState &uiState)
{
  ParameterEditing::syncGesture(uiState);
  if (uiState.encoderInputChanged || uiState.controlsWaitRelease) {
    magEncoder.clearPendingTicks();
    uiState.encoderInputChanged = false;
  }
  if (!magEncoder.isConnected() || uiState.controlsWaitRelease) return;
  // Every read's increment is forwarded, however small: the driver has
  // already drained those ticks, and the step and base paths accumulate them.
  const float delta=magEncoder.takeParameterIncrement(-1.0f,1.0f,3);
  if(delta==0.0f) return;
  VoiceEditor::encoder(delta);
}

// =======================
//   ENCODER HELPER FUNCTIONS (moved from main file)
// =======================

/**
 * Gets the current value of the active encoder parameter, normalized to a 0.0-1.0 range.
 * This is used for visual feedback, such as controlling the brightness or color of an LED.
 */
float getEncoderParameterValue()
{
  if(!voiceManager || uiState.selectedVoiceIndex>=4) return 0.0f;
  const auto *config=voiceManager->getVoiceConfig(voiceSystem.getVoiceId(uiState.selectedVoiceIndex));
  return config?VoiceEdit::value(VoiceEditor::encoderTarget(),*config):0.0f;
}

void initEncoderBaseValues()
{
  // VoiceSetup initializes each patch's bases from its preset. The old
  // encoderBaseValues array was removed with that ownership change.
  VoiceEditor::clearEncoder();
}

void resetEncoderBaseValues(UIState &uiState, bool currentVoiceOnly)
{
  if(!voiceManager) return;
  for(uint8_t index=0;index<4;++index) {
    if(currentVoiceOnly && index!=uiState.selectedVoiceIndex) continue;
    const auto *requested=voiceManager->getVoiceConfig(voiceSystem.getVoiceId(index));
    if(!requested) continue;
    VoiceConfig next=*requested;
    VoiceConfig defaults=VoicePresets::getPresetConfig(uiState.voicePresetIndices[index]);
    if(defaults.engine!=next.engine) VoiceEdit::setValue(VoiceEdit::Id::Engine,defaults,next.engine);
    if(next.engine==ENGINE_RECIPE) VoiceEdit::setValue(VoiceEdit::Id::Recipe,defaults,VoiceEdit::value(VoiceEdit::Id::Recipe,next));
    for(uint8_t lane=0;lane<PARAM_ID_COUNT;++lane) {
      const auto id=static_cast<VoiceEdit::Id>(lane);
      VoiceEdit::setValue(id,next,VoiceEdit::value(id,defaults));
    }
    next.slideSeconds=defaults.slideSeconds;
    VoiceEditor::publish(index,next);
  }
  VoiceEditor::clearEncoder();
}
