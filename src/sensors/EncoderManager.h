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

// Jog the focused target: selected-step lane if one is targeted, else the
// encoder's patch base via VoiceEditor. Drops ticks while controlsWaitRelease.
void updateEncoderBaseValues(UIState& uiState);

// Lane bounds for clamping a turn (from CORE_PARAMETERS).
float getParameterMinValueForParamId(ParamId paramId);

float getParameterMaxValueForParamId(ParamId paramId);

// Discard pending encoder motion; preset setup owns initial patch bases.
void initEncoderBaseValues();

// Global magnetic encoder driver (TMAG5273 on the Velocity Encoder board).
// Defined in EncoderManager.cpp; the main sketch accesses it through this
// extern.
extern MagEncoder magEncoder;

#endif // ENCODER_MANAGER_H
