#ifndef ENCODER_MANAGER_H
#define ENCODER_MANAGER_H

#include "../VelocityEncoder/src/MagEncoder.h"
#include "SensorConstants.h"
#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include "../utils/DspMapping.h" // dspmap::fmap for filter Hz display
#include "../ui/UIState.h"

// EncoderManager — TMAG5273 jog/scrub knob (Core 0, I2C).
// Player view: the one knob that edits whatever is focused — a selected step's
// lane, or the patch base the encoder targets. Slow turns whisper (fine), fast
// spins jump (velocity curve in the MagEncoder driver).

// Forward declarations
struct VoiceState;

// Flash zones: how close a lane is to its min/max edge (0 = mid, 1 = edge).
enum class FlashSpeedZone : uint8_t {
  Normal = 0,   // Normal operation range (0.0 to 0.65 proximity factor)
  Warning = 1,  // Warning zone (0.65 to 0.8375 proximity factor)
  Critical = 2  // Critical zone (0.8375 to 1.0 proximity factor)
};

// Proximity-to-limit zones: the LEDs flash faster as a value nears its edge.
struct FlashSpeedConfig {
  float speedMultiplier;  // Flash speed multiplier for this zone
  float thresholdStart;   // Proximity factor where this zone starts (0.0-1.0)
  float thresholdEnd;     // Proximity factor where this zone ends (0.0-1.0)
};

// Global flash speed zone configuration array
extern const FlashSpeedConfig FLASH_SPEED_ZONES[];

// Jog the focused target: selected-step lane if one is targeted, else the
// encoder's patch base via VoiceEditor. Drops ticks while controlsWaitRelease.
void updateEncoderBaseValues(UIState& uiState);

// Lane bounds for clamping a turn (from CORE_PARAMETERS).
float getParameterMinValueForParamId(ParamId paramId);

float getParameterMaxValueForParamId(ParamId paramId);

// Restore preset bases (current voice, or all four).
void resetEncoderBaseValues(UIState& uiState, bool currentVoiceOnly = true);

// Discard pending encoder motion; preset setup owns initial patch bases.
void initEncoderBaseValues();

// Global magnetic encoder driver (TMAG5273 on the Velocity Encoder board).
// Defined in EncoderManager.cpp; the main sketch accesses it through this
// extern.
extern MagEncoder magEncoder;

#endif // ENCODER_MANAGER_H
