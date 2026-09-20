#include "Application.h"
#include "AppState.h"
#include "ControlIO.h"
#include "ClockService.h"
#include "RetainedSession.h"
#include "Session.h"
#include "SessionStorage.h"
#include "VoiceSetup.h"
#include "AudioEngine.h"
#include "../sensors/DistanceSensor.h"
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
// The one snapshot buffer for every load, capture and save in this file.
// Static, not stack: a snapshot is ~12.4 KB, and Core 0's loop stack runs into
// Core 1's stack after ~4 KB and the heap after ~8 KB (a stack copy here hard-
// faulted the board within seconds). Uses are sequential on Core 0, never nested.
persistence::ProjectSnapshot g_sessionSnapshot;
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
            // lidar=-1 means nothing measured recently. st is the ST range status:
            // 0 valid, 1 sigma fail (still used), 2 signal fail, 4 out of bounds, 255 none.
            Serial.printf("[DIAG C0] ids=%u,%u,%u,%u mgrVoices=%u warmBoots=%lu steps=%lu audioBufs=%lu audio=%s i2sstage=%lu lidar=%dmm st=%u\n",
                          voiceSystem.getVoiceId(0), voiceSystem.getVoiceId(1),
                          voiceSystem.getVoiceId(2), voiceSystem.getVoiceId(3),
                          (unsigned)(voiceManager ? voiceManager->getVoiceCount() : 0),
                          (unsigned long)watchdog_hw->scratch[2],
                          (unsigned long)g_processedStepCount,
                          (unsigned long)AudioEngine::completedBufferCount(),
                          AudioEngine::phaseName(AudioEngine::phase()),
                          (unsigned long)AudioEngine::driverSetupStage(),
                          distanceSensor.getRawDistanceMm(),
                          static_cast<unsigned>(distanceSensor.getLastRangeStatus()));
        }
    }

    // Bench aid for live recording: every gate the lidar path passes through,
    // in one line. held is a bitmask over ParamId (bit 2 Filter, bit 10
    // Release), hand is PerformanceInput::handPresent, rec is the normalized
    // hand height the recorder writes, and each lane shows its own cursor and
    // the value stored there. A held button with hand=1 and a rec that moves
    // but a lane value that does not means the write is being rejected.
    static uint32_t lastRecordDiag = 0;
    if (currentMillis - lastRecordDiag >= kDiagnosticIntervalMs)
    {
        lastRecordDiag = currentMillis;
        if (Serial)
        {
            uint16_t held = 0;
            for (uint8_t lane = 0; lane < PARAM_ID_COUNT; ++lane)
                if (uiState.parameterButtonHeld[lane])
                    held |= static_cast<uint16_t>(1u << lane);
            const Sequencer &seq = AppState::sequencerView.clamped(uiState.selectedVoiceIndex);
            const uint8_t filterStep = seq.getCurrentStepForParameter(ParamId::Filter);
            const uint8_t releaseStep = seq.getCurrentStepForParameter(ParamId::Release);
            Serial.printf("[DIAG REC] held=0x%03X hand=%d rec=%.2f selStep=%d voice=%u "
                          "filt[%u]=%.3f rel[%u]=%.3f\n",
                          static_cast<unsigned>(held),
                          AppState::performanceInput.handPresent ? 1 : 0,
                          AppState::performanceInput.recordingValue(),
                          uiState.selectedStepForEdit,
                          static_cast<unsigned>(uiState.selectedVoiceIndex),
                          static_cast<unsigned>(filterStep),
                          seq.getStepParameterValue(ParamId::Filter, filterStep),
                          static_cast<unsigned>(releaseStep),
                          seq.getStepParameterValue(ParamId::Release, releaseStep));
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
    bool loaded;
    if (resumeFromRetained)
    {
        loaded = RetainedSession::takeResumeSnapshot(g_sessionSnapshot);
        if (!loaded)
            Serial.println("[STORAGE] retained session invalid; factory defaults");
    }
    else
    {
        loaded = SessionStorage::load(g_sessionSnapshot) == SessionStorage::LoadResult::Ok;
    }
    Session::g_bootLoadedOk = loaded;
    if (loaded)
    {
        Session::applyBeforeVoices(g_sessionSnapshot);
        g_bootSnapshotPending = true;
        Session::setLastSavedCrc(persistence::crc32(
            reinterpret_cast<const uint8_t *>(&g_sessionSnapshot), sizeof(g_sessionSnapshot)));
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
        Session::applyAfterVoices(g_sessionSnapshot);
    ControlIO::observeVoiceChanges();
    ControlIO::beginMatrixAndTiles();

    freezeWatchdogFeed(FW_SETUP_UCLOCK);
    initializeClock();
    if (g_bootSnapshotPending)
        Session::applyAfterClock(g_sessionSnapshot);
    Serial.println("[VOICE EDIT] Patch bases + lidar modifiers; Shift + slider 4 opens editor");
    Serial.println("[CORE0] Setup complete!");
    voicesReady.store(true, std::memory_order_release);

    // Seed the retained-RAM mirror so a freeze 100 ms into loop() still finds
    // a fresh session. markBootCompleted() deliberately does NOT run here —
    // the resume-attempt counter only clears after a proven-healthy loop.
    // The boot snapshot has been fully applied, so its buffer is free again.
    Session::captureSession(g_sessionSnapshot);
    RetainedSession::refresh(g_sessionSnapshot);
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
        Session::captureSession(g_sessionSnapshot);
        RetainedSession::refresh(g_sessionSnapshot);
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

        if (action == Session::PendingAction::Save)
        {
            Session::captureSession(g_sessionSnapshot);
            const uint32_t crc = persistence::crc32(
                reinterpret_cast<const uint8_t *>(&g_sessionSnapshot), sizeof(g_sessionSnapshot));
            if (SessionStorage::save(g_sessionSnapshot))
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
            if (SessionStorage::load(g_sessionSnapshot) == SessionStorage::LoadResult::Ok)
            {
                Session::applyBeforeVoices(g_sessionSnapshot);
                Session::applyAfterVoices(g_sessionSnapshot);
                Session::applyAfterClock(g_sessionSnapshot);
                Session::setLastSavedCrc(persistence::crc32(
                    reinterpret_cast<const uint8_t *>(&g_sessionSnapshot), sizeof(g_sessionSnapshot)));
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
        Session::captureSession(g_sessionSnapshot);
        const uint32_t crc = persistence::crc32(
            reinterpret_cast<const uint8_t *>(&g_sessionSnapshot), sizeof(g_sessionSnapshot));
        if (crc != Session::lastSavedCrc())
        {
            if (SessionStorage::save(g_sessionSnapshot))
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
    // Preserve this order: steps, diagnostics, gate ticks, controls, LEDs, OLED.
    freezeWatchdogFeed(FW_LOOP_CLOCK_EVENTS);
    processClockEvents();
    freezeWatchdogMark(FW_LOOP_DIAGNOSTICS);
    printRuntimeDiagnostics(nowMs);
    freezeWatchdogFeed(FW_LOOP_PPQN);
    processPendingGateTicks();
    ControlIO::scanControls(nowMs);
    ControlIO::refreshLeds(nowMs);
    ControlIO::refreshOled(nowMs);
}
