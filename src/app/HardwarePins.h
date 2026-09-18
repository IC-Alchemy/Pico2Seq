#pragma once
#include <cstdint>

// Main I2C bus (Wire, I2C0, 400 kHz): OLED display (SH1106, 0x3C), touch pads
// (MPR121), magnetic encoder (TMAG5273) and distance sensor (VL53L1X).
constexpr uint8_t PIN_WIRE_SDA = 4;
constexpr uint8_t PIN_WIRE_SCL = 5;
// Secondary I2C bus (Wire1, I2C1): Alchemy tiles only, at 100 kHz.
constexpr uint8_t PIN_ALCHEMY_WIRE1_SDA = 14;
constexpr uint8_t PIN_ALCHEMY_WIRE1_SCL = 15;
// GP7 to GND: LOW = Param, HIGH = Utility. Polarity is in ControlSurfaceLogic.h.
constexpr uint8_t PIN_ALCHEMY_MODE_SWITCH = 7;
// MPR121 /IRQ: active-low, open-drain; Matrix configures INPUT_PULLUP.
constexpr uint8_t PIN_MPR121_INT = 8;

// I2S owns these pads. The PIO drives LRCK at clock base + 1.
constexpr uint8_t PICO_AUDIO_I2S_DATA_PIN = 12;
constexpr uint8_t PICO_AUDIO_I2S_CLOCK_PIN_BASE = 10; // BCLK GP10, LRCK GP11
