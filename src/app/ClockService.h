#pragma once
#include <cstdint>

// Stock uClock's timer runs on Core 0. Start/stop are thread callbacks;
// step and PPQN callbacks only stage events for the control loop.
void initializeClock();
void onClockStart();
void onClockStop();
void processClockEvents();
void processPendingGateTicks();

// Kept for the watchdog's persisted post-mortem counter.
extern uint32_t g_processedStepCount;
