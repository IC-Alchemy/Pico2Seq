// Headless end-to-end soak: boots the full desktop engine (voices, clock,
// control loop) offline, programs a pattern through the real Sequencer API,
// renders 4 seconds of audio through the real VoiceManager, and asserts the
// sequencer advanced exactly as the firmware would and audio actually sounded.

#include <catch2/catch_test_macros.hpp>

#include "../src/DesktopApp.h"
#include "../src/DesktopAudio.h"
#include "../src/DisplayBridge.h"

#include "Adafruit_MPR121.h"
#include "app/AppState.h"
#include "app/ClockService.h"
#include "pico2seq-core/sequencer/Sequencer.h"
#include "pico2seq-core/sequencer/SequencerDefs.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

namespace
{
struct TempSessionDir
{
    std::string path;

    TempSessionDir()
    {
        path = (std::filesystem::temp_directory_path() / "p2s_headless_test").string();
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    ~TempSessionDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
} // namespace

TEST_CASE("Headless 4-second soak renders steps and sound", "[desktop][soak]")
{
    TempSessionDir sessionDir;

    DesktopApp::Options options;
    options.storageDir = sessionDir.path.c_str();
    options.startAudioDevice = false;  // offline render; no WASAPI in CI/tests
    options.startControlThread = false; // deterministic manual pumping
    DesktopApp::begin(options);

    // Drive a pad press through the REAL input stack: touch bits -> MPR121
    // interrupt -> Matrix_scan -> matrixEventHandler -> gate toggle. Pad 0 is
    // matrix row 0 / col 0, i.e. MPR121 electrodes 3 and 4.
    Sequencer &seq = *AppState::sequencers[0];
    const float gateBefore = seq.getStepParameterValue(ParamId::Gate, 0);
    p2s::host::setMpr121TouchBits((1u << 3) | (1u << 4));
    for (int i = 0; i < 5; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        DesktopApp::updateOnce(); // scanControls gate runs at 1 kHz
    }
    p2s::host::setMpr121TouchBits(0); // release
    for (int i = 0; i < 5; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        DesktopApp::updateOnce();
    }
    const float gateAfter = seq.getStepParameterValue(ParamId::Gate, 0);
    CHECK(gateAfter != gateBefore); // tap toggled the step gate
    CHECK(gateAfter == 1.0f);

    // Program two more notes through the public sequencer API so the soak
    // has guaranteed audible content (Square preset defaults, velocity 0.9).
    seq.setStepParameterValue(ParamId::Gate, 0, 1.0f);
    seq.setStepParameterValue(ParamId::Gate, 8, 1.0f);
    seq.setStepParameterValue(ParamId::Note, 0, 12.0f);
    seq.setStepParameterValue(ParamId::Note, 8, 24.0f);
    seq.setStepParameterValue(ParamId::Velocity, 0, 0.9f);
    seq.setStepParameterValue(ParamId::Velocity, 8, 0.9f);

    const uint32_t stepsBefore = g_processedStepCount;

    // 4 seconds of audio at 90 BPM = 24 sixteenth steps. Render 256-frame
    // blocks (the firmware's exact buffer size) and pump the control loop
    // per block, which is faster than its real 1 kHz cadence.
    int16_t buffer[256 * 2];
    int32_t peak = 0;
    const uint64_t totalFrames = 4ULL * 48000;
    for (uint64_t rendered = 0; rendered < totalFrames; rendered += 256)
    {
        DesktopAudio::instance().renderOffline(256, buffer);
        for (int i = 0; i < 512; ++i)
        {
            const int32_t s = buffer[i];
            if (s > peak)
                peak = s;
            else if (-s > peak)
                peak = -s;
        }
        DesktopApp::updateOnce();
    }

    const uint32_t stepsAfter = g_processedStepCount;
    CHECK(stepsAfter - stepsBefore == 24);
    CHECK(peak > 1000); // a gated Square-preset voice must be audible

    // Display plane: LEDs and OLED must have rendered frames for the GUI.
    uint8_t leds[p2s::display::kLedCount * 3];
    uint8_t oled[p2s::display::kOledBytes];
    CHECK(p2s::display::copyLeds(leds) > 0);
    CHECK(p2s::display::copyOled(oled) > 0);
    bool oledHasInk = false;
    for (uint8_t b : oled)
        if (b)
        {
            oledHasInk = true;
            break;
        }
    CHECK(oledHasInk); // status screen actually drew something

    DesktopApp::shutdown();
}
