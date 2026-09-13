#include "Application.h"
#include "AppState.h"
#include "ControlIO.h"
#include "ClockService.h"
#include "VoiceSetup.h"
#include "AudioEngine.h"
#include "../utils/FreezeWatchdog.h"
#include <Arduino.h>

// diagnostic.h defines these in the header, so AudioEngine.cpp is its only
// includer; re-declare the two flags here for the DIAG audio-failure line.
extern volatile bool g_audioOK;
extern volatile uint8_t g_errorState;

namespace
{
constexpr uint32_t kBootStabilizationMs = 100;
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kDiagnosticIntervalMs = 2000;
constexpr uint32_t kAudioStallAlarmMs = 5000;    // Heartbeat silence before alarming.
constexpr uint32_t kAudioStallRetryMs = 10000;   // Repeat the alarm at this interval.

// Serial and watchdog diagnostics stay on Core 0.
void printRuntimeDiagnostics(uint32_t currentMillis)
{
    if (Serial)
    {
        // Post-mortem of the previous run, staged at boot and held in RAM
        // until a host attaches (the old 3 s boot-time wait lost it).
        freezeWatchdogPrintPostMortem();
    }

    // A fault on either core stores this before the forced reboot; usually the
    // reboot's [FREEZE] post-mortem reports it, but if Core 0 is still looping
    // (e.g. the faulting core is Core 1 and the TRIGGER reset is in flight),
    // print the live copy too.
    if (freezeFaultPending)
    {
        freezeFaultPending = 0;
        if (Serial)
        {
            Serial.printf("[FAULT] core %lu PC=0x%08lx LR=0x%08lx CFSR=0x%08lx BFAR=0x%08lx\n",
                          (unsigned long)freezeFaultCore, (unsigned long)freezeFaultPc,
                          (unsigned long)freezeFaultLr, (unsigned long)freezeFaultCfsr,
                          (unsigned long)freezeFaultBfar);
        }
    }

    static uint32_t lastVoiceDiag = 0;
    if (currentMillis - lastVoiceDiag >= kDiagnosticIntervalMs)
    {
        lastVoiceDiag = currentMillis;
        if (Serial)
        {
            Serial.printf("[DIAG C0] ids=%u,%u,%u,%u mgrVoices=%u gates=%d%d%d%d steps=%u,%u,%u,%u warmBoots=%lu\n",
                          voiceSystem.getVoiceId(0), voiceSystem.getVoiceId(1),
                          voiceSystem.getVoiceId(2), voiceSystem.getVoiceId(3),
                          (unsigned)(voiceManager ? voiceManager->getVoiceCount() : 0),
                          (int)voiceSystem.getGate(0), (int)voiceSystem.getGate(1),
                          (int)voiceSystem.getGate(2), (int)voiceSystem.getGate(3),
                          AppState::sequencers[0]->getCurrentStep(),
                          AppState::sequencers[1]->getCurrentStep(),
                          AppState::sequencers[2]->getCurrentStep(),
                          AppState::sequencers[3]->getCurrentStep(),
                          (unsigned long)watchdog_hw->scratch[2]);
        }
    }

    // One-shot: I2S setup failing is otherwise silent (no DMA consumer means
    // the audio core wedges on its first blocking take without any report).
    static bool audioSetupReported = false;
    if (!audioSetupReported && !g_audioOK && voicesReady.load(std::memory_order_acquire))
    {
        audioSetupReported = true;
        if (Serial)
        {
            Serial.printf("[DIAG C0] AUDIO SETUP FAILED errState=%u (I2S never enabled)\n",
                          (unsigned)g_errorState);
        }
    }

    // The audio core stops pushing heartbeats when it faults or wedges, while
    // Core 0 keeps this loop looking healthy. Track the last buffer-count
    // advance so a dead render loop is announced, not silent.
    static uint32_t lastHeartbeatBufs = 0;
    static uint32_t lastHeartbeatAdvanceMs = 0;
    static uint32_t nextStallReportMs = 0;

    AudioEngine::Heartbeat heartbeat{};
    while (AudioEngine::takeHeartbeat(heartbeat))
    {
        if (heartbeat.bufferCount != lastHeartbeatBufs)
        {
            lastHeartbeatBufs = heartbeat.bufferCount;
            lastHeartbeatAdvanceMs = currentMillis;
        }
        if (Serial)
        {
            Serial.printf("[DIAG C1] alive bufs=%lu ids=%u,%u,%u,%u\n",
                          static_cast<unsigned long>(heartbeat.bufferCount),
                          heartbeat.voiceIds[0], heartbeat.voiceIds[1],
                          heartbeat.voiceIds[2], heartbeat.voiceIds[3]);
        }
    }

    if (voicesReady.load(std::memory_order_acquire) &&
        currentMillis - lastHeartbeatAdvanceMs >= kAudioStallAlarmMs &&
        currentMillis >= nextStallReportMs)
    {
        nextStallReportMs = currentMillis + kAudioStallRetryMs;
        if (Serial)
        {
            Serial.printf("[DIAG C1] STALLED bufs=%lu (no advance for >=%lu ms)\n",
                          (unsigned long)lastHeartbeatBufs,
                          (unsigned long)(currentMillis - lastHeartbeatAdvanceMs));
        }
    }
}
} // namespace

void Application::begin()
{
    freezeWatchdogBootCheck();
    delay(kBootStabilizationMs);
    Serial.begin(kSerialBaud);
    Serial.print("[CORE0] Setup starting... ");
    Serial.printf("[BUILD] %s %s fault-forensics-2\n", __DATE__, __TIME__);

    // Build the audio buffer pools here (core 0): the audio core must never
    // allocate — arduino-pico's heap lock doesn't exclude the other core.
    // Waits briefly for core 1's I2S hardware setup, which it must precede.
    AudioEngine::allocateBuffers();

    ControlIO::beginMainBusAndLeds();
    ControlIO::beginPerformanceSensors();
    ControlIO::beginTouchPads();
    ControlIO::beginDisplay();
    initializeVoices();
    ControlIO::observeVoiceChanges();
    ControlIO::beginMatrixAndTiles();

    freezeWatchdogFeed(FW_SETUP_UCLOCK);
    initializeClock();
    Serial.println("[CORE0] Setup complete!");
}

void Application::update()
{
    // Retry queued controls even without new input, including a final gate-off.
    voiceManager->flushControlUpdates();
    freezeWatchdogFeed(FW_LOOP_USB_READ); // Retain the persisted watchdog phase ID.
    const uint32_t nowMs = millis();

    ControlIO::pollHeldButtons();
    // Preserve this order: steps, diagnostics, gate ticks, controls, displays.
    freezeWatchdogFeed(FW_LOOP_CLOCK_EVENTS);
    processClockEvents();
    printRuntimeDiagnostics(nowMs);
    freezeWatchdogFeed(FW_LOOP_PPQN);
    processPendingGateTicks();
    ControlIO::scanControls(nowMs);
    ControlIO::refreshDisplays(nowMs);
}
