#pragma once
#include <cstdint>

// Core 0 prepares the optional effect before publishing the voices.
// Core 1 owns the I2S pool and renders one paced buffer per loop1() call.
namespace AudioEngine
{
// A best-effort snapshot, queued without Serial or waiting on the audio core.
struct Heartbeat
{
    uint32_t bufferCount;
    uint8_t voiceIds[4];
};

void prepareEffects();
void begin();
void renderNextBuffer();
bool takeHeartbeat(Heartbeat &heartbeat) noexcept; // Core 0 only
}
