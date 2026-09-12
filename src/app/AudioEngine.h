#pragma once
#include <cstdint>

// Core 0 publishes the voices after control setup; Core 1 owns the I2S pool
// and renders one paced buffer per loop1() call.
namespace AudioEngine
{
enum class Phase : uint32_t {
    NotStarted, BufferPool, I2SSetup, I2SDriver, I2SConnect, InitialFill,
    I2SEnable, BufferWait, Render, Submit, Failed
};
// Read-only diagnostics for Core 0; the count advances after a buffer is submitted.
Phase phase() noexcept;
uint32_t completedBufferCount() noexcept;
uint32_t driverSetupStage() noexcept;
const char *phaseName(Phase phase) noexcept;

// A best-effort snapshot, queued without Serial or waiting on the audio core.
struct Heartbeat
{
    uint32_t bufferCount;
    uint8_t voiceIds[4];
    uint32_t renderAverageUs; // Last reporting window, excludes buffer wait
    uint32_t renderMaxUs;
    uint32_t renderOverBudget; // Cumulative buffers exceeding 256 / 48000 s
    uint32_t underruns; // Cumulative DMA silence substitutions
    uint32_t txStalls; // Cumulative observations of stalled I2S output
};

void begin();
void renderNextBuffer();
bool takeHeartbeat(Heartbeat &heartbeat) noexcept; // Core 0 only
}
