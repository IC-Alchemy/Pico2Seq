#pragma once

// Core libraries
#include <FastLED.h>
#include <Melopero_VL53L1X.h>
#include <Adafruit_MPR121.h> // MAKE SURE TO ENABLE AUTOCONFIG IN MPR121.H
#include <MIDI.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// Project Libraries
#include <LEDMatrix.h>
#include <Sequencer.h>
#include <UI.h>
#include <Voice.h>

// Audio and DSP
#include "src/audio/audio.h"
#include "src/audio/audio_i2s.h"
#include "src/dsp/adsr.h"
#include "src/dsp/ladder.h"
#include "src/dsp/svf.h"
#include "src/dsp/oscillator.h"
#include "src/dsp/delayline.h"
#include "src/scales/scales.h"
#include "src/dsp/wavefolder.h"
#include "src/dsp/overdrive.h"

// Matrix
#include "src/matrix/Matrix.h"

// Sensors
#include "src/sensors/SensorConstants.h"
#include "src/sensors/DistanceSensor.h"
#include "src/sensors/as5600.h"
#include "src/sensors/AS5600Manager.h"

// MIDI
#include "src/midi/MidiManager.h"

// OLED Display
#include "src/OLED/oled.h"

// Standard libraries
#include <Wire.h>
#include <cmath>
#include <cstdint>
#include <uClock.h>