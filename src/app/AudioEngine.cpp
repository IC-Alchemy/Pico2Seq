#include "AudioEngine.h"
#include "AppState.h"
#include "HardwarePins.h"
#include "../FeatureConfig.h"
#include "../audio/audio.h"
#include "../audio/audio_i2s.h"
#include <Arduino.h>
#include "../../diagnostic.h"
#include "Pcm16.h"
#include "../utils/SpscQueue.h"
#include <atomic>

static_assert(std::atomic<bool>::is_always_lock_free, "Audio readiness must not lock");
static_assert(sizeof(AudioEngine::Heartbeat::voiceIds) == VoiceSystem::MAX_VOICES,
              "Heartbeat must cover the fixed voice collection");

constexpr float SAMPLE_RATE = 48000.0f;
constexpr int NUM_AUDIO_BUFFERS = 3;
constexpr int SAMPLES_PER_BUFFER = 256;

#if PICO2SEQ_ENABLE_DELAY_EFFECT
#include "../rpdsp/src/rpdsp/delay_line.h"
extern constexpr size_t MAX_DELAY_SAMPLES = static_cast<size_t>(SAMPLE_RATE * 1.8f);
float delayTarget = 48000.0f * 0.15f;
float feedbackAmmount = 0.45f;
#endif

namespace
{
constexpr uint32_t kBootStabilizationMs = 100;
constexpr uint32_t kHeartbeatIntervalMs = 2000;
constexpr size_t kHeartbeatQueueCapacity = 4;
constexpr uint32_t kConsumerBufferCount = 4;
constexpr uint16_t kStereoChannels = 2;
constexpr uint16_t kStereoFrameBytes = kStereoChannels * sizeof(int16_t);
SpscQueue<AudioEngine::Heartbeat, kHeartbeatQueueCapacity> heartbeats;
audio_buffer_pool_t *producer_pool = nullptr;
#if PICO2SEQ_ENABLE_DELAY_EFFECT
constexpr float FEEDBACK_FADE_RATE = 0.01f;
rpdsp::StateVariableFilter delLowPass;
rpdsp::DelayLine<MAX_DELAY_SAMPLES> del1;
float currentDelayOutputGain = 0.0f;
float currentFeedbackGain = 0.0f;
float currentDelay = 48000.0f * 0.15f;
float delayTimeSmoothing(float currentDelay, float targetDelay, float slewRate)
{
    float difference = targetDelay - currentDelay;
    return currentDelay + (difference * slewRate);
}

float processDelayEffect(float inputSignal)
{
    // Read current delay output
    float delayOutput = del1.readLinear(currentDelay);

    // Calculate feedback signal with current gain
    float feedbackSignal = delayOutput * currentFeedbackGain;

    // Apply low-pass filtering to feedback to prevent harsh artifacts
    float filteredFeedback = delLowPass.process(feedbackSignal).lowpass;

    // Write to delay line: dry input + filtered feedback (clamped at 75%)
    del1.push(inputSignal + (filteredFeedback * 0.75f));

    // Mix dry and wet signals based on current delay output gain
    return inputSignal + (delayOutput * currentDelayOutputGain);
}
#endif

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

#if PICO2SEQ_ENABLE_DELAY_EFFECT
    // Determine target gains based on delay state
    float targetDelayOutputGain = uiState.delayOn ? 1.0f : 0.0f;
    float targetFeedbackGain = uiState.delayOn ? feedbackAmmount : 0.0f;

    // Smooth parameters once per buffer to reduce CPU load
    currentFeedbackGain = delayTimeSmoothing(currentFeedbackGain, targetFeedbackGain, FEEDBACK_FADE_RATE);
    currentDelayOutputGain = delayTimeSmoothing(currentDelayOutputGain, targetDelayOutputGain, FEEDBACK_FADE_RATE);
    currentDelay = delayTimeSmoothing(currentDelay, delayTarget, 0.0001f);
    // Delay time is applied per tap read (del1.readLinear in processDelayEffect);
    // rpdsp's DelayLine has no SetDelay — the old DaisySP call here was stale.
#endif // PICO2SEQ_ENABLE_DELAY_EFFECT

    // Process each sample in the buffer
    for (int i = 0; i < N; ++i)
    {
        // Process all voices through VoiceManager (voice states updated by sequencer callbacks)
        finalVoiceOutput = voiceManager->processAllVoices();

#if PICO2SEQ_ENABLE_DELAY_EFFECT
        // Apply global delay effect
        finalVoiceOutput = processDelayEffect(finalVoiceOutput);
#endif

        // Convert once for both channels (mono -> stereo)
        int16_t convertedSample = AudioSamples::toPcm16(finalVoiceOutput);
        out[2 * i + 0] = convertedSample; // Left channel
        out[2 * i + 1] = convertedSample; // Right channel
    }

    buffer->sample_count = N;
}

void setupI2SAudio(audio_format_t *audioFormat, audio_i2s_config_t *i2sConfig)
{
    // Initialize I2S hardware with specified format
    if (!audio_i2s_setup(audioFormat, i2sConfig))
    {
        g_errorState |= ERR_AUDIO;
        return;
    }

    // Connect audio buffer pool to I2S interface using 4 I2S consumer buffers
    if (!audio_i2s_connect_extra(producer_pool, false, kConsumerBufferCount, SAMPLES_PER_BUFFER, nullptr))
    {
        g_errorState |= ERR_AUDIO;
        return;
    }

    // Enable audio processing
    audio_i2s_set_enabled(true);
    g_audioOK = true;
}
} // namespace

void AudioEngine::prepareEffects()
{
#if PICO2SEQ_ENABLE_DELAY_EFFECT
    // Initialize global delay effect low-pass filter.
    // (rpdsp's SVF has no drive parameter; the old Svf drive is not carried over.)
    delLowPass.prepare(SAMPLE_RATE);
    delLowPass.setCutoff(1340.0f);  // Delay low-pass filter frequency
    delLowPass.setResonance(0.19f); // Filter resonance

    // Initialize delay line
    del1.reset(); // Clear any garbage in delay buffer

    // Start at 667 ms; the audio-owned delay time slews from its existing 150 ms.
    const float delayMs = 667.0f;
    size_t delaySamples = static_cast<size_t>(delayMs * SAMPLE_RATE * 0.001f);

    // Initialize delay target to match initial delay
    delayTarget = static_cast<float>(delaySamples);
#endif // PICO2SEQ_ENABLE_DELAY_EFFECT
}

void AudioEngine::begin()
{
    delay(kBootStabilizationMs);


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
    producer_pool = audio_new_producer_pool(&bufferFormat, NUM_AUDIO_BUFFERS, SAMPLES_PER_BUFFER);

    // Configure I2S hardware interface
    audio_i2s_config_t i2sConfig = {
        .data_pin = PICO_AUDIO_I2S_DATA_PIN,
        .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
        .dma_channel = 0,
        .pio_sm = 0};

    // Initialize I2S audio system
    setupI2SAudio(&audioFormat, &i2sConfig);
}

void AudioEngine::renderNextBuffer()
{
    audio_buffer_t *audioBuffer = take_audio_buffer(producer_pool, true);

    if (audioBuffer)
    {
        fill_audio_buffer(audioBuffer);
        give_audio_buffer(producer_pool, audioBuffer);
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
        Heartbeat heartbeat{c1BufCount, {}};
        if (voicesReady.load(std::memory_order_acquire))
        {
            for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i)
                heartbeat.voiceIds[i] = voiceSystem.getVoiceId(i);
        }
        heartbeats.tryPush(heartbeat);
    }
}

bool AudioEngine::takeHeartbeat(Heartbeat &heartbeat) noexcept
{
    return heartbeats.tryPop(heartbeat);
}
