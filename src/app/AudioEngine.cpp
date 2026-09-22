#include "AudioEngine.h"
#include "../utils/AudioRam.h"
#include "AppState.h"
#include "HardwarePins.h"
#include "../audio/audio.h"
#include "../audio/audio_i2s.h"
#include <Arduino.h>
#include "../../diagnostic.h"
#include "Pcm16.h"
#include "../utils/SpscQueue.h"
#include <atomic>

// Core 1 render path: fill buffers from the published voices, duplicate mono to
// stereo, and track timing so Core 0 can print dropouts without touching audio.

static_assert(std::atomic<bool>::is_always_lock_free, "Audio readiness must not lock");
static_assert(std::atomic<AudioEngine::Phase>::is_always_lock_free, "Audio diagnostics must not lock");
static_assert(sizeof(AudioEngine::Heartbeat::voiceIds) == VoiceSystem::MAX_VOICES,
              "Heartbeat must cover the fixed voice collection");

constexpr float SAMPLE_RATE = 48000.0f;
// Four buffers = three queued + one with DMA: the groove survives one late render
// without a click, and no extra consumer copy is needed in the DMA IRQ.
constexpr int NUM_AUDIO_BUFFERS = 4;
constexpr int SAMPLES_PER_BUFFER = 256;
static_assert(SAMPLES_PER_BUFFER <= static_cast<int>(VoiceManager::kMaxBlock));

namespace
{
constexpr uint32_t kHeartbeatIntervalMs = 2000;
constexpr size_t kHeartbeatQueueCapacity = 4;
constexpr uint16_t kStereoChannels = 2;
constexpr uint16_t kStereoFrameBytes = kStereoChannels * sizeof(int16_t);
std::atomic<AudioEngine::Phase> audioPhase{AudioEngine::Phase::NotStarted};
std::atomic<uint32_t> completedBuffers{0};
std::atomic<uint32_t> driverStage{0};
bool audioStarted = false; // Core 1 only; never read blindly from Core 0
SpscQueue<AudioEngine::Heartbeat, kHeartbeatQueueCapacity> heartbeats;
audio_buffer_pool_t *producer_pool = nullptr;
std::array<float, SAMPLES_PER_BUFFER> mixBuffer{}; // Core 1 render scratch (2 KiB stack limit)
VoiceManager::StageProfile lastStageProfile{};

void PICO2SEQ_AUDIO_FUNC(fill_audio_buffer)(audio_buffer_t *buffer)
{
    int N = buffer->max_sample_count;
    int16_t *out = reinterpret_cast<int16_t *>(buffer->buffer->bytes);

    if (!voicesReady.load(std::memory_order_acquire))
    {
        // Core 0 still building voices: output silence rather than racing it.
        // Acquire pairs with Core 0's publish before the manager is even read.
        for (int i = 0; i < N; ++i)
        {
            out[2 * i + 0] = 0;
            out[2 * i + 1] = 0;
        }
        buffer->sample_count = N;
        return;
    }

    // Fixed scratch bounds even an oversized producer buffer; chunking keeps
    // each VoiceManager block within kMaxBlock.
    lastStageProfile = {};
    for (int offset = 0; offset < N; offset += SAMPLES_PER_BUFFER)
    {
        const int count = std::min(N - offset, SAMPLES_PER_BUFFER);
        voiceManager->processBlock(mixBuffer.data(), static_cast<uint32_t>(count));
        const VoiceManager::StageProfile chunkProfile = voiceManager->lastStageProfile();
        lastStageProfile.voicesUs += chunkProfile.voicesUs;
        lastStageProfile.delayUs += chunkProfile.delayUs;
        lastStageProfile.compressorUs += chunkProfile.compressorUs;
        for (int i = 0; i < count; ++i)
        {
            const int16_t sample = AudioSamples::toPcm16(mixBuffer[i]);
            out[2 * (offset + i)] = sample;
            out[2 * (offset + i) + 1] = sample;
        }
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

    // Identical PCM16 stereo formats, so DMA takes the rendered buffer directly.
    // Zero consumer buffers selects the driver's pass-through connection.
    audioPhase.store(AudioEngine::Phase::I2SConnect, std::memory_order_relaxed);
    if (!audio_i2s_connect_extra(producer_pool, false, 0, SAMPLES_PER_BUFFER, nullptr))
    {
        g_errorState |= ERR_AUDIO;
        return false;
    }

    // Pre-fill the full reserve before clocks start; otherwise the first DMA
    // transfer underruns and the downbeat starts with silence.
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
    // Core 0 owns control/voice setup and never waits for audio. A watchdog
    // recovery boot leaves voicesReady low so Core 1 parks instead of reviving
    // a failing hardware path behind the console.
    while (!voicesReady.load(std::memory_order_acquire))


        // Wait for Core 0's voice publish. Yield so system work can proceed.
        delay(1);

    // 48 kHz 16-bit stereo; stride is one L+R frame (4 bytes).
    static audio_format_t audioFormat = {
        .sample_freq = static_cast<uint32_t>(SAMPLE_RATE),
        .format = AUDIO_BUFFER_FORMAT_PCM_S16,
        .channel_count = kStereoChannels};

    // Configure audio buffer format
    static audio_buffer_format_t bufferFormat = {
        .format = &audioFormat,
        .sample_stride = kStereoFrameBytes
    };

    // Claim pool + I2S channel/SM via AUTO so coexisting DMA/PIO users survive.
    audioPhase.store(Phase::BufferPool, std::memory_order_relaxed);
    producer_pool = audio_new_producer_pool(&bufferFormat, NUM_AUDIO_BUFFERS, SAMPLES_PER_BUFFER);

    // I2S pins/channel/SM (see HardwarePins.h).
    audio_i2s_config_t i2sConfig = {
        .data_pin = PICO_AUDIO_I2S_DATA_PIN,
        .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
        .dma_channel = PICO_AUDIO_I2S_DMA_CHANNEL_AUTO,
        .pio_sm = PICO_AUDIO_I2S_PIO_SM_AUTO};

    // Bring up the I2S driver.
    audioPhase.store(Phase::I2SSetup, std::memory_order_relaxed);
    audioStarted = setupI2SAudio(&audioFormat, &i2sConfig);
    if (!audioStarted)
        audioPhase.store(Phase::Failed, std::memory_order_relaxed);
}

void PICO2SEQ_AUDIO_FUNC(AudioEngine::renderNextBuffer)()
{
    static uint32_t renderTotalUs = 0;
    static uint32_t renderCount = 0;
    static uint32_t renderMaxUs = 0;
    static uint32_t renderOverBudget = 0;
    static uint32_t voicesTotalUs = 0;
    static uint32_t delayTotalUs = 0;
    static uint32_t compressorTotalUs = 0;
    static uint32_t miscTotalUs = 0;
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
        voicesTotalUs += lastStageProfile.voicesUs;
        delayTotalUs += lastStageProfile.delayUs;
        compressorTotalUs += lastStageProfile.compressorUs;
        const uint32_t measuredStages = lastStageProfile.voicesUs + lastStageProfile.delayUs +
                                        lastStageProfile.compressorUs;
        miscTotalUs += renderUs > measuredStages ? renderUs - measuredStages : 0;
        ++renderCount;
        if (renderUs > renderMaxUs) renderMaxUs = renderUs;
        if (renderUs > (SAMPLES_PER_BUFFER * 1000000u / 48000u)) ++renderOverBudget;
        audioPhase.store(Phase::Submit, std::memory_order_relaxed);
        give_audio_buffer(producer_pool, audioBuffer);
        completedBuffers.fetch_add(1, std::memory_order_relaxed);
    }

    // Liveness probe for Core 0's diagnostics. A full queue drops the report
    // rather than stalling audio; loss here only thins the log.
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
        heartbeat.voicesAverageUs = renderCount ? voicesTotalUs / renderCount : 0;
        heartbeat.delayAverageUs = renderCount ? delayTotalUs / renderCount : 0;
        heartbeat.compressorAverageUs = renderCount ? compressorTotalUs / renderCount : 0;
        heartbeat.miscAverageUs = renderCount ? miscTotalUs / renderCount : 0;
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
        voicesTotalUs = delayTotalUs = compressorTotalUs = miscTotalUs = 0;
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
