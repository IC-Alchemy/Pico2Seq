// Pico2Seq: 4-voice polyphonic step sequencer/synth for pads, faders, and hand motion.
// Musical role: the groove box the performer touches; technical role: thin delegate
// to src/app/ (Core 0: Application, Core 1: AudioEngine). Keep logic out of here.
// Start here, then follow the module guide in docs/firmware-structure.md.
#include "src/app/Application.h"
#include "src/app/AudioEngine.h"

// Core 0: musical controls, clock-event drain, OLED/LED feedback. Must stay responsive;
// heavy work (flash I/O) is deferred inside Application::update().
void setup()
{
    Application::begin();
}

void loop()
{
    Application::update();
}

// Core 1: synthesis only. Blocking on buffer availability *is* the 48 kHz pacing;
// never add UI, sensor, or Serial work here — it would break the groove.
void setup1()
{
    AudioEngine::begin();
}

void loop1()
{
    AudioEngine::renderNextBuffer();
}
