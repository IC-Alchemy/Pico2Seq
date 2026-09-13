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
#include "../pico2seq-core/persistence/SnapshotFormat.h"
#include "../ui/UIConstants.h"
#include <Arduino.h>
#include <uClock.h>

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
        Session::setLastSavedCrc(persistence::crc32(
            reinterpret_cast<const uint8_t *>(&snapshot), sizeof(snapshot)));
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

    // One-time confirmation that a session was restored at boot.
    static bool bootNoticeShown = false;
    if (!bootNoticeShown)
    {
        bootNoticeShown = true;
        if (Session::g_bootLoadedOk)
        {
            uiState.oledNoticeKind = UIState::OledNoticeKind::Loaded;
            uiState.oledNoticeUntil = nowMs + OLED_NOTICE_DURATION_MS;
        }
    }

    // Deferred flash I/O: requested by UI handlers, executed here — never in
    // ISR/uClock callback context. A save with the transport running stops
    // the clock for the erase window and restarts it after.
    const Session::PendingAction action = Session::consumePendingAction();
    if (action != Session::PendingAction::None)
    {
        const bool wasRunning = isClockRunning;
        if (wasRunning)
            stopClockForEditor(); // also drains pending steps (ClockService.cpp)
        voiceManager->flushControlUpdates();

        persistence::ProjectSnapshotV1 snap;
        Session::captureSession(snap);
        const uint32_t crc = persistence::crc32(
            reinterpret_cast<const uint8_t *>(&snap), sizeof(snap));

        if (action == Session::PendingAction::Save)
        {
            if (SessionStorage::save(snap))
            {
                Session::setLastSavedCrc(crc);
                uiState.oledNoticeKind = UIState::OledNoticeKind::Saved;
                uiState.oledNoticeUntil = nowMs + OLED_NOTICE_DURATION_MS;
                Serial.println("[STORAGE] saved");
            }
            else
            {
                uiState.oledNoticeKind = UIState::OledNoticeKind::LoadError;
                uiState.oledNoticeUntil = nowMs + OLED_NOTICE_DURATION_MS;
                Serial.println("[STORAGE] save FAILED");
            }
            if (wasRunning)
                uClock.start(); // onClockStart restarts all four sequencers
        }
        else // Load
        {
            persistence::ProjectSnapshotV1 loaded{};
            if (SessionStorage::load(loaded) == SessionStorage::LoadResult::Ok)
            {
                Session::applyBeforeVoices(loaded);
                Session::applyAfterVoices(loaded);
                Session::applyAfterClock(loaded);
                Session::setLastSavedCrc(persistence::crc32(
                    reinterpret_cast<const uint8_t *>(&loaded), sizeof(loaded)));
                uiState.oledNoticeKind = UIState::OledNoticeKind::Loaded;
                uiState.oledNoticeUntil = nowMs + OLED_NOTICE_DURATION_MS;
                Serial.println("[STORAGE] loaded");
            }
            else
            {
                uiState.oledNoticeKind = UIState::OledNoticeKind::LoadError;
                uiState.oledNoticeUntil = nowMs + OLED_NOTICE_DURATION_MS;
                Serial.println("[STORAGE] load FAILED");
            }
        }
        freezeWatchdogFeed(FW_LOOP_USB_READ); // long flash op: re-arm the 2 s budget
    }

    // Autosave on transport stop (debounced). isClockRunning flips inside the
    // uClock callbacks (ISR-adjacent); polling the edge here stays safe.
    static bool wasClockRunningForAutosave = false;
    static uint32_t stopEdgeMs = 0;
    if (wasClockRunningForAutosave && !isClockRunning)
        stopEdgeMs = nowMs;
    wasClockRunningForAutosave = isClockRunning;
    if (stopEdgeMs != 0 && !isClockRunning && nowMs - stopEdgeMs >= 1000)
    {
        stopEdgeMs = 0;
        persistence::ProjectSnapshotV1 snap;
        Session::captureSession(snap);
        const uint32_t crc = persistence::crc32(
            reinterpret_cast<const uint8_t *>(&snap), sizeof(snap));
        if (crc != Session::lastSavedCrc())
        {
            if (SessionStorage::save(snap))
            {
                Session::setLastSavedCrc(crc);
                Serial.println("[STORAGE] autosaved on stop");
            }
        }
    }

    // Bench aid: type 'W' over serial to stop feeding the watchdog and prove
    // the retained-RAM resume path end-to-end (plan Task 12).
    if (Serial.available() > 0 && Serial.read() == 'W')
    {
        Serial.println("[BENCH] freezing Core 0 on request");
        for (;;) {}
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
