#pragma once

// Central include map: one place to see every subsystem the firmware wires together.
// Musical role: none at runtime — build-time only. Technical role: legacy aggregator
// kept so older modules still compile; new app code in src/app/ includes directly.
// Core note: no code here, so no core constraints.

// Third-party UI/sensor/display drivers.
#include <FastLED.h>
#include <Adafruit_VL53L1X.h>
#include <Adafruit_MPR121.h> // MAKE SURE TO ENABLE AUTOCONFIG IN MPR121.H
// <MIDI.h> / USB MIDI removed 2026-09-06. Adafruit_TinyUSB.h stays: it provides
// the TinyUSB CDC serial console (Serial); usbstack=tinyusb is still required.
#include <Adafruit_TinyUSB.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>


// Sound out (I2S pool) + what notes are legal (scale tables).
#include "src/audio/audio.h"
#include "src/audio/audio_i2s.h"
#include "src/pico2seq-core/scales/scales.h"

// Step pads + the polymetric pattern engine they drive.
#include "src/matrix/Matrix.h"
#include "src/pico2seq-core/sequencer/Sequencer.h"
#include "src/pico2seq-core/sequencer/SequencerDefs.h"

// Step-grid light feedback.
#include "src/LEDMatrix/ledMatrix.h"
#include "src/LEDMatrix/LEDMatrixFeedback.h"

// Hand-distance + knob motion feeding live recording.
#include "src/sensors/SensorConstants.h"
#include "src/sensors/DistanceSensor.h"
#include "src/VelocityEncoder/src/MagEncoder.h"
#include "src/sensors/EncoderManager.h"

// Performer-facing controls and shared UI state.
#include "src/ui/UIEventHandler.h"
#include "src/ui/ButtonManager.h"
#include "src/ui/UIState.h"
#include "src/ui/AlchemyControlBridge.h"

#include "src/app/HardwarePins.h"

// 128x64 status display.
#include "src/OLED/oled.h"

// Synth voices and their Core-0-owned staging structs.
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
