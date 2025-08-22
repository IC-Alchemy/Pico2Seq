#pragma once

// Core libraries
#include <FastLED.h>
#include <Melopero_VL53L1X.h>
#include <Adafruit_MPR121.h> // MAKE SURE TO ENABLE AUTOCONFIG IN MPR121.H
#include <MIDI.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>


// Matrix and Sequencer
#include "src/scales/scales.h"
#include "src/matrix/Matrix.h"
#include "src/sequencer/Sequencer.h"
#include "src/sequencer/SequencerDefs.h"

// LED Matrix
#include "src/LEDMatrix/ledMatrix.h"
#include "src/LEDMatrix/LEDMatrixFeedback.h"
#include "src/LEDMatrix/LEDController.h"

// Sensors
#include "src/sensors/SensorConstants.h"
#include "src/sensors/DistanceSensor.h"
#include "src/sensors/as5600.h"
#include "src/sensors/AS5600Manager.h"

// MIDI and UI
#include "src/midi/MidiManager.h"
#include "src/ui/UIEventHandler.h"
#include "src/ui/ButtonManager.h"
#include "src/ui/UIState.h"

// OLED Display
#include "src/OLED/oled.h"

// Standard libraries
#include <Wire.h>
#include <cmath>
#include <cstdint>
#include <uClock.h>