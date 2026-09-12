#include "AudioEngine.h"
#include "AppState.h"
#include "HardwarePins.h"
#include "../audio/audio.h"
#include "../audio/audio_i2s.h"
#include <Arduino.h>
#include "../../diagnostic.h"
#include "Pcm16.h"
#include "../utils/SpscQueue.h"
#include <atomic>

static_assert(std::atomic<bool>::is_always_lock_free, "Audio readiness must not lock");
static_assert(std::atomic<AudioEngine::Phase>::is_always_lock_free, "Audio diagnostics must not lock");
static_assert(sizeof(AudioEngine::Heartbeat::voiceIds) == VoiceSystem::MAX_VOICES,
              "Heartbeat must cover the fixed voice collection");

constexpr float SAMPLE_RATE = 48000.0f;
// Four buffers retain the old effective depth (three queued + one DMA buffer),
// without a separate set of consumer buffers or copies in the DMA interrupt.
constexpr int NUM_AUDIO_BUFFERS = 4;
constexpr int SAMPLES_PER_BUFFER = 256;

namespace
{
constexpr uint32_t kHeartbeatIntervalMs = 2000;
constexpr size_t kHeartbeatQueueCapacity = 4;
constexpr uint16_t kStereoChannels = 2;
constexpr uint16_t kStereoFrameBytes = kStereoChannels * sizeof(int16_t);
std::atomic<AudioEngine::Phase> audioPhase{AudioEngine::Phase::NotStarted};
std::atomic<uint32_t> completedBuffers{0};
std::atomic<uint32_t> driverStage{0};
bool audioStarted = false; // Core 1 only
SpscQueue<AudioEngine::Heartbeat, kHeartbeatQueueCapacity> heartbeats;
audio_buffer_pool_t *producer_pool = nullptr;



void fill_audio_buffer(audio_buffer_t *buffer)
{
    int N = buffer->max_sample_count;
    int16_t *out = reinterpret_cast<int16_t *>(buffer->buffer->bytes);
    float finalVoiceOutput;

    if (!voicesReady.load(std::memory_order_acquire))
    {
        // Core 0 may still be constructing the voices. Acquire their publication
        // before even reading the manager pointer or its fixed collection.
        for (int i = 0; i < N; ++i)
        {
            out[2 * i + 0] = 0;
            out[2 * i + 1] = 0;
        }
        buffer->sample_count = N;
        return;
    }

    // Process each sample in the buffer
    for (int i = 0; i < N; ++i)
    {
        // Process all voices through VoiceManager (voice states updated by sequencer callbacks)
        finalVoiceOutput = voiceManager->processAllVoices();

        // Convert once for both channels (mono -> stereo)
        int16_t convertedSample = AudioSamples::toPcm16(finalVoiceOutput);
        out[2 * i + 0] = convertedSample; // Left channel
        out[2 * i + 1] = convertedSample; // Right channel
    }

    buffer->sample_count = N;
}

bool setupI2SAudio(audio_format_t *audioFormat, audio_i2s_config_t *i2sConfig)
{
    // Initialize I2S hardware with specified format
    audioPhase.store(AudioEngine::Phase::I2SDriver, std::memory_order_relaxed);
    if (!audio_i2s_setup(audioFormat, i2sConfig))
    {
        g_errorState |= ERR_AUDIO;
        return false;
    }

    // Identical PCM16 stereo formats: give the DMA the rendered buffer itself.
    // A zero consumer-buffer count selects the driver's pass-through connection.
    audioPhase.store(AudioEngine::Phase::I2SConnect, std::memory_order_relaxed);
    if (!audio_i2s_connect_extra(producer_pool, false, 0, SAMPLES_PER_BUFFER, nullptr))
    {
        g_errorState |= ERR_AUDIO;
        return false;
    }

    // Queue the full initial reserve before starting the clocks. Otherwise the
    // very first transfer necessarily underruns and substitutes silence.
    for (int i = 0; i < NUM_AUDIO_BUFFERS; ++i)
    {
        audioPhase.store(AudioEngine::Phase::InitialFill, std::memory_order_relaxed);
        audio_buffer_t *buffer = take_audio_buffer(producer_pool, false);
        if (!buffer)
        {
            g_errorState |= ERR_AUDIO;
            return false;
        }
        fill_audio_buffer(buffer);
        give_audio_buffer(producer_pool, buffer);
    }

    // Enable audio processing
    audioPhase.store(AudioEngine::Phase::I2SEnable, std::memory_order_relaxed);
    audio_i2s_set_enabled(true);
    g_audioOK = true;
    return true;
}
} // namespace

extern "C" void audio_i2s_debug_stage(uint32_t stage)
{
    driverStage.store(stage, std::memory_order_relaxed);
}

void AudioEngine::begin()
{
    // Core 0 performs all control/voice initialization first. It never waits
    // for audio during setup. A watchdog recovery boot leaves this flag low,
    // so Core 1 cannot restart a failing hardware path behind the console.
    while (!voicesReady.load(std::memory_order_acquire))


        // Wait until Core 0 has published the voice collection. Yield rather
        // than busy-spinning so initialization and other system work can run.
        delay(1);

    // Configure audio format (48kHz, 16-bit stereo)
    static audio_format_t audioFormat = {
        .sample_freq = static_cast<uint32_t>(SAMPLE_RATE),
        .format = AUDIO_BUFFER_FORMAT_PCM_S16,
        .channel_count = kStereoChannels};

    // Configure audio buffer format
    static audio_buffer_format_t bufferFormat = {
        .format = &audioFormat,
        .sample_stride = kStereoFrameBytes
    };

    // Create audio buffer pool
    audioPhase.store(Phase::BufferPool, std::memory_order_relaxed);
    producer_pool = audio_new_producer_pool(&bufferFormat, NUM_AUDIO_BUFFERS, SAMPLES_PER_BUFFER);

    // Configure I2S hardware interface
    audio_i2s_config_t i2sConfig = {
        .data_pin = PICO_AUDIO_I2S_DATA_PIN,
        .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
        .dma_channel = PICO_AUDIO_I2S_DMA_CHANNEL_AUTO,
        .pio_sm = PICO_AUDIO_I2S_PIO_SM_AUTO};

    // Initialize I2S audio system
    audioPhase.store(Phase::I2SSetup, std::memory_order_relaxed);
    audioStarted = setupI2SAudio(&audioFormat, &i2sConfig);
    if (!audioStarted)
        audioPhase.store(Phase::Failed, std::memory_order_relaxed);
}

void AudioEngine::renderNextBuffer()
{
    static uint32_t renderTotalUs = 0;
    static uint32_t renderCount = 0;
    static uint32_t renderMaxUs = 0;
    static uint32_t renderOverBudget = 0;
    if (!audioStarted)
        return;
    audioPhase.store(Phase::BufferWait, std::memory_order_relaxed);
    audio_buffer_t *audioBuffer = take_audio_buffer(producer_pool, true);

    if (audioBuffer)
    {
        audioPhase.store(Phase::Render, std::memory_order_relaxed);
        const uint32_t renderStart = micros();
        fill_audio_buffer(audioBuffer);
        const uint32_t renderUs = micros() - renderStart;
        renderTotalUs += renderUs;
        ++renderCount;
        if (renderUs > renderMaxUs) renderMaxUs = renderUs;
        if (renderUs > (SAMPLES_PER_BUFFER * 1000000u / 48000u)) ++renderOverBudget;
        audioPhase.store(Phase::Submit, std::memory_order_relaxed);
        give_audio_buffer(producer_pool, audioBuffer);
        completedBuffers.fetch_add(1, std::memory_order_relaxed);
    }

    // Keep the existing liveness probe, but hand its output to Core 0. A full
    // diagnostic queue drops the new report rather than delaying audio.
    static uint32_t c1BufCount = 0;
    static uint32_t c1LastBeat = 0;
    c1BufCount++;
    const uint32_t c1Now = millis();
    if (c1Now - c1LastBeat >= kHeartbeatIntervalMs)
    {
        c1LastBeat = c1Now;
        Heartbeat heartbeat{};
        heartbeat.bufferCount = c1BufCount;
        heartbeat.renderAverageUs = renderCount ? renderTotalUs / renderCount : 0;
        heartbeat.renderMaxUs = renderMaxUs;
        heartbeat.renderOverBudget = renderOverBudget;
        heartbeat.underruns = audio_i2s_underrun_count();
        heartbeat.txStalls = audio_i2s_tx_stall_count();
        if (voicesReady.load(std::memory_order_acquire))
        {
            for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i)
                heartbeat.voiceIds[i] = voiceSystem.getVoiceId(i);
        }
        heartbeats.tryPush(heartbeat);
        renderTotalUs = 0;
        renderCount = 0;
        renderMaxUs = 0;
    }
}

bool AudioEngine::takeHeartbeat(Heartbeat &heartbeat) noexcept
{
    return heartbeats.tryPop(heartbeat);
}

AudioEngine::Phase AudioEngine::phase() noexcept
{
    return audioPhase.load(std::memory_order_relaxed);
}

uint32_t AudioEngine::completedBufferCount() noexcept
{
    return completedBuffers.load(std::memory_order_relaxed);
}

uint32_t AudioEngine::driverSetupStage() noexcept
{
    return driverStage.load(std::memory_order_relaxed);
}

const char *AudioEngine::phaseName(Phase value) noexcept
{
    switch (value)
    {
    case Phase::NotStarted: return "not started";
    case Phase::BufferPool: return "buffer pool";
    case Phase::I2SSetup: return "I2S setup";
    case Phase::I2SDriver: return "I2S driver";
    case Phase::I2SConnect: return "I2S connect";
    case Phase::InitialFill: return "initial fill";
    case Phase::I2SEnable: return "I2S enable";
    case Phase::BufferWait: return "buffer wait";
    case Phase::Render: return "render";
    case Phase::Submit: return "submit";
    case Phase::Failed: return "setup failed";
    }
    return "unknown";
}
