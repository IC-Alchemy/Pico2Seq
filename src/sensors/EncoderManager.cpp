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
#include <uClock.h>
#include "../voice/VoiceManager.h"
#include "../voice/VoiceSystem.h" // VoiceSystem::MAX_VOICES

// TMAG5273 jog knob state: per-target motion so a new voice/step/lane starts at zero.

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
ControlSurface::EncoderMotion arpTempoMotion;
bool arpDialShift = false;
bool arpDialActive = false;
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
  // An absolute lane that follows the patch starts from the value it plays.
  const float curVal = isPatchDefaultLane(targetParam)
                           ? selectedSeq->getPlaybackValue(targetParam, step)
                           : selectedSeq->getStepParameterValue(targetParam, step);
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
  // Like the lidar and faders, pitch is never written into a gate-off step.
  if (newVal != curVal && selectedSeq->editStepValue(targetParam, step, newVal))
  {
    updateActiveVoiceState(step, *selectedSeq);
  }
  return true;
}
} // namespace

// The magnetic encoder driver for the TMAG5273A Velocity Encoder board.
MagEncoder magEncoder(makeMagEncoderConfig());

void updateEncoderBaseValues(UIState &uiState)
{
  if (!magEncoder.isConnected() || uiState.controlsWaitRelease) return;
  // Every read's increment is forwarded, however small: the driver has
  // already drained those ticks, and the step and base paths accumulate them.
  const float delta=magEncoder.takeParameterIncrement(-1.0f,1.0f,3);
  const bool arpOwnsDial = uiState.arp.active() && !uiState.voiceEditor.active;
  if (arpOwnsDial != arpDialActive || uiState.shiftHeld != arpDialShift) {
    arpTempoMotion.reset();
    uiState.arp.resetRateMotion();
    arpDialActive = arpOwnsDial;
    arpDialShift = uiState.shiftHeld;
  }
  if(delta==0.0f) return;
  // Arpeggiator mode: the dial is the arp's rate. It is the one arp control
  // that wants absolute, stepped access, and the four faders are already
  // carrying range, gate, swing and tone.
  if(arpOwnsDial) {
    if (uiState.shiftHeld) {
      arpTempoMotion.add(delta);
      const int steps = arpTempoMotion.takeSteps(SensorConstants::MagneticEncoder::STEPPED_VALUE_DETENT);
      if (steps) {
        uClock.setTempo(std::clamp(uClock.getTempo() + static_cast<float>(steps), 45.0f, 200.0f));
        uiState.showArpControl(UIState::ArpControl::Tempo, millis());
      }
    } else {
      const auto previous = uiState.arp.settings().rate;
      uiState.arp.turnRate(delta, SensorConstants::MagneticEncoder::STEPPED_VALUE_DETENT);
      if (previous != uiState.arp.settings().rate)
        uiState.showArpControl(UIState::ArpControl::Rate, millis());
    }
    return;
  }
  if(!uiState.voiceEditor.active && editSelectedStep(uiState, delta)) return;
  VoiceEditor::encoder(delta);
}

// Lane-inverse mapping lives in ControlSurface::stepEditParameter (single source).
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
void initEncoderBaseValues()
{
  // VoiceSetup initializes each patch's bases from its preset. The old
  // encoderBaseValues array was removed with that ownership change.
  VoiceEditor::clearEncoder();
}
