// Pico2Seq: four sequencer voices, played with pads, faders and hand movement.
// Start here, then follow the module guide in docs/firmware-structure.md.
#include "src/app/Application.h"
#include "src/app/AudioEngine.h"

// Core 0 runs the musical controls, clock event processing, OLED and LEDs.
void setup()
{
    Application::begin();
}

void loop()
{
    Application::update();
}

// Core 1 only supplies audio to I2S. Buffer availability paces this core.
void setup1()
{
    AudioEngine::begin();
}

void loop1()
{
    AudioEngine::renderNextBuffer();
}
