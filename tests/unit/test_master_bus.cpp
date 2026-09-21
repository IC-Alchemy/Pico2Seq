#include <catch2/catch_test_macros.hpp>
#include "voice/VoiceManager.h"
#include "ui/ControlSurfaceLogic.h"
#include "scales/scales.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace {
constexpr float kSampleRate = 48000.0f;

VoiceConfig busPatch()
{
    VoiceConfig config;
    config.oscillatorCount = 1;
    config.oscWaveforms[0] = WAVE_SIN;
    config.hasEnvelope = false;
    config.hasFilter = false;
    return config;
}

VoiceState busNote()
{
    VoiceState state;
    state.noteIndex = 24.0f;
    state.velocityLevel = 1.0f;
    state.isGateHigh = true;
    state.shouldRetrigger = true;
    return state;
}

void configureCompressor(rpdsp::Compressor &comp, float macro)
{
    const auto settings = VoiceManager::settingsForMacro(macro);
    comp.prepare(kSampleRate);
    comp.setThresholdDb(settings.thresholdDb);
    comp.setRatio(settings.ratio);
    comp.setKneeWidthDb(settings.kneeDb);
    comp.setAttackRelease(settings.attackMs, settings.releaseMs);
    comp.setMakeupGainDb(settings.makeupDb);
    comp.reset();
}

float renderPeak(VoiceManager &manager, uint32_t count)
{
    std::array<float, 256> block{};
    float peak = 0.0f;
    while (count)
    {
        const uint32_t n = std::min<uint32_t>(count, block.size());
        manager.processBlock(block.data(), n);
        for (uint32_t i = 0; i < n; ++i)
        {
            REQUIRE(std::isfinite(block[i]));
            peak = std::max(peak, std::fabs(block[i]));
        }
        count -= n;
    }
    return peak;
}
} // namespace

TEST_CASE("Master bus compresses the delayed mix after master volume", "[master][master_bus]")
{
    // Compare the actual manager with a dry voice and standalone effects.
    // This catches a lost compressor, lost delay, or changed bus order.
    for (float macro : {0.0f, 0.5f, 1.0f})
    for (float mix : {0.0f, 1.0f})
    {
        CAPTURE(macro, mix);
        auto manager = std::make_unique<VoiceManager>(1);
        auto delay = std::make_unique<MasterDelay>();
        const uint8_t id = manager->addVoice(busPatch());
        Voice dry(id, busPatch());
        dry.setScaleTable(scale, SCALES_COUNT);
        dry.setCurrentScalePointer(&currentScale);
        manager->setGlobalVolume(0.6f);
        manager->setMasterMacro(macro);
        manager->setDelayMix(mix);
        manager->setDelayTime(0.010f);
        manager->init(kSampleRate);
        dry.init(kSampleRate);
        manager->updateVoiceState(id, busNote());
        dry.updateParameters(busNote());
        delay->prepare(kSampleRate);
        delay->setMix(mix);
        delay->setDelaySeconds(0.010f);
        delay->reset();
        rpdsp::Compressor compressor;
        configureCompressor(compressor, macro);

        constexpr std::array<uint32_t, 4> sizes{1, 32, 256, 513};
        std::array<float, 514> actual{};
        std::array<float, 513> source{};
        float error = 0.0f;
        float wetPeak = 0.0f;
        for (unsigned call = 0; call < 240; ++call)
        {
            const uint32_t n = sizes[call % sizes.size()];
            actual[n] = 99.0f;
            manager->processBlock(actual.data(), n);
            dry.processBlock(source.data(), n);
            for (uint32_t i = 0; i < n; ++i)
            {
                const float delayed = delay->process(source[i]);
                wetPeak = std::max(wetPeak, std::fabs(delayed - source[i]));
                const float expected = compressor.process(delayed * 0.6f);
                REQUIRE(std::isfinite(actual[i]));
                error = std::max(error, std::fabs(actual[i] - expected));
            }
            REQUIRE(actual[n] == 99.0f);
        }
        CHECK(error < 1e-6f);
        if (mix > 0.0f) CHECK(wetPeak > 0.01f);
        else CHECK(wetPeak == 0.0f); // dry bypass preserves the compressor path
    }
}

TEST_CASE("Delay tails survive silent voices and obey transport mute and volume", "[master][master_bus]")
{
    auto manager = std::make_unique<VoiceManager>(1);
    const auto id = manager->addVoice(busPatch());
    manager->setDelayMix(1.0f);
    manager->setDelayTime(0.05f);
    manager->init(kSampleRate);
    manager->updateVoiceState(id, busNote());
    REQUIRE(renderPeak(*manager, 48000) > 0.01f);
    manager->disableVoice(id);
    renderPeak(*manager, 512); // drain the queued enable update
    CHECK(renderPeak(*manager, 2400) > 0.01f);

    manager->setTransportMuted(true);
    renderPeak(*manager, 12000); // settle the 15 ms master gain smoother
    CHECK(renderPeak(*manager, 1024) < 1e-6f);
    manager->enableVoice(id);
    manager->setTransportMuted(false);
    REQUIRE(renderPeak(*manager, 48000) > 0.01f);
    manager->setGlobalVolume(0.0f);
    renderPeak(*manager, 12000);
    CHECK(renderPeak(*manager, 1024) < 1e-6f);
}

TEST_CASE("Delay fader time matches the audible range at 48 kHz", "[master][master_bus]")
{
    auto delay = std::make_unique<MasterDelay>();
    delay->prepare(kSampleRate);
    CHECK(ControlSurface::kDelayTimeMinSeconds == MasterDelay::kMinDelaySeconds);
    CHECK(ControlSurface::kDelayTimeMaxSeconds == delay->maxDelaySeconds());
    for (unsigned i = 0; i <= 100; ++i)
    {
        const float seconds = ControlSurface::delaySecondsForFader(i / 100.0f);
        delay->setDelaySeconds(seconds);
        CHECK(std::fabs(delay->delaySamplesTarget() / kSampleRate - seconds) < 1e-6f);
    }
}
