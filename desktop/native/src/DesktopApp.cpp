#include "DesktopApp.h"
#include "DesktopAudio.h"
#include "DesktopStorage.h"

#include "app/AppState.h"
#include "app/ControlIO.h"
#include "app/ClockService.h"
#include "app/Session.h"
#include "app/SessionStorage.h"
#include "app/StepPlayback.h"
#include "app/VoiceSetup.h"
#include "pico2seq-core/persistence/ProjectSnapshot.h"
#include "pico2seq-core/persistence/SnapshotFormat.h"
#include "ui/UIConstants.h"
#include "ui/UIState.h"

#include <Arduino.h>
#include <uClock.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
// The one snapshot buffer for every load, capture and save in this file,
// matching Application.cpp's sequential-use discipline.
persistence::ProjectSnapshotV1 g_sessionSnapshot;
bool g_bootSnapshotPending = false;

std::atomic<bool> g_running{false};
std::thread g_controlThread;
bool g_controlThreadStarted = false;

void controlLoop()
{
    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    while (g_running.load(std::memory_order_relaxed))
    {
        DesktopApp::updateOnce();
        // 1 kHz fixed-rate cadence, same as the firmware's loop() intent.
        next += std::chrono::milliseconds(1);
        std::this_thread::sleep_until(next);
    }
}
} // namespace

void DesktopApp::begin(const Options &options)
{
    if (g_running.exchange(true))
        return; // already begun

    Serial.begin(115200);
    Serial.println("[DESKTOP] Core0 setup starting");

    // Session storage loads before voices exist, exactly like the firmware.
    p2s::storage::setOverrideDir(options.storageDir);
    Serial.println("[DESKTOP] stage: storage begin");
    SessionStorage::begin();
    Serial.println("[DESKTOP] stage: storage load");
    const bool loaded =
        SessionStorage::load(g_sessionSnapshot) == SessionStorage::LoadResult::Ok;
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

    Serial.println("[DESKTOP] stage: main bus + leds");
    ControlIO::beginMainBusAndLeds();
    Serial.println("[DESKTOP] stage: performance sensors");
    ControlIO::beginPerformanceSensors();
    Serial.println("[DESKTOP] stage: touch pads");
    ControlIO::beginTouchPads();
    Serial.println("[DESKTOP] stage: display");
    ControlIO::beginDisplay();

    Serial.println("[DESKTOP] stage: initializeVoices");
    initializeVoices(); // consumes uiState.voicePresetIndices
    if (g_bootSnapshotPending)
        Session::applyAfterVoices(g_sessionSnapshot);
    Serial.println("[DESKTOP] stage: observeVoiceChanges");
    ControlIO::observeVoiceChanges();
    Serial.println("[DESKTOP] stage: matrix and tiles");
    ControlIO::beginMatrixAndTiles();

    Serial.println("[DESKTOP] stage: initializeClock");
    initializeClock();
    if (g_bootSnapshotPending)
        Session::applyAfterClock(g_sessionSnapshot);
    g_bootSnapshotPending = false;
    Serial.println("[DESKTOP] Core0 setup complete");
    voicesReady.store(true, std::memory_order_release);

    Session::captureSession(g_sessionSnapshot);

    // Audio runs on its own thread (the firmware's Core 1): the device
    // callback owns the render path and the HostClock sample counter, and
    // must not share the UI thread's COM apartment.
    if (options.startAudioDevice)
    {
        std::thread([]() {
            DesktopAudio::Options audioOptions;
            audioOptions.useDevice = true;
            if (!DesktopAudio::instance().begin(audioOptions))
                Serial.println("[DESKTOP] audio device start FAILED; clock runs only on rendered audio");
            else
                Serial.println("[DESKTOP] audio device started");
        }).detach();
    }

    if (options.startControlThread)
    {
        g_controlThreadStarted = true;
        g_controlThread = std::thread(controlLoop);
    }
}

void DesktopApp::updateOnce()
{
    if (!voiceManager)
        return;
    // Retry queued controls even without new input, including a final gate-off.
    voiceManager->flushControlUpdates();
    const uint32_t nowMs = millis();

    // Deferred session I/O: requested by UI handlers, executed here — never
    // in input-scan or clock-callback context (same contract as the firmware).
    const Session::PendingAction action = Session::consumePendingAction();
    if (action != Session::PendingAction::None)
    {
        const bool wasRunning = isClockRunning;
        if (wasRunning)
            stopClockForEditor(); // also drains pending steps
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
    }

    // Autosave on transport stop (debounced), identical policy to the firmware.
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

    // Preserve this order: steps, gate ticks, controls, displays.
    ControlIO::pollHeldButtons();
    processClockEvents();
    processPendingGateTicks();
    ControlIO::scanControls(nowMs);
    ControlIO::refreshDisplays(nowMs);
}

void DesktopApp::shutdown()
{
    if (!g_running.exchange(false))
        return;
    if (g_controlThreadStarted && g_controlThread.joinable())
        g_controlThread.join();
    g_controlThreadStarted = false;

    DesktopAudio::instance().shutdown();
    uClock.stop(); // runs the clock-stop cleanup path (gate releases)
    voicesReady.store(false, std::memory_order_release);
}

bool DesktopApp::isRunning() { return g_running.load(std::memory_order_relaxed); }
