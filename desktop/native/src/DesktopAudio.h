#pragma once
// WASAPI audio output for the desktop port (Core-1 equivalent).
//
// The firmware's Core 1 loop pulls 256-frame buffers from a producer pool;
// the desktop equivalent renders the same 256-frame blocks inside the
// miniaudio callback and then advances the HostClock by the rendered sample
// count, so transport timing is locked to the audio device clock.

#include <cstdint>

struct ma_device;

class DesktopAudio
{
public:
    struct Options
    {
        bool useDevice = true; // false = offline rendering only (tests)
    };

    static DesktopAudio &instance();

    // Initializes the engine audio path and (optionally) the playback device.
    // Returns false only when a device was requested but failed to start;
    // offline rendering still works in that case.
    bool begin(const Options &options);
    void shutdown();

    // Offline/test path: render `frames` stereo s16 frames into `out` and
    // advance the clock. `out` must hold frames*2 int16_t values.
    void renderOffline(uint32_t frames, int16_t *out);

    uint64_t renderedSamples() const { return sampleCounter; }

private:
    static void onDataStatic(ma_device *device, void *output, const void *input,
                             uint32_t frameCount);
    void render(int16_t *out, uint32_t frames);

    ma_device *device = nullptr;
    uint64_t sampleCounter = 0;
    bool offlineOnly = false;
};
