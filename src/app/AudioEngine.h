#pragma once
#include <cstdint>

// Core 0 builds the buffer pools (allocateBuffers) — the audio core must never
// call malloc: arduino-pico's heap lock only gates interrupts on the current
// core, so cross-core allocation corrupts the heap. Core 1 programs the I2S
// hardware (begin), then renders one paced buffer per loop1() call.
namespace AudioEngine
{
// A best-effort snapshot, queued without Serial or waiting on the audio core.
struct Heartbeat
{
    uint32_t bufferCount;
    uint8_t voiceIds[4];
};

void allocateBuffers(); // Core 0 only: create producer/consumer pools (heap).
void prepareEffects();
void begin();           // Core 1 only: I2S hardware, waits for buffers, enables.
void renderNextBuffer();
bool takeHeartbeat(Heartbeat &heartbeat) noexcept; // Core 0 only
}
