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
  return cfg;
}
} // namespace

// The magnetic encoder driver for the TMAG5273A Velocity Encoder board.
MagEncoder magEncoder(makeMagEncoderConfig());

// Note: currentEncoderParameter is accessed via uiState.currentEncoderParameter

void updateEncoderBaseValues(UIState &uiState)
{
  if (!magEncoder.isConnected() || uiState.controlsWaitRelease) return;
  const float delta=magEncoder.takeParameterIncrement(-1.0f,1.0f,3);
  if(fabsf(delta)<SensorConstants::MagneticEncoder::MINIMUM_INCREMENT_THRESHOLD) return;
  if(uiState.voiceEditor.active) {VoiceEditor::encoder(delta);return;}
  if(!voiceManager || uiState.selectedVoiceIndex>=4) return;
  const auto index=uiState.selectedVoiceIndex;
  const auto *requested=voiceManager->getVoiceConfig(voiceSystem.getVoiceId(index));
  if(!requested) return;
  VoiceConfig next=*requested;
  const auto target=VoiceEditor::encoderTarget();
  const float before=VoiceEdit::value(target,next);
  VoiceEdit::adjust(target,next,delta);
  // A knob already pinned at the parameter's limit must not republish.
  if(VoiceEdit::value(target,next)==before) return;
  VoiceEditor::publish(index,next);
}

// --- Helper Functions for Step Parameter Editing ---

// Convert EncoderParameterMode to ParamId for step editing
ParamId convertEncoderParameterToParamId(EncoderParameterMode encoderParam)
{
  switch (encoderParam)
  {
  case EncoderParameterMode::Note:
    return ParamId::Note;
  case EncoderParameterMode::Velocity:
    return ParamId::Velocity;
  case EncoderParameterMode::Filter:
    return ParamId::Filter;
  case EncoderParameterMode::Attack:
    return ParamId::Attack;
  case EncoderParameterMode::Decay:
    return ParamId::Decay;
  case EncoderParameterMode::Octave:
    return ParamId::Octave;
  case EncoderParameterMode::SlideTime:
    return ParamId::Count; // SlideTime is not a step parameter
  default:
    return ParamId::Count; // Invalid for step editing
  }
}

float getParameterMinValueForParamId(ParamId paramId)
{
  switch (paramId)
  {
  case ParamId::Velocity:
  case ParamId::Filter:
  case ParamId::Attack:
  case ParamId::Decay:
    return SensorConstants::MagneticEncoder::PARAMETER_MIN_VALUE;

  case ParamId::Note:
    return static_cast<float>(SequencerConstants::NOTE_PARAMETER_MIN);

  default:
    return SensorConstants::MagneticEncoder::PARAMETER_MIN_VALUE;
  }
}

float getParameterMaxValueForParamId(ParamId paramId)
{
  switch (paramId)
  {
  case ParamId::Velocity:
  case ParamId::Filter:
  case ParamId::Attack:
  case ParamId::Decay:
    return SensorConstants::MagneticEncoder::PARAMETER_MAX_VALUE;

  case ParamId::Note:
    return static_cast<float>(SequencerConstants::NOTE_PARAMETER_MAX);

  default:
    return SensorConstants::MagneticEncoder::PARAMETER_MAX_VALUE;
  }
}

// Helper function for the "Shift and Scale" mapping.
// This function takes a sequencer value (0.0-1.0) and an encoder offset
// (a bipolar value, e.g., -0.6 to 0.6) and combines them intelligently.
float shiftAndScale(float seqValue, float encoderOffset)
{
  float finalValue;
  if (encoderOffset >= 0.0f)
  {
    // When the encoder offset is positive, it sets the minimum value,
    // and the sequencer value is scaled to fit the remaining range up to 1.0.
    finalValue = encoderOffset + (seqValue * (1.0f - encoderOffset));
  }
  else
  {
    // When the encoder offset is negative, it reduces the maximum value,
    // and the sequencer value is scaled to fit the range from 0.0 up to that new maximum.
    finalValue = seqValue * (1.0f + encoderOffset);
  }
  // Clamp the result to ensure it remains within the valid [0.0, 1.0] range.
  return std::max(0.0f, std::min(finalValue, 1.0f));
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
  magEncoder.clearPendingTicks();
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
  magEncoder.clearPendingTicks();
}
