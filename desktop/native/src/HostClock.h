#pragma once
// Driver-facing surface of the desktop uClock implementation (HostClock.cpp).
//
// The firmware's uClock runs off a hardware timer ISR on Core 0. The desktop
// port drives the exact same tick logic from the audio callback's sample
// counter: 48,000 samples per second advance an absolute nanosecond deadline,
// so the clock is sample-accurate and can never drift against the audio.

#include <cstdint>

namespace p2s
{
namespace host
{
// Advance the clock to an absolute 48 kHz sample index (fires all due ticks).
void advanceClockBySamples(uint64_t sampleIndex);

// Advance to an absolute nanosecond timestamp (offline/test path).
void advanceClockNs(uint64_t nowNs);

// Clear all state including registered callbacks (test isolation only).
void resetHostClockForTest();
}
}
