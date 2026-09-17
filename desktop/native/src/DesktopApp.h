#pragma once
// Desktop twin of src/app/Application (Core-0 equivalent).
//
// begin() mirrors Application::begin()'s startup order minus the RP2350
// watchdog/retained-RAM layer; updateOnce() is one pass of
// Application::update() with the same ordering contract. A dedicated thread
// pumps updateOnce() at 1 kHz, matching the firmware's control-loop cadence.

#include <cstdint>

namespace DesktopApp
{
struct Options
{
    const char *storageDir = nullptr; // default %APPDATA%\Pico2Seq
    bool startAudioDevice = true;     // false = offline rendering (tests)
    bool startControlThread = true;   // false = caller pumps updateOnce()
};

// Runs the full startup sequence. Not idempotent: call once per process
// (or after shutdown()).
void begin(const Options &options = {});

// One control-loop pass (control thread calls this; tests call it directly).
void updateOnce();

// Stops threads/audio cleanly. Voices are torn down at DLL unload.
void shutdown();

bool isRunning();
}
