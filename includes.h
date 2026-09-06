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
//   I2C BUS PINS
// =======================
// Main bus (Wire, I2C0): OLED, MPR121, TMAG5273, VL53L1X.
constexpr uint8_t PIN_WIRE_SDA = 4;
constexpr uint8_t PIN_WIRE_SCL = 5;
// Tile bank (Wire1, I2C1) at 100 kHz: SliderModule + ButtonModule8.
// (I2S moved off GP15 onto GP10-12; see PICO_AUDIO_I2S_* in Pico2Seq.ino.)
constexpr uint8_t PIN_ALCHEMY_WIRE1_SDA = 14;
constexpr uint8_t PIN_ALCHEMY_WIRE1_SCL = 15;
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
// Stock uClock from the library manager (installed: 2.2.1; upstream 2.3.0
// changed the callback API — re-verify before upgrading). Its rp2040 backend
// runs the tick timer in the SDK default alarm pool, so the uClock ISR always
// fires on core 0 — the control core.
#include <uClock.h>
