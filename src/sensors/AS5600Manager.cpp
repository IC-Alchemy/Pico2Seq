#include "AS5600Manager.h"
#include <Arduino.h>
#include "../sequencer/SequencerDefs.h"
#include "../sequencer/Sequencer.h"
#include "../ui/UIState.h"
#include "as5600.h"
#include <algorithm>
#include <cmath>
#include "../dsp/oscillator.h"
#include "../voice/VoiceManager.h"

// =======================
//   EXTERNAL REFERENCES
// =======================

// Reference to the global UIState from main file
extern UIState uiState;
// Reference to the global VoiceManager
extern std::unique_ptr<VoiceManager> voiceManager;

// =======================
//   AS5600 GLOBAL VARIABLES
// =======================

// AS5600 global variables moved from main file
// Note: currentAS5600Parameter is now accessed via uiState.currentAS5600Parameter

unsigned long lastAS5600ButtonPress = 0;

// Definitions for AS5600 base value globals (previously declared as extern).
// Centralize the definitions here so other translation units can reference them
// via extern declarations if necessary (but header externs have been removed).
AS5600BaseValuesVoice1 as5600BaseValuesVoice1;
AS5600BaseValues as5600BaseValuesVoice2;

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
//   AS5600 PARAMETER BOUNDS MANAGEMENT
// =======================

float getParameterMinValue(AS5600ParameterMode param)
{
  // Return the minimum valid value for each parameter type
  switch (param)
  {
  case AS5600ParameterMode::Velocity:
  case AS5600ParameterMode::Filter:
  case AS5600ParameterMode::Attack:
  case AS5600ParameterMode::Decay:
    return SensorConstants::MagneticEncoder::PARAMETER_MIN_VALUE;

  case AS5600ParameterMode::Note:
    return SensorConstants::MagneticEncoder::PARAMETER_MIN_VALUE; // Scale array indices (0-21)

  case AS5600ParameterMode::DelayTime:
    return SensorConstants::MagneticEncoder::DELAY_TIME_MIN_SAMPLES; // 10ms minimum delay at 48kHz

  case AS5600ParameterMode::DelayFeedback:
    return SensorConstants::MagneticEncoder::PARAMETER_MIN_VALUE;

  case AS5600ParameterMode::SlideTime:
    return SensorConstants::MagneticEncoder::PARAMETER_MIN_VALUE; // Minimum slide time (instant)

  default:
    return SensorConstants::MagneticEncoder::PARAMETER_MIN_VALUE;
  }
}

float getParameterMaxValue(AS5600ParameterMode param)
{
  // Return the maximum valid value for each parameter type
  switch (param)
  {
  case AS5600ParameterMode::Velocity:
  case AS5600ParameterMode::Filter:
  case AS5600ParameterMode::Attack:
  case AS5600ParameterMode::Decay:
    return SensorConstants::MagneticEncoder::PARAMETER_MAX_VALUE;

  case AS5600ParameterMode::Note:
    return SensorConstants::MagneticEncoder::NOTE_PARAMETER_MAX; // Scale array indices (0-21)

  case AS5600ParameterMode::DelayTime:
    return MAX_DELAY_SAMPLES * 0.85f; // Maximum delay in samples (1.8 seconds)

  case AS5600ParameterMode::DelayFeedback:
    return SensorConstants::MagneticEncoder::DELAY_FEEDBACK_MAX; // Maximum 91% feedback to prevent excessive feedback

  case AS5600ParameterMode::SlideTime:
    return SensorConstants::MagneticEncoder::PARAMETER_MAX_VALUE; // Maximum slide time

  default:
    return SensorConstants::MagneticEncoder::PARAMETER_MAX_VALUE;
  }
}

float getAS5600BaseValueRange(AS5600ParameterMode param)
{
  // Calculate the full parameter range
  float fullParameterRange = getParameterMaxValue(param) - getParameterMinValue(param);

  // Delay parameters use full range without restrictions
  if (param == AS5600ParameterMode::DelayTime || param == AS5600ParameterMode::DelayFeedback || param == AS5600ParameterMode::SlideTime)
  {
    return fullParameterRange; // Full range for delay parameters
  }

  // Voice parameters use reduced range to leave room for sequencer values
  return fullParameterRange * SensorConstants::MagneticEncoder::PARAMETER_RANGE_SCALE_FACTOR;
}

float clampAS5600BaseValue(AS5600ParameterMode param, float value)
{
  // Clamp AS5600 base values to their allowed bidirectional range
  float maxAllowedRange = getAS5600BaseValueRange(param);
  return std::max(-maxAllowedRange, std::min(value, maxAllowedRange));
}
void updateAS5600BaseValues(UIState &uiState)
{
  if (!as5600Sensor.isConnected())
  {
    return;
  }

  // Check if we're in edit mode for a specific step
  if (uiState.selectedStepForEdit >= 0)
  {
    updateAS5600StepParameterValues(uiState);
    return;
  }

  // Get current AS5600 base values for the active voice
  AS5600BaseValues *activeVoiceBaseValues = uiState.isVoice2Mode
                                                ? (AS5600BaseValues *)&as5600BaseValuesVoice2
                                                : (AS5600BaseValues *)&as5600BaseValuesVoice1;

  // Calculate bidirectional velocity-sensitive parameter increment
  float parameterMinValue = getParameterMinValue(uiState.currentAS5600Parameter);
  float parameterMaxValue = getParameterMaxValue(uiState.currentAS5600Parameter);
  float parameterIncrement = as5600Sensor.getParameterIncrement(
      parameterMinValue - parameterMaxValue,
      parameterMaxValue - parameterMinValue,
      3);

  // Ignore tiny increments to prevent sensor noise from affecting parameters
  if (fabsf(parameterIncrement) < SensorConstants::MagneticEncoder::MINIMUM_INCREMENT_THRESHOLD)
  {
    return;
  }

  // Apply increment to the appropriate parameter with boundary checking
  applyIncrementToParameter(activeVoiceBaseValues, uiState.currentAS5600Parameter, parameterIncrement);
}

void updateAS5600StepParameterValues(UIState &uiState)
{
  if (!as5600Sensor.isConnected() ||
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
  float parameterIncrement = as5600Sensor.getParameterIncrement(
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
  Serial.print("AS5600 Edit Mode - Step ");
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

void applyIncrementToParameter(AS5600BaseValues *baseValues, AS5600ParameterMode param, float increment)
{
  float *targetParameterValue = nullptr;

  // Select the appropriate parameter to modify
  switch (param)
  {
  case AS5600ParameterMode::Velocity:
    targetParameterValue = &baseValues->velocity;
    break;
  case AS5600ParameterMode::Filter:
    targetParameterValue = &baseValues->filter;
    break;
  case AS5600ParameterMode::Attack:
    targetParameterValue = &baseValues->attack;
    break;
  case AS5600ParameterMode::Decay:
    targetParameterValue = &baseValues->decay;
    break;
  case AS5600ParameterMode::DelayTime:
    targetParameterValue = &baseValues->delayTime;
    break;
  case AS5600ParameterMode::DelayFeedback:
    targetParameterValue = &baseValues->delayFeedback;
    break;
  case AS5600ParameterMode::SlideTime:
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
  float previousValue = *targetParameterValue;

  // Apply appropriate clamping based on parameter type
  if (param <= AS5600ParameterMode::Decay)
  {
    // Bidirectional parameters (voice parameters) use symmetric range
    float maxAllowedRange = getAS5600BaseValueRange(param);
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
  if (param == AS5600ParameterMode::DelayTime || param == AS5600ParameterMode::DelayFeedback) {
    Serial.print("AS5600 ");
    Serial.print(param == AS5600ParameterMode::DelayTime ? "DelayTime" : "DelayFeedback");
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

// Convert AS5600ParameterMode to ParamId for step editing
ParamId convertAS5600ParameterToParamId(AS5600ParameterMode as5600Param)
{
  switch (as5600Param)
  {
  case AS5600ParameterMode::Velocity:
    return ParamId::Velocity;
  case AS5600ParameterMode::Filter:
    return ParamId::Filter;
  case AS5600ParameterMode::Attack:
    return ParamId::Attack;
  case AS5600ParameterMode::Decay:
    return ParamId::Decay;
  case AS5600ParameterMode::Note:
    return ParamId::Note;
  case AS5600ParameterMode::SlideTime:
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
    int filterFrequencyHz = daisysp::fmap(
        value,
        SensorConstants::System::FILTER_FREQUENCY_MIN_HZ,
        SensorConstants::System::FILTER_FREQUENCY_MAX_HZ,
        daisysp::Mapping::EXP);
    return String(filterFrequencyHz) + "Hz";
  }

  case ParamId::Attack:
  case ParamId::Decay:
    return String(value, 3) + "s";

  default:
    return String(value, 2);
  }
}

// ======================
// Boundary Proximity and Flash Speed
// ======================

float calculateAS5600BoundaryProximity(AS5600ParameterMode param)
{
  const AS5600BaseValues *activeBaseValues = uiState.isVoice2Mode
                                                 ? (const AS5600BaseValues *)&as5600BaseValuesVoice2
                                                 : (const AS5600BaseValues *)&as5600BaseValuesVoice1;

  float value = 0.0f;
  switch (param)
  {
  case AS5600ParameterMode::Velocity:
    value = activeBaseValues->velocity;
    break;
  case AS5600ParameterMode::Filter:
    value = activeBaseValues->filter;
    break;
  case AS5600ParameterMode::Attack:
    value = activeBaseValues->attack;
    break;
  case AS5600ParameterMode::Decay:
    value = activeBaseValues->decay;
    break;
  case AS5600ParameterMode::DelayTime:
    value = activeBaseValues->delayTime;
    break;
  case AS5600ParameterMode::DelayFeedback:
    value = activeBaseValues->delayFeedback;
    break;
  case AS5600ParameterMode::SlideTime:
    value = activeBaseValues->slideTime;
    break;
  case AS5600ParameterMode::Note:
    value = 0.0f;
    break; // Not stored in base values
  default:
    break;
  }

  // Bipolar voice params (<= Decay) use symmetrical range around 0
  if (param <= AS5600ParameterMode::Decay)
  {
    float maxRange = getAS5600BaseValueRange(param);
    if (maxRange <= 0.0f)
      return 0.0f;
    return std::min(1.0f, fabsf(value) / maxRange);
  }

  // Unipolar params - proximity is distance from center normalized to [0,1]
  float minVal = getParameterMinValue(param);
  float maxVal = getParameterMaxValue(param);
  float mid = 0.5f * (minVal + maxVal);
  float halfRange = 0.5f * (maxVal - minVal);
  if (halfRange <= 0.0f)
    return 0.0f;
  return std::min(1.0f, fabsf(value - mid) / halfRange);
}

float calculateDynamicFlashSpeed(AS5600ParameterMode param)
{
  float proximity = calculateAS5600BoundaryProximity(param);
  // Iterate zones to find matching threshold
  for (const auto &zone : FLASH_SPEED_ZONES)
  {
    if (proximity >= zone.thresholdStart && proximity < zone.thresholdEnd)
    {
      return zone.speedMultiplier;
    }
  }
  // Edge case: exactly 1.0 proximity
  return FLASH_SPEED_ZONES[2].speedMultiplier; // Critical
}

// Helper function for the "Shift and Scale" mapping.
// This function takes a sequencer value (0.0-1.0) and an AS5600 offset
// (a bipolar value, e.g., -0.6 to 0.6) and combines them intelligently.
float shiftAndScale(float seqValue, float as5600Offset)
{
  float finalValue;
  if (as5600Offset >= 0.0f)
  {
    // When the AS5600 offset is positive, it sets the minimum value,
    // and the sequencer value is scaled to fit the remaining range up to 1.0.
    finalValue = as5600Offset + (seqValue * (1.0f - as5600Offset));
  }
  else
  {
    // When the AS5600 offset is negative, it reduces the maximum value,
    // and the sequencer value is scaled to fit the range from 0.0 up to that new maximum.
    finalValue = seqValue * (1.0f + as5600Offset);
  }
  // Clamp the result to ensure it remains within the valid [0.0, 1.0] range.
  return std::max(0.0f, std::min(finalValue, 1.0f));
}

/**
 * Apply AS5600 magnetic encoder base values to voice parameters.
 * Implements a "Shift and Scale" mapping to combine encoder and sequencer values.
 * This avoids "dead zones" by scaling the sequencer's output within the range
 * defined by the encoder's offset.
 * */
void applyAS5600BaseValues(VoiceState *voiceState, uint8_t voiceId)
{
  if (!as5600Sensor.isConnected() || !voiceState)
  {
    return;
  }

  // Select base values by pairing voices: 0/2 -> voice1 base; 1/3 -> voice2 base
  const AS5600BaseValues *baseValues = (voiceId % 2 == 1)
                                         ? (const AS5600BaseValues *)&as5600BaseValuesVoice2
                                         : (const AS5600BaseValues *)&as5600BaseValuesVoice1;

  // Apply "Shift and Scale" for each parameter.
  // This maps the sequencer value into the dynamic range set by the AS5600 offset.
  voiceState->velocityLevel = shiftAndScale(voiceState->velocityLevel, baseValues->velocity);
  voiceState->filterCutoff = shiftAndScale(voiceState->filterCutoff, baseValues->filter);
  voiceState->attackTimeSeconds = shiftAndScale(voiceState->attackTimeSeconds, baseValues->attack);
  voiceState->decayTimeSeconds = shiftAndScale(voiceState->decayTimeSeconds, baseValues->decay);
}

/**
 * Apply AS5600 magnetic encoder values to global delay effect parameters.
 * Direct parameter control: delay parameters use full range without restrictions.
 * Thread-safe communication for Core0 audio processing.
 */
void applyAS5600DelayValues()
{
  if (!as5600Sensor.isConnected())
  {
    return;
  }

  // Use Voice 1 base values for global delay parameters (delay is not per-voice)
  const AS5600BaseValuesVoice1 *baseValues = &as5600BaseValuesVoice1;

  // Apply delay time directly (already clamped to 10ms-1.8s range in updateAS5600BaseValues)
  delayTarget = baseValues->delayTime;

  // Apply delay feedback directly (already clamped to 0.0-1.0 range in updateAS5600BaseValues)
  feedbackAmount = baseValues->delayFeedback;
}

// ----------------------
// Helper: Update slide time on the active voice
// ----------------------
void updateAS5600SlideTime(uint8_t voiceId, float slideTime)
{
  if (!as5600Sensor.isConnected() || !uiState.slideMode)
  {
    return;
  }
  const AS5600BaseValues *activeBaseValues = uiState.isVoice2Mode ? &as5600BaseValuesVoice2 : &as5600BaseValuesVoice1;
  // finish up by updating the base values
  applyIncrementToParameter((AS5600BaseValues *)activeBaseValues, AS5600ParameterMode::SlideTime, slideTime);
}

// ----------------------
// Apply slide time values from AS5600 to the active voice
// ----------------------
void applyAS5600SlideTimeValues()
{
  if (!as5600Sensor.isConnected() || !uiState.slideMode)
  {
    return;
  }

  // Determine which base values are active
  AS5600BaseValues *activeBaseValues = uiState.isVoice2Mode
                                           ? (AS5600BaseValues *)&as5600BaseValuesVoice2
                                           : (AS5600BaseValues *)&as5600BaseValuesVoice1;

  // Read encoder increment for SlideTime (unipolar 0.0 - 1.0 seconds)
  float minVal = getParameterMinValue(AS5600ParameterMode::SlideTime);
  float maxVal = getParameterMaxValue(AS5600ParameterMode::SlideTime);
  float increment = as5600Sensor.getParameterIncrement(minVal - maxVal, maxVal - minVal, 3);

  // Apply increment if above noise threshold
  if (fabsf(increment) >= SensorConstants::MagneticEncoder::MINIMUM_INCREMENT_THRESHOLD)
  {
    applyIncrementToParameter(activeBaseValues, AS5600ParameterMode::SlideTime, increment);
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
//   AS5600 HELPER FUNCTIONS (moved from main file)
// =======================

/**
 * Gets the current value of the active AS5600 parameter, normalized to a 0.0-1.0 range.
 * This is used for visual feedback, such as controlling the brightness or color of an LED.
 */
float getAS5600ParameterValue()
{
  if (!as5600Sensor.isConnected())
  {
    return 0.0f;
  }

  const AS5600BaseValues *activeBaseValues = uiState.isVoice2Mode ? &as5600BaseValuesVoice2 : &as5600BaseValuesVoice1;
  float value = 0.0f;

  // Retrieve the raw value for the current parameter
  switch (uiState.currentAS5600Parameter)
  {
  case AS5600ParameterMode::Velocity:
    value = activeBaseValues->velocity;
    break;
  case AS5600ParameterMode::Filter:
    value = activeBaseValues->filter;
    break;
  case AS5600ParameterMode::Attack:
    value = activeBaseValues->attack;
    break;
  case AS5600ParameterMode::Decay:
    value = activeBaseValues->decay;
    break;
  case AS5600ParameterMode::DelayTime:
    value = activeBaseValues->delayTime;
    break;
  case AS5600ParameterMode::DelayFeedback:
    value = activeBaseValues->delayFeedback;
    break;
  case AS5600ParameterMode::SlideTime:
    value = activeBaseValues->slideTime;
    break;
  }

  // Normalize the value to a 0.0-1.0 range for LED feedback
  float minVal = getParameterMinValue(uiState.currentAS5600Parameter);
  float maxVal = getParameterMaxValue(uiState.currentAS5600Parameter);
  float normalizedValue = (value - minVal) / (maxVal - minVal);

  // For bipolar parameters (like velocity, filter, etc.), we need to handle the normalization differently.
  // Since they range from -maxRange to +maxRange, we can map this to 0.0-1.0.
  if (uiState.currentAS5600Parameter <= AS5600ParameterMode::Decay) // Assuming these are the bipolar params
  {
    float maxRange = getAS5600BaseValueRange(uiState.currentAS5600Parameter);
    normalizedValue = (value + maxRange) / (2 * maxRange);
  }

  return std::max(0.0f, std::min(normalizedValue, 1.0f)); // Clamp to ensure valid range
}

void initAS5600BaseValues()
{
  // Initialize voice parameters to neutral position for both voices
  as5600BaseValuesVoice1.velocity = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  as5600BaseValuesVoice1.filter = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  as5600BaseValuesVoice1.attack = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  as5600BaseValuesVoice1.decay = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;

  as5600BaseValuesVoice2.velocity = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  as5600BaseValuesVoice2.filter = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  as5600BaseValuesVoice2.attack = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
  as5600BaseValuesVoice2.decay = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;

  // Initialize delay parameters with reasonable defaults for both voices
  as5600BaseValuesVoice1.delayTime = SensorConstants::MagneticEncoder::DEFAULT_DELAY_TIME_SAMPLES;
  as5600BaseValuesVoice1.delayFeedback = SensorConstants::MagneticEncoder::DEFAULT_DELAY_FEEDBACK;
  as5600BaseValuesVoice2.delayTime = SensorConstants::MagneticEncoder::DEFAULT_DELAY_TIME_SAMPLES;
  as5600BaseValuesVoice2.delayFeedback = SensorConstants::MagneticEncoder::DEFAULT_DELAY_FEEDBACK;
}

void resetAS5600BaseValues(UIState &uiState, bool currentVoiceOnly)
{
  if (currentVoiceOnly)
  {
    // Reset only the currently active voice to neutral position
    AS5600BaseValues *activeVoiceBaseValues = uiState.isVoice2Mode
                                                  ? (AS5600BaseValues *)&as5600BaseValuesVoice2
                                                  : (AS5600BaseValues *)&as5600BaseValuesVoice1;

    // Reset voice parameters to neutral position
    activeVoiceBaseValues->velocity = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
    activeVoiceBaseValues->filter = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
    activeVoiceBaseValues->attack = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;
    activeVoiceBaseValues->decay = SensorConstants::MagneticEncoder::DEFAULT_VOICE_PARAMETER;

    // Note: Delay parameters are global and not reset with this function
  }
  else
  {
    // Reset all voices - call the full initialization
    initAS5600BaseValues();
  }
}