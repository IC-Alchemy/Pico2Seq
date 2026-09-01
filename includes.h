#pragma once

// Core libraries
#include <FastLED.h>
#include <Adafruit_VL53L1X.h>
#include <Adafruit_MPR121.h> // MAKE SURE TO ENABLE AUTOCONFIG IN MPR121.H
#include <MIDI.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>


// Audio and DSP (voice DSP comes from src/rpdsp via src/voice/Voice.h)
#include "src/audio/audio.h"
#include "src/audio/audio_i2s.h"
#include "src/pico2seq-core/scales/scales.h"

// Matrix and Sequencer
#include "src/matrix/Matrix.h"
#include "src/pico2seq-core/sequencer/Sequencer.h"
#include "src/pico2seq-core/sequencer/SequencerDefs.h"

// LED Matrix
#include "src/LEDMatrix/ledMatrix.h"
#include "src/LEDMatrix/LEDMatrixFeedback.h"
#include "src/LEDMatrix/LEDController.h"

// Sensors
#include "src/sensors/SensorConstants.h"
#include "src/sensors/DistanceSensor.h"
#include "src/VelocityEncoder/src/MagEncoder.h"
#include "src/sensors/EncoderManager.h"

// MIDI and UI
#include "src/midi/MidiManager.h"
#include "src/ui/UIEventHandler.h"
#include "src/ui/ButtonManager.h"
#include "src/ui/UIState.h"
#include "src/ui/AlchemyControlBridge.h"

// =======================
//   ALCHEMY TILE CONTROL SURFACE PINS
// =======================
// SliderModule + ButtonModule8 live on a dedicated Wire1 bank at 400 kHz.
// Proposed pins avoid the gate pins 10-12 — confirm against the actual panel
// wiring at the bench (design doc, bench item 1).
constexpr uint8_t PIN_ALCHEMY_WIRE1_SDA = 8;
constexpr uint8_t PIN_ALCHEMY_WIRE1_SCL = 9;
// GP7 strap switch to GND: LOW = Param mode, HIGH = Utility mode. If the
// bench polarity is inverted, flip ControlSurface::kModeParamLevel in
// src/ui/ControlSurfaceLogic.h instead.
constexpr uint8_t PIN_ALCHEMY_MODE_SWITCH = 7;

// OLED Display
#include "src/OLED/oled.h"

// Voices
#include "src/voice/VoiceManager.h"
#include "src/voice/Voice.h"
#include "src/voice/VoiceSystem.h"
#include "src/pico2seq-core/sequencer/Sequencer.h"

// Standard libraries
#include <Wire.h>
#include <cmath>
#include <cstdint>
#include <uClock.h>
