#ifndef AS5600_MANAGER_H
#define AS5600_MANAGER_H

#include "as5600.h"
#include "SensorConstants.h"
#include "../sequencer/SequencerDefs.h"
#include "../dsp/oscillator.h"
#include "../ui/UIState.h"

// Forward declarations
class AS5600Sensor;
struct VoiceState;
namespace daisysp {
  class Oscillator;
}

/**
 * @brief Flash speed zones for dynamic boundary proximity feedback
 * 
 * Defines different visual feedback zones based on how close a parameter
 * value is to its minimum or maximum boundary. Used to provide visual
 * warnings when approaching parameter limits.
 */
enum class FlashSpeedZone : uint8_t {
  Normal = 0,   // Normal operation range (0.0 to 0.65 proximity factor)
  Warning = 1,  // Warning zone (0.65 to 0.8375 proximity factor)
  Critical = 2  // Critical zone (0.8375 to 1.0 proximity factor)
};

/**
 * @brief Configuration for flash speed zones
 * 
 * Defines the speed multiplier and threshold ranges for each flash zone.
 * Used to create dynamic visual feedback that increases in intensity as
 * parameter values approach their boundaries.
 */
struct FlashSpeedConfig {
  float speedMultiplier;  // Flash speed multiplier for this zone
  float thresholdStart;   // Proximity factor where this zone starts (0.0-1.0)
  float thresholdEnd;     // Proximity factor where this zone ends (0.0-1.0)
};

// Global flash speed zone configuration array
extern const FlashSpeedConfig FLASH_SPEED_ZONES[];

// ======================
// Core Parameter Management Functions
// ======================

/**
 * @brief Apply parameter increment with boundary checking and clamping
 * 
 * Safely applies an increment to an AS5600 parameter while ensuring the
 * resulting value stays within valid bounds. Handles both bidirectional
 * parameters (voice parameters) and unidirectional parameters (delay/slide).
 * 
 * @param baseValues Pointer to the base values structure to modify
 * @param param The parameter type to modify
 * @param increment The increment value to apply (can be positive or negative)
 */
void applyIncrementToParameter(AS5600BaseValues* baseValues, AS5600ParameterMode param, float increment);

/**
 * @brief Update AS5600 base values using velocity-sensitive bidirectional control
 * 
 * Processes magnetic encoder input to update base parameter values for the
 * currently active voice. Uses velocity-sensitive scaling to provide fine
 * control at slow movements and coarse control at fast movements.
 * 
 * @param uiState Reference to UI state containing current voice and parameter selection
 * @note Automatically switches to step parameter editing if a step is selected
 */
void updateAS5600BaseValues(UIState& uiState);

/**
 * @brief Update individual step parameter values in edit mode
 * 
 * Processes magnetic encoder input to directly modify parameter values for
 * a specific sequencer step. Used when editing individual step parameters
 * rather than global base values.
 * 
 * @param uiState Reference to UI state containing step and parameter selection
 * @note Only active when uiState.selectedStepForEdit >= 0
 */
void updateAS5600StepParameterValues(UIState& uiState);

/**
 * @brief Apply AS5600 base values to voice synthesis parameters
 * 
 * Implements "Shift and Scale" mapping to combine magnetic encoder base values
 * with sequencer step values. This approach avoids dead zones by scaling the
 * sequencer output within the range defined by the encoder offset.
 * 
 * @param voiceState Pointer to voice state structure to modify
 * @param voiceId Voice identifier (0 = voice1, 1 = voice2)
 * @note Uses different base value sets for each voice
 */
void applyAS5600BaseValues(VoiceState *voiceState, uint8_t voiceId);

/**
 * @brief Apply AS5600 values to global delay effect parameters
 * 
 * Updates global delay time and feedback parameters using direct parameter
 * control. Delay parameters use full range without the bidirectional scaling
 * applied to voice parameters.
 * 
 * @note Delay parameters are global and not per-voice
 * @note Thread-safe for Core0 audio processing communication
 */
void applyAS5600DelayValues();

/**
 * @brief Apply AS5600 slide time values to active voice
 * 
 * Updates slide time parameter for smooth note transitions. Only active
 * when slide mode is enabled in the UI state.
 */
void applyAS5600SlideTimeValues();

// ======================
// Parameter Range and Validation Functions
// ======================

/**
 * @brief Get minimum valid value for AS5600 parameter type
 * 
 * @param param The parameter type to query
 * @return Minimum valid value for the parameter
 */
float getParameterMinValue(AS5600ParameterMode param);

/**
 * @brief Get maximum valid value for AS5600 parameter type
 * 
 * @param param The parameter type to query
 * @return Maximum valid value for the parameter
 */
float getParameterMaxValue(AS5600ParameterMode param);

/**
 * @brief Get allowed range for AS5600 base values
 * 
 * Calculates the bidirectional range allowed for base values, which may
 * be smaller than the full parameter range to leave room for sequencer values.
 * 
 * @param param The parameter type to query
 * @return Allowed range for base values (typically 75% of full range)
 */
float getAS5600BaseValueRange(AS5600ParameterMode param);

/**
 * @brief Clamp AS5600 base value to allowed bidirectional range
 * 
 * @param param The parameter type for range determination
 * @param value The value to clamp
 * @return Clamped value within allowed range
 */
float clampAS5600BaseValue(AS5600ParameterMode param, float value);

// ======================
// Step Parameter Editing Helper Functions
// ======================

/**
 * @brief Convert AS5600 parameter mode to sequencer ParamId
 * 
 * Maps AS5600 parameter types to their corresponding sequencer parameter IDs
 * for step editing functionality.
 * 
 * @param as5600Param The AS5600 parameter mode to convert
 * @return Corresponding ParamId, or ParamId::Count if not applicable
 */
ParamId convertAS5600ParameterToParamId(AS5600ParameterMode as5600Param);

/**
 * @brief Get minimum value for sequencer parameter ID
 * 
 * @param paramId The sequencer parameter ID to query
 * @return Minimum valid value for the parameter
 */
float getParameterMinValueForParamId(ParamId paramId);

/**
 * @brief Get maximum value for sequencer parameter ID
 * 
 * @param paramId The sequencer parameter ID to query
 * @return Maximum valid value for the parameter
 */
float getParameterMaxValueForParamId(ParamId paramId);

/**
 * @brief Format parameter value for display output
 * 
 * Converts raw parameter values to human-readable strings with appropriate
 * units and formatting for OLED display and debug output.
 * 
 * @param paramId The parameter type for formatting rules
 * @param value The raw parameter value to format
 * @return Formatted string representation of the value
 */
String formatParameterValueForDisplay(ParamId paramId, float value);

// ======================
// System Management and Utility Functions
// ======================

/**
 * @brief Calculate boundary proximity factor for visual feedback
 * 
 * Determines how close a parameter value is to its minimum or maximum
 * boundary, returning a factor from 0.0 (center) to 1.0 (at boundary).
 * Used for dynamic flash speed calculation.
 * 
 * @param param The parameter type to analyze
 * @return Proximity factor (0.0 = center, 1.0 = at boundary)
 */
float calculateAS5600BoundaryProximity(AS5600ParameterMode param);

/**
 * @brief Calculate dynamic flash speed based on boundary proximity
 * 
 * Determines the appropriate flash speed multiplier based on how close
 * a parameter value is to its boundaries. Provides visual feedback that
 * increases in intensity as limits are approached.
 * 
 * @param param The parameter type to analyze
 * @return Flash speed multiplier (1.0x to 3.0x)
 */
float calculateDynamicFlashSpeed(AS5600ParameterMode param);

/**
 * @brief Reset AS5600 base values to default positions
 * 
 * Resets parameter base values to their neutral or default positions.
 * Can reset either the current voice only or all voices depending on the flag.
 * 
 * @param uiState Reference to UI state for voice mode information
 * @param currentVoiceOnly If true, only reset currently active voice
 */
void resetAS5600BaseValues(UIState& uiState, bool currentVoiceOnly = true);

/**
 * @brief Initialize AS5600 base values with system defaults
 * 
 * Sets up initial base values for all voices with appropriate defaults:
 * - Voice parameters: neutral position (0.0f)
 * - Delay parameters: reasonable defaults (200ms delay, 55% feedback)
 */
void initAS5600BaseValues();

extern AS5600Sensor as5600Sensor;



extern float delayTarget;
extern float feedbackAmmount;

extern const size_t MAX_DELAY_SAMPLES;


#endif // AS5600_MANAGER_H
