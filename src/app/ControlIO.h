#pragma once
#include <cstdint>

// All bus, sensor and display work belongs to Core 0.
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
void refreshDisplays(uint32_t nowMs);
}
