#pragma once
#include <cstdint>

// ControlIO: Core 0 hands, lights, and display polling.
// Musical role: turns pads, faders, knob, and hand height into sound edits every
// millisecond, and mirrors the groove on LEDs/OLED. All blocking I2C stays here.
namespace ControlIO
{
void beginMainBusAndLeds();
void beginPerformanceSensors();
void beginTouchPads();
void beginDisplay();
void observeVoiceChanges();
void beginMatrixAndTiles();
void pollHeldButtons();
void scanControls(uint32_t nowMs);
void refreshLeds(uint32_t nowMs);
void refreshOled(uint32_t nowMs);
}
