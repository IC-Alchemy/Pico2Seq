#include "EncoderManager.h"
#include <Arduino.h>
#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../ui/UIState.h"
#include <algorithm>
#include <cmath>
#include "../voice/VoiceManager.h"

// =======================
//   EXTERNAL REFERENCES
// =======================

// Reference to the global UIState from main file
extern UIState uiState;
// Reference to the global VoiceManager
extern std::unique_ptr<VoiceManager> voiceManager;

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

// Definitions for encoder base value globals (previously declared as extern).
// Centralize the definitions here so other translation units can reference them
// via extern declarations if necessary (but header externs have been removed).
EncoderBaseValuesVoice1 encoderBaseValuesVoice1;
EncoderBaseValues encoderBaseValuesVoice2;

namespace
{
bool isBipolarVoiceBaseParameter(EncoderParameterMode param)
{
  switch (param)
  {
  case EncoderParameterMode::Note:
  case EncoderParameterMode::Velocity:
  case EncoderParameterMode::Filter:
  case EncoderParameterMode::Attack:
  case EncoderParameterMode::Decay:
  case EncoderParameterMode::Octave:
    return true;
  default:
    return false;
  }
}
} // namespace

// Flash speed zones configuration for dynamic boundary proximity feedback
const FlashSpeedConfig FLASH_SPEED_ZONES[] = {
    {SensorConstants::MagneticEncoder::NORMAL_FLASH_SPEED,
     SensorConstants::MagneticEncoder::NORMAL_ZONE_START,
     SensorConstants::MagneticEncoder::NORMAL_ZONE_END},
    {SensorConstants::MagneticEncoder::WARNING_FLASH_SPEED,
     SensorConstants::MagneticEncoder::WARNING_ZONE_START,
     SensorConstants::MagneticEncoder::WARNING_ZONE_END},
    {SensorConstants::MagneticEncoder::CRITICAL_FLASH_SPEED,
     SensorConstants::MagneticEncoder::CRITICAL_ZONE_START,
     SensorConstants::MagneticEncoder::CRITICAL_ZONE_END}};

// =======================
//   ENCODER PARAMETER BOUNDS MANAGEMENT
// =======================

float getParameterMinValue(EncoderParameterMode param)
{
  // Return the minimum valid value for each parameter type
  switch (param)
  {
  case EncoderParameterMode::Note:
  case EncoderParameterMode::Velocity:
  case EncoderParameterMode::Filter:
  case EncoderParameterMode::Attack:
  case EncoderParameterMode::Decay:
  case EncoderParameterMode::Octave:
    return SensorConstants::MagneticEncoder::PARAMETER_MIN_VALUE;

  case EncoderParameterMode::DelayTime:
    return SensorConstants::MagneticEncoder::DELAY_TIME_MIN_SAMPLES; // 2.5ms minimum delay at 48kHz

  case EncoderParameterMode::DelayFeedback:
    return SensorConstants::MagneticEncoder::PARAMETER_MIN_VALUE;

  case EncoderParameterMode::SlideTime:
    return SensorConstants::MagneticEncoder::PARAMETER_MIN_VALUE; // Minimum slide time (instant)

  default:
    return SensorConstants::MagneticEncoder::PARAMETER_MIN_VALUE;
  }
}

float getParameterMaxValue(EncoderParameterMode param)
{
  // Return the maximum valid value for each parameter type
  switch (param)
  {
  case EncoderParameterMode::Note:
  case EncoderParameterMode::Velocity:
  case EncoderParameterMode::Filter:
  case EncoderParameterMode::Attack:
  case EncoderParameterMode::Decay:
  case EncoderParameterMode::Octave:
    return SensorConstants::MagneticEncoder::PARAMETER_MAX_VALUE;

  case EncoderParameterMode::DelayTime:
    return MAX_DELAY_SAMPLES * 0.85f; // 85% of the 1.8s delay line (~1.53s at 48kHz)

  case EncoderParameterMode::DelayFeedback:
    return SensorConstants::MagneticEncoder::DELAY_FEEDBACK_MAX; // Maximum 91% feedback to prevent excessive feedback

  case EncoderParameterMode::SlideTime:
    return SensorConstants::MagneticEncoder::PARAMETER_MAX_VALUE; // Maximum slide time

  default:
    return SensorConstants::MagneticEncoder::PARAMETER_MAX_VALUE;
  }
}

float getEncoderBaseValueRange(EncoderParameterMode param)
{
  // Calculate the full parameter range
  float fullParameterRange = getParameterMaxValue(param) - getParameterMinValue(param);

  // Voice bases are normalized bipolar offsets. Delay and slide time retain
  // direct, unipolar controls over their physical ranges.
  if (!isBipolarVoiceBaseParameter(param))
  {
    return fullParameterRange; // Full range for delay parameters
  }

  // Voice parameters use reduced range to leave room for sequencer values
  return fullParameterRange * SensorConstants::MagneticEncoder::PARAMETER_RANGE_SCALE_FACTOR;
}

float clampEncoderBaseValue(EncoderParameterMode param, float value)
{
  // Clamp encoder base values to their allowed bidirectional range
  float maxAllowedRange = getEncoderBaseValueRange(param);
  return std::max(-maxAllowedRange, std::min(value, maxAllowedRange));
}
void updateEncoderBaseValues(UIState &uiState)
{
  if (!magEncoder.isConnected())
  {
    return;
  }

  // Check if we're in edit mode for a specific step
  if (uiState.selectedStepForEdit >= 0)
  {
    updateEncoderStepParameterValues(uiState);
    return;
  }

  // Get current encoder base values for the active voice
  EncoderBaseValues *activeVoiceBaseValues = uiState.isVoice2Mode
                                                ? (EncoderBaseValues *)&encoderBaseValuesVoice2
                                                : (EncoderBaseValues *)&encoderBaseValuesVoice1;

  // Calculate bidirectional velocity-sensitive parameter increment.
  // takeParameterIncrement drains the pending-tick accumulator filled once per
  // sensor read; the const getParameterIncrement() would re-report the same
  // delta on every ~1ms call between 5ms sensor reads (applied ~5x).
  float parameterMinValue = getParameterMinValue(uiState.currentEncoderParameter);
  float parameterMaxValue = getParameterMaxValue(uiState.currentEncoderParameter);
  float parameterIncrement = magEncoder.takeParameterIncrement(
      parameterMinValue - parameterMaxValue,
      parameterMaxValue - parameterMinValue,
      3);

  // Ignore tiny increments to prevent sensor noise from affecting parameters
  if (fabsf(parameterIncrement) < SensorConstants::MagneticEncoder::MINIMUM_INCREMENT_THRESHOLD)
  {
    return;
  }

  // Apply increment to the appropriate parameter with boundary checking
  applyIncrementToParameter(activeVoiceBaseValues, uiState.currentEncoderParameter, parameterIncrement);
}

void updateEncoderStepParameterValues(UIState &uiState)
{
  if (!magEncoder.isConnected() ||
      uiState.selectedStepForEdit < 0 ||
      uiState.currentEditParameter == ParamId::Count)
  {
    return;
  }

  // Get the active sequencer based on selected voice (0-3 maps to seq1-seq4)
  extern Sequencer seq1, seq2, seq3, seq4;
  Sequencer &activeSequencer = (uiState.selectedVoiceIndex == 0) ? seq1 : (uiState.selectedVoiceIndex == 1) ? seq2
                                                                      : (uiState.selectedVoiceIndex == 2)   ? seq3
                                                                                                            : seq4;

  // Use the currently selected edit parameter
  ParamId targetParameterId = uiState.currentEditParameter;
  if (targetParameterId == ParamId::Count)
  {
    return; // No parameter selected for editing
  }

  // Get parameter range for the target parameter
  float parameterMinValue = getParameterMinValueForParamId(targetParameterId);
  float parameterMaxValue = getParameterMaxValueForParamId(targetParameterId);

  // Get velocity-sensitive increment with full range scaling
  // (consuming getter: drains the per-read tick accumulator exactly once)
  float parameterIncrement = magEncoder.takeParameterIncrement(
      parameterMinValue - parameterMaxValue,
      parameterMaxValue - parameterMinValue,
      3);

  // Ignore tiny increments to prevent sensor noise
  if (fabsf(parameterIncrement) < SensorConstants::MagneticEncoder::MINIMUM_INCREMENT_THRESHOLD)
  {
    return;
  }

  // Get current parameter value for the selected step
  uint8_t editStepIndex = static_cast<uint8_t>(uiState.selectedStepForEdit);
  float currentParameterValue = activeSequencer.getStepParameterValue(targetParameterId, editStepIndex);

  // Apply increment with boundary checking
  float newParameterValue = currentParameterValue + parameterIncrement;
  newParameterValue = std::max(parameterMinValue, std::min(newParameterValue, parameterMaxValue));

  // Set the new parameter value
  activeSequencer.setStepParameterValue(targetParameterId, editStepIndex, newParameterValue);

  // Trigger immediate OLED update by updating the active voice state
  extern void updateActiveVoiceState(uint8_t stepIndex, Sequencer &activeSeq);
  updateActiveVoiceState(editStepIndex, activeSequencer);

  /*
  // Debug output for parameter changes
  Serial.print("Encoder Edit Mode - Step ");
  Serial.print(editStepIndex);
  Serial.print(", Parameter: ");
  Serial.print(CORE_PARAMETERS[static_cast<int>(targetParameterId)].name);
  Serial.print(", Value: ");
  Serial.print(newParameterValue, 3);
  Serial.print(" (");
  Serial.print(formatParameterValueForDisplay(targetParameterId, newParameterValue));
  Serial.println(")");
  */
}

void applyIncrementToParameter(EncoderBaseValues *baseValues, EncoderParameterMode param, float increment)
{
  float *targetParameterValue = nullptr;

  // Select the appropriate parameter to modify
  switch (param)
  {
  case EncoderParameterMode::Note:
    targetParameterValue = &baseValues->note;
    break;
  case EncoderParameterMode::Velocity:
    targetParameterValue = &baseValues->velocity;
    break;
  case EncoderParameterMode::Filter:
    targetParameterValue = &baseValues->filter;
    break;
  case EncoderParameterMode::Attack:
    targetParameterValue = &baseValues->attack;
    break;
  case EncoderParameterMode::Decay:
    targetParameterValue = &baseValues->decay;
    break;
  case EncoderParameterMode::Octave:
    targetParameterValue = &baseValues->octave;
    break;
  case EncoderParameterMode::DelayTime:
    targetParameterValue = &baseValues->delayTime;
    break;
  case EncoderParameterMode::DelayFeedback:
    targetParameterValue = &baseValues->delayFeedback;
    break;
  case EncoderParameterMode::SlideTime:
    targetParameterValue = &baseValues->slideTime;
    break;
  default:
    return; // Invalid parameter type
  }

  if (!targetParameterValue)
  {
    return; // Safety check for null pointer
  }

  // Calculate new value with increment applied
  float newParameterValue = *targetParameterValue + increment;

  // Apply appropriate clamping based on parameter type
  if (isBipolarVoiceBaseParameter(param))
  {
    // Bidirectional parameters (voice parameters) use symmetric range
    float maxAllowedRange = getEncoderBaseValueRange(param);
    *targetParameterValue = std::max(-maxAllowedRange, std::min(newParameterValue, maxAllowedRange));
  }
  else
  {
    // Unidirectional parameters (delay and slide time) use min/max bounds
    float parameterMinValue = getParameterMinValue(param);
    float parameterMaxValue = getParameterMaxValue(param);
    *targetParameterValue = std::max(parameterMinValue, std::min(newParameterValue, parameterMaxValue));
  }

  // Debug output for delay parameter changes (uncomment for debugging)
  /*
  if (param == EncoderParameterMode::DelayTime || param == EncoderParameterMode::DelayFeedback) {
    Serial.print("Encoder ");
    Serial.print(param == EncoderParameterMode::DelayTime ? "DelayTime" : "DelayFeedback");
    Serial.print(" changed from ");
    Serial.print(previousValue, 3);
    Serial.print(" to ");
    Serial.print(*targetParameterValue, 3);
    Serial.print(" (increment: ");
    Serial.print(increment, 3);
    Serial.println(")");
  }
  */
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
    return SensorConstants::MagneticEncoder::PARAMETER_MIN_VALUE; // Scale array indices (0-21)

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
    return SensorConstants::MagneticEncoder::NOTE_PARAMETER_MAX; // Scale array indices (0-21)

  default:
    return SensorConstants::MagneticEncoder::PARAMETER_MAX_VALUE;
  }
}

String formatParameterValueForDisplay(ParamId paramId, float value)
{
  switch (paramId)
  {
  case ParamId::Note:
    return String((int)value);

  case ParamId::Velocity:
    return String((int)(value * 100)) + "%";

  case ParamId::Filter:
  {
    int filterFrequencyHz = dspmap::fmap(
        value,
        SensorConstants::System::FILTER_FREQUENCY_MIN_HZ,
        SensorConstants::System::FILTER_FREQUENCY_MAX_HZ,
        dspmap::Mapping::EXP);
    return String(filterFrequencyHz) + "Hz";
  }

  case ParamId::Attack:
  case ParamId::Decay:
    return String(value, 3) + "s";

  default:
    return String(value, 2);
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

/**
 * Apply magnetic encoder base values to voice parameters.
 * Implements a "Shift and Scale" mapping to combine encoder and sequencer values.
 * This avoids "dead zones" by scaling the sequencer's output within the range
 * defined by the encoder's offset.
 * */
void applyEncoderBaseValues(VoiceState *voiceState, uint8_t voiceId)
{
  if (!magEncoder.isConnected() || !voiceState)
  {
    return;
  }

  // Select the correct base values based on voice ID (0 = voice1, 1 = voice2)
  const EncoderBaseValues *baseValues = (voiceId == 1) ? (const EncoderBaseValues *)&encoderBaseValuesVoice2 : (const EncoderBaseValues *)&encoderBaseValuesVoice1;

  // Apply "Shift and Scale" for each parameter.
  // This maps the sequencer value into the dynamic range set by the encoder offset.
  const float normalizedNote = voiceState->noteIndex /
                               SensorConstants::MagneticEncoder::NOTE_PARAMETER_MAX;
  voiceState->noteIndex = shiftAndScale(normalizedNote, baseValues->note) *
                          SensorConstants::MagneticEncoder::NOTE_PARAMETER_MAX;
  voiceState->velocityLevel = shiftAndScale(voiceState->velocityLevel, baseValues->velocity);
  voiceState->filterCutoff = shiftAndScale(voiceState->filterCutoff, baseValues->filter);
  voiceState->attackTimeSeconds = shiftAndScale(voiceState->attackTimeSeconds, baseValues->attack);
  voiceState->decayTimeSeconds = shiftAndScale(voiceState->decayTimeSeconds, baseValues->decay);
  voiceState->octaveOffset = shiftAndScale(voiceState->octaveOffset, baseValues->octave);
}

/**
 * Apply magnetic encoder values to global delay effect parameters.
 * Direct parameter control: delay parameters use full range without restrictions.
 * Thread-safe communication for Core0 audio processing.
 */
void applyEncoderDelayValues()
{
  if (!magEncoder.isConnected())
  {
    return;
  }

  // Use Voice 1 base values for global delay parameters (delay is not per-voice)
  const EncoderBaseValuesVoice1 *baseValues = &encoderBaseValuesVoice1;

  // Apply delay time directly (already clamped to 2.5ms-1.53s range in updateEncoderBaseValues)
  delayTarget = baseValues->delayTime;

  // Apply delay feedback directly (already clamped to 0.0-0.91 range in updateEncoderBaseValues)
  feedbackAmmount = baseValues->delayFeedback;
}

// ----------------------
// Apply slide time values from the magnetic encoder to the active voice
// ----------------------
void applyEncoderSlideTimeValues()
{
  if (!magEncoder.isConnected() || !uiState.slideMode)
  {
    return;
  }

  // Determine which base values are active
  EncoderBaseValues *activeBaseValues = uiState.isVoice2Mode
                                           ? (EncoderBaseValues *)&encoderBaseValuesVoice2
                                           : (EncoderBaseValues *)&encoderBaseValuesVoice1;

  // Read encoder increment for SlideTime (unipolar 0.0 - 1.0 seconds)
  float minVal = getParameterMinValue(EncoderParameterMode::SlideTime);
  float maxVal = getParameterMaxValue(EncoderParameterMode::SlideTime);
  float increment = magEncoder.takeParameterIncrement(minVal - maxVal, maxVal - minVal, 3);

  // Apply increment if above noise threshold
  if (fabsf(increment) >= SensorConstants::MagneticEncoder::MINIMUM_INCREMENT_THRESHOLD)
  {
    applyIncrementToParameter(activeBaseValues, EncoderParameterMode::SlideTime, increment);
  }

  // Map and apply to the currently selected voice via VoiceManager
  if (voiceManager)
  {
    uint8_t voiceId = static_cast<uint8_t>(uiState.selectedVoiceIndex);
    float slideSeconds = activeBaseValues->slideTime; // already clamped 0.0 - 1.0
    voiceManager->setVoiceSlide(voiceId, slideSeconds);
  }
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
  if (!magEncoder.isConnected())
  {
    return 0.0f;
  }

  const EncoderBaseValues *activeBaseValues = uiState.isVoice2Mode ? &encoderBaseValuesVoice2 : &encoderBaseValuesVoice1;
  float value = 0.0f;

  // Retrieve the raw value for the current parameter
  switch (uiState.currentEncoderParameter)
  {
  case EncoderParameterMode::Note:
    value = activeBaseValues->note;
    break;
  case EncoderParameterMode::Velocity:
    value = activeBaseValues->velocity;
    break;
  case EncoderParameterMode::Filter:
    value = activeBaseValues->filter;
    break;
  case EncoderParameterMode::Attack:
    value = activeBaseValues->attack;
    break;
  case EncoderParameterMode::Decay:
    value = activeBaseValues->decay;
    break;
  case EncoderParameterMode::Octave:
    value = activeBaseValues->octave;
    break;
  case EncoderParameterMode::DelayTime:
    value = activeBaseValues->delayTime;
    break;
  case EncoderParameterMode::DelayFeedback:
    value = activeBaseValues->delayFeedback;
    break;
  case EncoderParameterMode::SlideTime:
    value = activeBaseValues->slideTime;
    break;
  }

  // Normalize the value to a 0.0-1.0 range for LED feedback
  float minVal = getParameterMinValue(uiState.currentEncoderParameter);
  float maxVal = getParameterMaxValue(uiState.currentEncoderParameter);
  float normalizedValue = (value - minVal) / (maxVal - minVal);

  // For bipolar parameters (like velocity, filter, etc.), we need to handle the normalization differently.
  // Since they range from -maxRange to +maxRange, we can map this to 0.0-1.0.
  if (isBipolarVoiceBaseParameter(uiState.currentEncoderParameter))
  {
    float maxRange = getEncoderBaseValueRange(uiState.currentEncoderParameter);
    normalizedValue = (value + maxRange) / (2 * maxRange);
  }

  return std::max(0.0f, std::min(normalizedValue, 1.0f)); // Clamp to ensure valid range
}

void initEncoderBaseValues()
{
  // Initialize voice parameters to neutral position for both voices
  encoderBaseValuesVoice1.note = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  encoderBaseValuesVoice1.velocity = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  encoderBaseValuesVoice1.filter = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  encoderBaseValuesVoice1.attack = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  encoderBaseValuesVoice1.decay = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  encoderBaseValuesVoice1.octave = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;

  encoderBaseValuesVoice2.note = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  encoderBaseValuesVoice2.velocity = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  encoderBaseValuesVoice2.filter = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  encoderBaseValuesVoice2.attack = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  encoderBaseValuesVoice2.decay = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  encoderBaseValuesVoice2.octave = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;

  // Initialize delay parameters with reasonable defaults for both voices
  encoderBaseValuesVoice1.delayTime = SensorConstants::MagneticEncoder::DEFAULT_DELAY_TIME_SAMPLES;
  encoderBaseValuesVoice1.delayFeedback = SensorConstants::MagneticEncoder::DEFAULT_DELAY_FEEDBACK;
  encoderBaseValuesVoice2.delayTime = SensorConstants::MagneticEncoder::DEFAULT_DELAY_TIME_SAMPLES;
  encoderBaseValuesVoice2.delayFeedback = SensorConstants::MagneticEncoder::DEFAULT_DELAY_FEEDBACK;
}

void resetEncoderBaseValues(UIState &uiState, bool currentVoiceOnly)
{
  if (currentVoiceOnly)
  {
    // Reset only the currently active voice to neutral position
    EncoderBaseValues *activeVoiceBaseValues = uiState.isVoice2Mode
                                                  ? (EncoderBaseValues *)&encoderBaseValuesVoice2
                                                  : (EncoderBaseValues *)&encoderBaseValuesVoice1;

    // Reset voice parameters to neutral position
    activeVoiceBaseValues->note = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
    activeVoiceBaseValues->velocity = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
    activeVoiceBaseValues->filter = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
    activeVoiceBaseValues->attack = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
    activeVoiceBaseValues->decay = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
    activeVoiceBaseValues->octave = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;

    // Note: Delay parameters are global and not reset with this function
  }
  else
  {
    // Reset all voices - call the full initialization
    initEncoderBaseValues();
  }
}
