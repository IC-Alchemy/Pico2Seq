#include "Application.h"
#include "AppState.h"
#include "ControlIO.h"
#include "ClockService.h"
#include "VoiceSetup.h"
#include "AudioEngine.h"
#include "../utils/FreezeWatchdog.h"
#include <Arduino.h>

namespace
{
constexpr uint32_t kBootStabilizationMs = 100;
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kDiagnosticIntervalMs = 2000;
bool recoveryMode = false;

// Serial and watchdog diagnostics stay on Core 0.
void printRuntimeDiagnostics(uint32_t currentMillis)
{
    static uint32_t lastVoiceDiag = 0;
    if (currentMillis - lastVoiceDiag >= kDiagnosticIntervalMs)
    {
        lastVoiceDiag = currentMillis;
        if (Serial)
        {
            freezeWatchdogPrintPreviousRun();
            Serial.printf("[DIAG C0] ids=%u,%u,%u,%u mgrVoices=%u warmBoots=%lu steps=%lu audioBufs=%lu audio=%s\n",
                          voiceSystem.getVoiceId(0), voiceSystem.getVoiceId(1),
                          voiceSystem.getVoiceId(2), voiceSystem.getVoiceId(3),
                          (unsigned)(voiceManager ? voiceManager->getVoiceCount() : 0),
                          (unsigned long)watchdog_hw->scratch[2],
                          (unsigned long)g_processedStepCount,
                          (unsigned long)AudioEngine::completedBufferCount(),
                          AudioEngine::phaseName(AudioEngine::phase()));
        }
    }

    AudioEngine::Heartbeat heartbeat{};
    while (AudioEngine::takeHeartbeat(heartbeat))
    {
        if (Serial)
        {
            Serial.printf("[DIAG C1] alive bufs=%lu ids=%u,%u,%u,%u render_us=%lu max_us=%lu budget_us=5333 over=%lu underruns=%lu txstalls=%lu\n",
                          static_cast<unsigned long>(heartbeat.bufferCount),
                          heartbeat.voiceIds[0], heartbeat.voiceIds[1],
                          heartbeat.voiceIds[2], heartbeat.voiceIds[3],
                          static_cast<unsigned long>(heartbeat.renderAverageUs),
                          static_cast<unsigned long>(heartbeat.renderMaxUs),
                          static_cast<unsigned long>(heartbeat.renderOverBudget),
                          static_cast<unsigned long>(heartbeat.underruns),
                          static_cast<unsigned long>(heartbeat.txStalls));
        }
    }
}
} // namespace

void Application::begin()
{
    freezeWatchdogBootCheck();
    delay(kBootStabilizationMs);
    Serial.begin(kSerialBaud);
    recoveryMode = previousFreeze.watchdogReset;
    if (recoveryMode)
    {
        // Leave peripherals and voices untouched so the previous failure
        // cannot reset us again before the USB monitor has time to reconnect.
        watchdog_disable();
        return;
    }
    Serial.print("[CORE0] Setup starting... ");
    Serial.printf("clock=%lu MHz\n", (unsigned long)(F_CPU / 1000000));

    ControlIO::beginMainBusAndLeds();
    ControlIO::beginPerformanceSensors();
    ControlIO::beginTouchPads();
    ControlIO::beginDisplay();
    freezeWatchdogFeed(FW_SETUP_VOICES);
    initializeVoices();
    ControlIO::observeVoiceChanges();
    ControlIO::beginMatrixAndTiles();

    freezeWatchdogFeed(FW_SETUP_UCLOCK);
    initializeClock();
    Serial.println("[CORE0] Setup complete!");
    voicesReady.store(true, std::memory_order_release);
}

void Application::update()
{
    if (recoveryMode)
    {
        static uint32_t lastReportMs = 0;
        const uint32_t nowMs = millis();
        if (Serial && nowMs - lastReportMs >= kDiagnosticIntervalMs)
        {
            lastReportMs = nowMs;
            Serial.println("[RECOVERY] Startup paused after watchdog reset. Power-cycle to retry.");
            freezeWatchdogPrintPreviousRun();
        }
        return;
    }
    // Retry queued controls even without new input, including a final gate-off.
    voiceManager->flushControlUpdates();
    freezeWatchdogFeed(FW_LOOP_USB_READ); // Retain the persisted watchdog phase ID.
    const uint32_t nowMs = millis();

    ControlIO::pollHeldButtons();
    // Preserve this order: steps, diagnostics, gate ticks, controls, displays.
    freezeWatchdogFeed(FW_LOOP_CLOCK_EVENTS);
    processClockEvents();
    freezeWatchdogMark(FW_LOOP_DIAGNOSTICS);
    printRuntimeDiagnostics(nowMs);
    freezeWatchdogFeed(FW_LOOP_PPQN);
    processPendingGateTicks();
    ControlIO::scanControls(nowMs);
    ControlIO::refreshDisplays(nowMs);
}
