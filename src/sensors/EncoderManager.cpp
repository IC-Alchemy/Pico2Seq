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

// Motion for the selected step, separate from base editing's motion in
// VoiceEditor. A different voice, step or parameter starts from zero.
ControlSurface::EncoderMotion stepMotion;
struct StepTurn
{
  uint8_t voice = UINT8_MAX;
  int step = -1;
  ParamId param = ParamId::Count;
};
StepTurn stepTurn;

// Stored-lane distance of one octave: mapOctave() quantizes the normalized
// lane in quarters, and the legacy thresholds sit at thirds.
constexpr float kOctaveLaneStep = 0.25f;

// A selected step takes the encoder: the held, toggled or encoder-target
// parameter (the same one the OLED shows). Returns false when no step
// parameter is targeted, leaving the turn to base editing.
bool editSelectedStep(UIState &uiState, float delta)
{
  if (uiState.selectedStepForEdit < 0 || uiState.selectedVoiceIndex >= VoiceSystem::MAX_VOICES)
    return false;
  const ParamId targetParam = ControlSurface::stepEditParameter(
      getHeldParameterParamId(uiState), uiState.currentEditParameter, uiState.currentEncoderParameter);
  Sequencer *selectedSeq = AppState::sequencers[uiState.selectedVoiceIndex];
  if (targetParam == ParamId::Count || !selectedSeq)
    return false;

  if (stepTurn.voice != uiState.selectedVoiceIndex || stepTurn.step != uiState.selectedStepForEdit ||
      stepTurn.param != targetParam)
  {
    stepMotion.reset();
    stepTurn = {uiState.selectedVoiceIndex, uiState.selectedStepForEdit, targetParam};
  }
  stepMotion.add(delta);

  const uint8_t step = static_cast<uint8_t>(uiState.selectedStepForEdit);
  const float curVal = selectedSeq->getStepParameterValue(targetParam, step);
  const float minVal = getParameterMinValueForParamId(targetParam);
  const float maxVal = getParameterMaxValueForParamId(targetParam);
  float newVal;
  if (targetParam == ParamId::Note || targetParam == ParamId::Octave)
  {
    // Whole scale steps (or octaves) per detent: rounding each small
    // increment left the value unchanged unless the knob was spun hard.
    const int steps = stepMotion.takeSteps(SensorConstants::MagneticEncoder::STEPPED_VALUE_DETENT);
    newVal = curVal + static_cast<float>(steps) * (targetParam == ParamId::Octave ? kOctaveLaneStep : 1.0f);
  }
  else
  {
    // Same sensitivity as base editing. The former extra 5% scale moved a
    // step well under 1% per slow revolution, too little to hear.
    newVal = curVal + stepMotion.takeContinuous(
        SensorConstants::MagneticEncoder::MINIMUM_INCREMENT_THRESHOLD) * (maxVal - minVal);
  }
  newVal = std::clamp(newVal, minVal, maxVal);
  if (newVal != curVal)
  {
    selectedSeq->setStepParameterValue(targetParam, step, newVal);
    updateActiveVoiceState(step, *selectedSeq);
  }
  return true;
}
} // namespace

// The magnetic encoder driver for the TMAG5273A Velocity Encoder board.
MagEncoder magEncoder(makeMagEncoderConfig());

// Note: currentEncoderParameter is accessed via uiState.currentEncoderParameter

void updateEncoderBaseValues(UIState &uiState)
{
  if (!magEncoder.isConnected() || uiState.controlsWaitRelease) return;
  // Every read's increment is forwarded, however small: the driver has
  // already drained those ticks, and the step and base paths accumulate them.
  const float delta=magEncoder.takeParameterIncrement(-1.0f,1.0f,3);
  if(delta==0.0f) return;
  if(!uiState.voiceEditor.active && editSelectedStep(uiState, delta)) return;
  VoiceEditor::encoder(delta);
}

// --- Helper Functions for Step Parameter Editing ---

// Note: the former encoder-to-parameter inverse switch was removed;
// ControlSurface::stepEditParameter() resolves encoder lanes from
// CORE_PARAMETERS instead, so there is a single mapping to maintain.

float getParameterMinValueForParamId(ParamId paramId)
{
  if (static_cast<size_t>(paramId) < static_cast<size_t>(ParamId::Count))
  {
    return parameterValueAsFloat(CORE_PARAMETERS[static_cast<size_t>(paramId)].minValue);
  }
  return SensorConstants::MagneticEncoder::PARAMETER_MIN_VALUE;
}

float getParameterMaxValueForParamId(ParamId paramId)
{
  if (static_cast<size_t>(paramId) < static_cast<size_t>(ParamId::Count))
  {
    return parameterValueAsFloat(CORE_PARAMETERS[static_cast<size_t>(paramId)].maxValue);
  }
  return SensorConstants::MagneticEncoder::PARAMETER_MAX_VALUE;
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
