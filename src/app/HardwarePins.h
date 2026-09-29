#pragma once
#include <cstdint>

// HardwarePins: the wiring map in one place so a rewire touches one file.
// Musical role: defines which fingers/ears reach which pads (I2C) and the DAC
// (I2S). Two I2C buses keep pad scanning tight while tiles chatter separately.

// Main bus (Wire @ 400 kHz): display + pads + knob + hand sensor share it.
constexpr uint8_t PIN_WIRE_SDA = 4;
constexpr uint8_t PIN_WIRE_SCL = 5;
// Tile bus (Wire1 @ 100 kHz): Alchemy faders/buttons only. Slower on purpose.
constexpr uint8_t PIN_ALCHEMY_WIRE1_SDA = 14;
constexpr uint8_t PIN_ALCHEMY_WIRE1_SCL = 15;
// GP7 to GND: LOW = Param, HIGH = Utility. Polarity is in ControlSurfaceLogic.h.
constexpr uint8_t PIN_ALCHEMY_MODE_SWITCH = 7;
// MPR121 /IRQ: active-low, open-drain; Matrix configures INPUT_PULLUP.
constexpr uint8_t PIN_MPR121_INT = 8;

// I2S owns these pads; PIO derives LRCK as clock base + 1. Moving them breaks sound.
constexpr uint8_t PICO_AUDIO_I2S_DATA_PIN = 12;
constexpr uint8_t PICO_AUDIO_I2S_CLOCK_PIN_BASE = 10; // BCLK GP10, LRCK GP11
