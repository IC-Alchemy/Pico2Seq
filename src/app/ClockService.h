#pragma once
// ClockService: uClock groove timing staged for the control loop.
// Musical role: keeps the four voices on a tight shared grid (480 PPQN + shuffle).
// Technical role: ISR callbacks only stage steps/ticks; loop() does the real work.
// Stock uClock's timer fires on Core 0, so everything here is Core 0 only.
void stopClockForEditor();
#include <cstdint>

// Start/stop run in thread context; step + PPQN callbacks only stage events.
void initializeClock();
void onClockStart();
void onClockStop();
void processClockEvents();
void processPendingGateTicks();

// Post-mortem step counter persisted for the watchdog report.
extern uint32_t g_processedStepCount;
