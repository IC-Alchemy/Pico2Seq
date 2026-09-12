#ifndef ENCODER_MANAGER_H
#define ENCODER_MANAGER_H

#include "../VelocityEncoder/src/MagEncoder.h"
#include "SensorConstants.h"
#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include "../utils/DspMapping.h" // dspmap::fmap for filter Hz display
#include "../ui/UIState.h"

// Forward declarations
struct VoiceState;

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
 * @brief Update encoder base values using velocity-sensitive bidirectional control
 *
 * Processes magnetic encoder input to update base parameter values for the
 * currently active voice. Uses velocity-sensitive scaling to provide fine
 * control at slow movements and coarse control at fast movements.
 *
 * @param uiState Reference to UI state containing current voice and parameter selection
 * @note Automatically switches to step parameter editing if a step is selected
 */
void updateEncoderBaseValues(UIState& uiState);

// ======================
// Step Parameter Editing Helper Functions
// ======================

/**
 * @brief Convert encoder parameter mode to sequencer ParamId
 *
 * Maps encoder parameter types to their corresponding sequencer parameter IDs
 * for step editing functionality.
 *
 * @param encoderParam The encoder parameter mode to convert
 * @return Corresponding ParamId, or ParamId::Count if not applicable
 */
ParamId convertEncoderParameterToParamId(EncoderParameterMode encoderParam);

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

// ======================
// System Management and Utility Functions
// ======================



/**
 * @brief Reset encoder base values to default positions
 *
 * Resets parameter base values to their neutral or default positions.
 * Can reset either the current voice only or all voices depending on the flag.
 *
 * @param uiState Reference to UI state for voice mode information
 * @param currentVoiceOnly If true, only reset currently active voice
 */
void resetEncoderBaseValues(UIState& uiState, bool currentVoiceOnly = true);

// Discard pending encoder motion; preset setup owns initial patch bases.
void initEncoderBaseValues();

// Global magnetic encoder driver (TMAG5273 on the Velocity Encoder board).
// Defined in EncoderManager.cpp; the main sketch accesses it through this
// extern.
extern MagEncoder magEncoder;

#endif // ENCODER_MANAGER_H
