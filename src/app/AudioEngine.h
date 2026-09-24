#pragma once
#include <cstdint>

// AudioEngine: Core 1 synthesis loop feeding I2S @ 48 kHz.
// Musical role: turns the four voices into click-free stereo sound. Technical role:
// owns the 4x256-sample producer pool and renders one paced buffer per loop1().
// Core 1 only: never block (except on buffer availability), never allocate.
namespace AudioEngine
{
enum class Phase : uint32_t {
    NotStarted, BufferPool, I2SSetup, I2SDriver, I2SConnect, InitialFill,
    I2SEnable, BufferWait, Render, Submit, Failed
};
// Read-only for Core 0; the count advances after each submitted buffer.
Phase phase() noexcept;
uint32_t completedBufferCount() noexcept;
uint32_t driverSetupStage() noexcept;
const char *phaseName(Phase phase) noexcept;

// Best-effort liveness snapshot; queued without Serial or waiting on Core 1.
struct Heartbeat
{
    uint32_t bufferCount;
    uint8_t voiceIds[4];
    uint32_t renderAverageUs; // Last window mean, excluding buffer-wait time
    uint32_t renderMaxUs;
    uint32_t renderOverBudget; // Buffers slower than 256/48000 s (~5.33 ms)
    uint32_t underruns; // DMA silence substitutions (listener hears dropouts)
    uint32_t txStalls; // Observed stalls of the I2S output
};

void begin();
void renderNextBuffer();
bool takeHeartbeat(Heartbeat &heartbeat) noexcept; // Core 0 only
}
