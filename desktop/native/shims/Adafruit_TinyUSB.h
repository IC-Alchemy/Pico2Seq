#pragma once
// Desktop replacement for Adafruit_TinyUSB.h.
//
// USB MIDI transmission was removed from the firmware on 2026-09-06;
// MidiManager.h includes this header only for historical plumbing. Like the
// real TinyUSB header, it transitively provides the Arduino core.

#include <Arduino.h>
