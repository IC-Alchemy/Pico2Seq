#include "Application.h"
#include "AppState.h"
#include "ControlIO.h"
#include "ClockService.h"
#include "RetainedSession.h"
#include "Session.h"
#include "SessionStorage.h"
#include "VoiceSetup.h"
#include "AudioEngine.h"
#include "../utils/FreezeWatchdog.h"
#include "../pico2seq-core/persistence/ProjectSnapshot.h"
#include <Arduino.h>

namespace
{
constexpr uint32_t kBootStabilizationMs = 100;
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kDiagnosticIntervalMs = 2000;
// The watchdog-resume attempt counter only resets after the control loop has
// run this long without a freeze; clearing it earlier would let a freeze that
// recurs shortly after every boot resume forever.
constexpr uint32_t kHealthyLoopIntervalMs = 15000;
bool recoveryMode = false;
persistence::ProjectSnapshotV1 g_pendingBootSnapshot;
bool g_bootSnapshotPending = false;

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
            Serial.printf("[DIAG C0] ids=%u,%u,%u,%u mgrVoices=%u warmBoots=%lu steps=%lu audioBufs=%lu audio=%s i2sstage=%lu\n",
                          voiceSystem.getVoiceId(0), voiceSystem.getVoiceId(1),
                          voiceSystem.getVoiceId(2), voiceSystem.getVoiceId(3),
                          (unsigned)(voiceManager ? voiceManager->getVoiceCount() : 0),
                          (unsigned long)watchdog_hw->scratch[2],
                          (unsigned long)g_processedStepCount,
                          (unsigned long)AudioEngine::completedBufferCount(),
                          AudioEngine::phaseName(AudioEngine::phase()),
                          (unsigned long)AudioEngine::driverSetupStage());
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
    RetainedSession::bootInit();
    bool resumeFromRetained = false;
    if (previousFreeze.watchdogReset)
    {
        if (RetainedSession::resumeAllowed())
        {
            resumeFromRetained = true;
            recoveryMode = false;
            Serial.println("[RECOVERY] watchdog reset; resuming live session from retained RAM");
            freezeWatchdogPrintPreviousRun();
        }
        else
        {
            // Fall back to parking: leave peripherals and voices untouched so
            // the previous failure cannot reset us again before the USB
            // monitor has time to reconnect. The flash-saved session is still
            // there; a power cycle restores it at boot.
            recoveryMode = true;
            watchdog_disable();
            Serial.println("[RECOVERY] session resume unavailable/exhausted. Power-cycle to retry.");
            return;
        }
    }
    Serial.print("[CORE0] Setup starting... ");
    Serial.printf("clock=%lu MHz\n", (unsigned long)(F_CPU / 1000000));

    // Session storage mounts BEFORE anything arms the watchdog
    // (ControlIO::beginMainBusAndLeds -> freezeWatchdogArm): a first-boot
    // LittleFS format can take seconds and must not reboot us mid-format.
    freezeWatchdogMark(FW_SETUP_STORAGE); // breadcrumb only; not armed yet
    SessionStorage::begin();
    persistence::ProjectSnapshotV1 snapshot;
    bool loaded;
    if (resumeFromRetained)
    {
        loaded = RetainedSession::takeResumeSnapshot(snapshot);
        if (!loaded)
            Serial.println("[STORAGE] retained session invalid; factory defaults");
    }
    else
    {
        loaded = SessionStorage::load(snapshot) == SessionStorage::LoadResult::Ok;
    }
    Session::g_bootLoadedOk = loaded;
    if (loaded)
    {
        Session::applyBeforeVoices(snapshot);
        g_pendingBootSnapshot = snapshot;
        g_bootSnapshotPending = true;
        Serial.println("[STORAGE] session loaded");
    }
    else
    {
        Serial.println("[STORAGE] no valid session; factory defaults");
    }

    ControlIO::beginMainBusAndLeds();
    ControlIO::beginPerformanceSensors();
    ControlIO::beginTouchPads();
    ControlIO::beginDisplay();
    freezeWatchdogFeed(FW_SETUP_VOICES);
    initializeVoices(); // consumes uiState.voicePresetIndices
    if (g_bootSnapshotPending)
        Session::applyAfterVoices(g_pendingBootSnapshot);
    ControlIO::observeVoiceChanges();
    ControlIO::beginMatrixAndTiles();

    freezeWatchdogFeed(FW_SETUP_UCLOCK);
    initializeClock();
    if (g_bootSnapshotPending)
        Session::applyAfterClock(g_pendingBootSnapshot);
    Serial.println("[VOICE EDIT] Patch bases + lidar modifiers; Shift + slider 4 opens editor");
    Serial.println("[CORE0] Setup complete!");
    voicesReady.store(true, std::memory_order_release);

    // Seed the retained-RAM mirror so a freeze 100 ms into loop() still finds
    // a fresh session. markBootCompleted() deliberately does NOT run here —
    // the resume-attempt counter only clears after a proven-healthy loop.
    persistence::ProjectSnapshotV1 snap;
    Session::captureSession(snap);
    RetainedSession::refresh(snap);
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

    // Reset the watchdog-resume attempt counter only after the control loop
    // has demonstrably run healthy (see kHealthyLoopIntervalMs).
    static const uint32_t bootStampMs = millis(); // first non-recovery pass
    static bool healthyBootMarked = false;
    if (!healthyBootMarked && nowMs - bootStampMs >= kHealthyLoopIntervalMs)
    {
        healthyBootMarked = true;
        RetainedSession::markBootCompleted();
    }

    // 1 Hz retained-RAM mirror refresh: zero flash wear, bounds watchdog
    // session loss to one second.
    static uint32_t lastRetainedRefreshMs = 0;
    if (nowMs - lastRetainedRefreshMs >= 1000)
    {
        lastRetainedRefreshMs = nowMs;
        persistence::ProjectSnapshotV1 snap;
        Session::captureSession(snap);
        RetainedSession::refresh(snap);
    }

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
