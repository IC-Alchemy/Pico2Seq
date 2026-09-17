// Windows SDK strictly first: winuser.h's INPUT typedef must not meet the
// Arduino INPUT macro pulled in by the app headers below.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "api.h"
#include "DesktopApp.h"
#include "DisplayBridge.h"

#include "app/AppState.h"
#include "app/ClockService.h"

#include <Adafruit_MPR121.h>
#include <Adafruit_VL53L1X.h>
#include <uClock.h>

#include <atomic>
#include <exception>
#include <cstdio>

namespace
{
// A C++ exception escaping into the WinRT activation context presents as a
// fail-fast crash with no message; this surfaces it in the debug output.
std::atomic<bool> g_initThreadActive{false};

void p2sTerminateHandler()
{
#if defined(_WIN32)
    OutputDebugStringA("[P2S] std::terminate during p2s_init\n");
#endif
    std::abort();
}
} // namespace

extern "C"
{
int p2s_init(const P2sInitOptions *options)
{
    std::set_terminate(p2sTerminateHandler);
    DesktopApp::Options o;
    if (options)
    {
        o.storageDir = options->storageDir;
        o.startAudioDevice = options->startAudioDevice != 0;
        o.startControlThread = options->startControlThread != 0;
    }
    try
    {
        DesktopApp::begin(o);
    }
    catch (const std::exception &e)
    {
#if defined(_WIN32)
        char buf[512];
        std::snprintf(buf, sizeof(buf), "[P2S] p2s_init exception: %s\n", e.what());
        OutputDebugStringA(buf);
#endif
        DesktopApp::shutdown();
        return -2;
    }
    catch (...)
    {
#if defined(_WIN32)
        OutputDebugStringA("[P2S] p2s_init unknown exception\n");
#endif
        DesktopApp::shutdown();
        return -3;
    }
    return DesktopApp::isRunning() ? 0 : -1;
}

void p2s_shutdown(void) { DesktopApp::shutdown(); }

int p2s_is_running(void) { return DesktopApp::isRunning() ? 1 : 0; }

void p2s_transport_start(void) { uClock.start(); }

void p2s_transport_stop(void) { uClock.stop(); }

int p2s_transport_running(void) { return isClockRunning ? 1 : 0; }

void p2s_push_touch(uint16_t electrodeBits) { p2s::host::setMpr121TouchBits(electrodeBits); }

void p2s_push_lidar(int distanceMm) { p2s::host::setLidarSampleMm(distanceMm); }

uint64_t p2s_poll_leds(uint8_t *outRgb96) { return p2s::display::copyLeds(outRgb96); }

uint64_t p2s_poll_oled(uint8_t *outBuffer1024) { return p2s::display::copyOled(outBuffer1024); }

void p2s_get_status(P2sStatus *out)
{
    if (!out)
        return;
    out->transportRunning = isClockRunning ? 1 : 0;
    out->selectedVoice = uiState.selectedVoiceIndex;
    out->tempoBpm = uClock.getTempo();
    out->currentScale = currentScale;
    out->currentShuffle = uiState.currentShufflePatternIndex;
    out->currentTheme = uiState.currentThemeIndex;
    out->stepEditActive = uiState.selectedStepForEdit >= 0 ? 1 : 0;
    out->voiceEditorActive = uiState.voiceEditor.active ? 1 : 0;
    out->padBankB = uiState.isVoice2Mode ? 1 : 0;
    out->processedSteps = g_processedStepCount;
    out->ledFrames = p2s::display::ledFrameSerial();
    out->oledFrames = p2s::display::oledFrameSerial();
}

uint32_t p2s_processed_step_count(void) { return g_processedStepCount; }
}
