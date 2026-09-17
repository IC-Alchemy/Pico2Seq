#include "DesktopAudio.h"
#include "HostClock.h"

#include "miniaudio.h"

#include "app/AppState.h"
#include "app/Pcm16.h"
#include "voice/VoiceManager.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace
{
constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kMaxBlock = VoiceManager::kMaxBlock; // 256, same as firmware buffers

std::atomic<bool> g_deviceFailed{false};
} // namespace

DesktopAudio &DesktopAudio::instance()
{
    static DesktopAudio audio;
    return audio;
}

void DesktopAudio::render(int16_t *out, uint32_t frames)
{
    if (!voicesReady.load(std::memory_order_acquire))
    {
        // Core 0 may still be constructing voices; mirror AudioEngine's
        // silence-before-ready path but still advance the clock deadline.
        std::memset(out, 0, static_cast<size_t>(frames) * 2 * sizeof(int16_t));
        sampleCounter += frames;
        p2s::host::advanceClockBySamples(sampleCounter);
        return;
    }

    // Fixed 256-frame spans: identical cadence and scratch bounds to the
    // firmware's fill_audio_buffer(), including the PCM16 round-trip.
    static float mixBuffer[kMaxBlock];
    uint32_t offset = 0;
    while (offset < frames)
    {
        const uint32_t count = std::min(frames - offset, kMaxBlock);
        voiceManager->processBlock(mixBuffer, count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const int16_t sample = AudioSamples::toPcm16(mixBuffer[i]);
            out[2 * (offset + i)] = sample;
            out[2 * (offset + i) + 1] = sample;
        }
        offset += count;
        sampleCounter += count;
        p2s::host::advanceClockBySamples(sampleCounter);
    }
}

void DesktopAudio::onDataStatic(ma_device *device, void *output, const void * /*input*/,
                                uint32_t frameCount)
{
    DesktopAudio *self = static_cast<DesktopAudio *>(device->pUserData);
    if (self)
        self->render(static_cast<int16_t *>(output), frameCount);
}

bool DesktopAudio::begin(const Options &options)
{
    offlineOnly = !options.useDevice;
    if (offlineOnly)
        return true;

    device = static_cast<ma_device *>(std::malloc(sizeof(ma_device)));
    if (!device)
        return false;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = 2;
    config.sampleRate = kSampleRate;
    config.dataCallback = &DesktopAudio::onDataStatic;
    config.pUserData = this;

    if (ma_device_init(nullptr, &config, device) != MA_SUCCESS)
    {
        std::free(device);
        device = nullptr;
        g_deviceFailed.store(true);
        return false;
    }
    if (ma_device_start(device) != MA_SUCCESS)
    {
        ma_device_uninit(device);
        std::free(device);
        device = nullptr;
        g_deviceFailed.store(true);
        return false;
    }
    return true;
}

void DesktopAudio::shutdown()
{
    if (device)
    {
        ma_device_uninit(device);
        std::free(device);
        device = nullptr;
    }
}

void DesktopAudio::renderOffline(uint32_t frames, int16_t *out)
{
    render(out, frames);
}
