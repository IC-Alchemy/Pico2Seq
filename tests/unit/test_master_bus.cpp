#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "voice/VoiceManager.h"
#include "app/Pcm16.h"
#include "ui/ControlSurfaceLogic.h"
#include "scales/scales.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace {
using Catch::Approx;
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
        manager->setDelayFeedback(0.0f);
        manager->setDelayTime(0.010f);
        manager->init(kSampleRate);
        dry.init(kSampleRate);
        manager->updateVoiceState(id, busNote());
        dry.updateParameters(busNote());
        delay->prepare(kSampleRate);
        delay->setMix(mix);
        delay->setFeedback(0.0f);
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
            // Live target changes must reach the audio-owned delay without
            // resetting its tail or changing the compressor/mix controls.
            if (call == 80 || call == 160)
            {
                const float feedback = call == 80 ? 1.0f : 0.35f;
                manager->setDelayFeedback(feedback);
                delay->setFeedback(feedback);
                CHECK(manager->getMasterMacro() == macro);
                CHECK(manager->getDelayMix() == mix);
                CHECK(manager->getDelayTime() == 0.010f);
            }
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

TEST_CASE("Synced delay follows the live clock and preserves the millisecond setting", "[master][delay_sync]")
{
    VoiceManager manager(1);
    manager.setDelayTime(0.240f);
    manager.setDelayNoteIndex(DelayTiming::kDefaultNoteIndex);
    manager.setDelayTempoBpm(120.0f);
    CHECK(manager.getEffectiveDelayTime() == Approx(0.240f));
    manager.setDelaySynced(true);
    CHECK(manager.getEffectiveDelayTime() == Approx(0.5f));
    manager.setDelayTempoBpm(60.0f);
    CHECK(manager.getEffectiveDelayTime() == Approx(1.0f));
    manager.setDelayNoteIndex(0);
    CHECK(manager.getEffectiveDelayTime() == Approx(4.0f));
    manager.setDelaySynced(false);
    CHECK(manager.getEffectiveDelayTime() == Approx(0.240f));
}

// ---- Stereo bus: voices -> delay -> reverb -> shared gain -> linked compressor ------
namespace {
ReverbSettings busReverb(float mix)
{
    ReverbSettings settings;
    settings.mix = mix;
    settings.decaySeconds = 4.0f;
    settings.dampingHz = 2500.0f;
    settings.lowCutHz = 60.0f;
    settings.diffusion = 0.7f;
    settings.modDepth = 0.4f;
    settings.modRateHz = 1.0f;
    settings.width = 1.5f;
    return settings;
}

// A hand-built copy of the documented order. Every stage is the real component; only
// the wiring is repeated here, so a reordered or dropped stage in VoiceManager shows.
struct StereoRig
{
    std::unique_ptr<VoiceManager> manager = std::make_unique<VoiceManager>(1);
    std::unique_ptr<Voice> voice;
    std::unique_ptr<MasterDelay> delay = std::make_unique<MasterDelay>();
    std::unique_ptr<MasterReverb> reverb = std::make_unique<MasterReverb>();
    rpdsp::Compressor compressor;
    float volume = 0.6f;
    uint8_t id = 0;

    StereoRig(float macro, float delayMix, const ReverbSettings &settings, bool playNote = true)
    {
        id = manager->addVoice(busPatch());
        voice = std::make_unique<Voice>(id, busPatch());
        voice->setScaleTable(scale, SCALES_COUNT);
        voice->setCurrentScalePointer(&currentScale);
        manager->setGlobalVolume(volume);
        manager->setMasterMacro(macro);
        manager->setDelayMix(delayMix);
        manager->setDelayFeedback(0.4f);
        manager->setDelayTime(0.010f);
        manager->applyReverbSettings(settings);
        manager->init(kSampleRate);
        voice->init(kSampleRate);
        if (playNote)
        {
            manager->updateVoiceState(id, busNote());
            voice->updateParameters(busNote());
        }
        delay->prepare(kSampleRate);
        delay->setMix(delayMix);
        delay->setFeedback(0.4f);
        delay->setDelaySeconds(0.010f);
        delay->reset();
        reverb->publishSettings(settings);
        reverb->prepare(kSampleRate);
        configureCompressor(compressor, macro);
    }

    // The reference bus for n frames. `reverbFirst` builds the wrong order on purpose.
    void reference(uint32_t n, float *left, float *right, bool reverbFirst = false)
    {
        std::vector<float> source(n), staged(n), wl(n), wr(n);
        voice->processBlock(source.data(), n);
        if (reverbFirst)
        {
            reverb->render(source.data(), wl.data(), wr.data(), n);
            for (uint32_t k = 0; k < n; ++k)
            {
                // Reverb output is stereo; feed the delay its mono sum, as a wrong bus would.
                staged[k] = delay->process(0.5f * (wl[k] + wr[k]));
                wl[k] = wr[k] = staged[k];
            }
        }
        else
        {
            for (uint32_t k = 0; k < n; ++k)
                staged[k] = delay->process(source[k]);
            reverb->render(staged.data(), wl.data(), wr.data(), n);
        }
        for (uint32_t k = 0; k < n; ++k)
        {
            float l = wl[k] * volume;
            float r = wr[k] * volume;
            compressor.processStereo(l, r);
            left[k] = l;
            right[k] = r;
        }
    }
};

double maxDifference(const std::vector<float> &a, const std::vector<float> &b)
{
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::fabs(a[i] - b[i])));
    return worst;
}
} // namespace

TEST_CASE("Master bus order is voices, delay, reverb, shared gain, linked compressor", "[master][master_bus][reverb_bus]")
{
    for (float macro : {0.0f, 0.5f, 1.0f})
    {
        CAPTURE(macro);
        StereoRig rig(macro, 1.0f, busReverb(0.6f));
        StereoRig wrongOrder(macro, 1.0f, busReverb(0.6f));
        constexpr std::array<uint32_t, 6> sizes{1, 32, 256, 513, 64, 130};
        std::vector<float> actualL(513), actualR(513), refL(513), refR(513), wrongL(513), wrongR(513);
        std::vector<float> allActual, allRef, allWrong;
        for (unsigned call = 0; call < 120; ++call)
        {
            const uint32_t n = sizes[call % sizes.size()];
            rig.manager->processStereoBlock(actualL.data(), actualR.data(), n);
            rig.reference(n, refL.data(), refR.data());
            wrongOrder.reference(n, wrongL.data(), wrongR.data(), /*reverbFirst=*/true);
            for (uint32_t k = 0; k < n; ++k)
            {
                REQUIRE(std::isfinite(actualL[k]));
                REQUIRE(std::isfinite(actualR[k]));
                allActual.push_back(actualL[k]);
                allActual.push_back(actualR[k]);
                allRef.push_back(refL[k]);
                allRef.push_back(refR[k]);
                allWrong.push_back(wrongL[k]);
                allWrong.push_back(wrongR[k]);
            }
        }
        // Same components in the same order: agreement to float rounding.
        CHECK(maxDifference(allActual, allRef) < 1.0e-6);
        // Delay repeats feed the reverb (not the other way round): the swapped bus is
        // audibly different, so the check above is sensitive to the order.
        CHECK(maxDifference(allActual, allWrong) > 1.0e-3);
    }
}

TEST_CASE("Stereo bus at reverb mix zero is the legacy mono bus on both channels", "[master][master_bus][reverb_bus]")
{
    for (float macro : {0.0f, 0.5f, 1.0f})
    {
        CAPTURE(macro);
        auto manager = std::make_unique<VoiceManager>(1);
        auto mono = std::make_unique<VoiceManager>(1);
        auto delay = std::make_unique<MasterDelay>();
        for (auto *m : {manager.get(), mono.get()})
        {
            const uint8_t id = m->addVoice(busPatch());
            m->setGlobalVolume(0.6f);
            m->setMasterMacro(macro);
            m->setDelayMix(1.0f);
            m->setDelayFeedback(0.4f);
            m->setDelayTime(0.010f);
            m->init(kSampleRate);
            m->updateVoiceState(id, busNote());
        }
        Voice dry(1, busPatch());
        dry.setScaleTable(scale, SCALES_COUNT);
        dry.setCurrentScalePointer(&currentScale);
        dry.init(kSampleRate);
        dry.updateParameters(busNote());
        delay->prepare(kSampleRate);
        delay->setMix(1.0f);
        delay->setFeedback(0.4f);
        delay->setDelaySeconds(0.010f);
        delay->reset();
        rpdsp::Compressor legacy;
        configureCompressor(legacy, macro);
        REQUIRE(manager->getReverbSettings().mix == 0.0f);

        std::vector<float> left(300), right(300), monoOut(300), source(300);
        for (unsigned call = 0; call < 160; ++call)
        {
            const uint32_t n = 1 + (call * 53) % 300;
            manager->processStereoBlock(left.data(), right.data(), n);
            mono->processBlock(monoOut.data(), n);
            dry.processBlock(source.data(), n);
            for (uint32_t k = 0; k < n; ++k)
            {
                // The previous bus, verbatim: delay -> master volume -> mono compressor.
                const float expected = legacy.process(delay->process(source[k]) * 0.6f);
                REQUIRE(std::fabs(left[k] - expected) < 1.0e-6f);
                REQUIRE(left[k] == right[k]);
                REQUIRE(monoOut[k] == left[k]);
                REQUIRE(AudioSamples::toPcm16(left[k]) == AudioSamples::toPcm16(expected));
                REQUIRE(AudioSamples::toPcm16(right[k]) == AudioSamples::toPcm16(expected));
            }
        }
    }
}

TEST_CASE("Wet channels are distinct and share one compressor gain", "[master][master_bus][reverb_bus]")
{
    StereoRig rig(1.0f, 0.0f, busReverb(1.0f)); // Punch: the strongest gain reduction
    std::vector<float> l(1024), r(1024), preL(1024), preR(1024), refL(1024), refR(1024);
    double difference = 0.0, energy = 0.0;
    unsigned compared = 0;
    float lowestGain = 1.0e9f, highestGain = 0.0f;
    for (unsigned call = 0; call < 90; ++call)
    {
        rig.manager->processStereoBlock(l.data(), r.data(), 1024);
        // The same wiring without the compressor: what the gain is applied to.
        std::vector<float> source(1024), wl(1024), wr(1024);
        rig.voice->processBlock(source.data(), 1024);
        std::vector<float> delayed(1024);
        for (unsigned k = 0; k < 1024; ++k) delayed[k] = rig.delay->process(source[k]);
        rig.reverb->render(delayed.data(), wl.data(), wr.data(), 1024);
        for (unsigned k = 0; k < 1024; ++k)
        {
            preL[k] = wl[k] * 0.6f;
            preR[k] = wr[k] * 0.6f;
            if (call < 4) continue; // let the tank and detector build
            difference += std::fabs(l[k] - r[k]);
            energy += std::fabs(l[k]) + std::fabs(r[k]);
            if (std::fabs(preL[k]) > 1.0e-2f && std::fabs(preR[k]) > 1.0e-2f)
            {
                const float gainL = l[k] / preL[k];
                const float gainR = r[k] / preR[k];
                REQUIRE(std::fabs(gainL - gainR) <= 1.0e-4f * std::fabs(gainL));
                lowestGain = std::min(lowestGain, gainL);
                highestGain = std::max(highestGain, gainL);
                ++compared;
            }
        }
    }
    CHECK(compared > 10000);
    CHECK(difference / energy > 0.05); // width 1.5: the wet channels differ substantially
    CHECK(highestGain > lowestGain);   // and the compressor was actually moving
}

TEST_CASE("Reverb tail survives silent voices and obeys transport mute and volume", "[master][master_bus][reverb_bus]")
{
    struct Bus
    {
        std::unique_ptr<VoiceManager> manager = std::make_unique<VoiceManager>(1);
        uint8_t id = 0;
        std::vector<float> left = std::vector<float>(512), right = std::vector<float>(512);
        Bus()
        {
            id = manager->addVoice(busPatch());
            ReverbSettings settings = busReverb(1.0f);
            settings.decaySeconds = 20.0f;
            manager->applyReverbSettings(settings);
            manager->init(kSampleRate);
            manager->updateVoiceState(id, busNote());
        }
        // Renders `frames`; returns the peak and, through rms, the RMS of the left channel.
        float render(uint32_t frames, double *rms = nullptr, double *maxStep = nullptr)
        {
            float peak = 0.0f;
            double sum = 0.0, step = 0.0;
            uint32_t count = 0;
            float previous = 0.0f;
            while (frames)
            {
                const uint32_t n = std::min<uint32_t>(frames, 512);
                manager->processStereoBlock(left.data(), right.data(), n);
                for (uint32_t i = 0; i < n; ++i)
                {
                    REQUIRE(std::isfinite(left[i]));
                    REQUIRE(std::isfinite(right[i]));
                    peak = std::max(peak, std::max(std::fabs(left[i]), std::fabs(right[i])));
                    sum += static_cast<double>(left[i]) * left[i];
                    if (count) step = std::max(step, static_cast<double>(std::fabs(left[i] - previous)));
                    previous = left[i];
                    ++count;
                }
                frames -= n;
            }
            if (rms) *rms = count ? std::sqrt(sum / count) : 0.0;
            if (maxStep) *maxStep = step;
            return peak;
        }
    };
    Bus muted, twin; // identical buses; only `muted` is ever muted
    for (Bus *bus : {&muted, &twin})
    {
        REQUIRE(bus->render(48000) > 0.01f);
        bus->manager->disableVoice(bus->id);
        bus->render(2400); // drain the queued disable
        bus->render(24000);
    }
    double rmsBefore = 0.0, naturalStep = 0.0;
    twin.render(480, &rmsBefore, &naturalStep);
    muted.render(480);
    CHECK(rmsBefore > 1.0e-3); // the tank rings on with the voice silent

    // Mute glides (15 ms), it does not cut.
    muted.manager->setTransportMuted(true);
    double muteStep = 0.0;
    muted.render(480, nullptr, &muteStep);
    twin.render(480);
    CHECK(muteStep <= 1.5 * naturalStep + 0.01);
    muted.render(24000); // settle the smoother
    twin.render(24000);
    double silentRms = 1.0;
    const float silentPeak = muted.render(1024, &silentRms);
    twin.render(1024);
    CHECK(silentPeak < 1.0e-6f);

    // The tank keeps evolving while muted: after unmuting, the tail is the one an
    // unmuted bus has at the same moment (the compressor re-settles within ~100 ms).
    muted.render(48000);
    twin.render(48000);
    muted.manager->setTransportMuted(false);
    muted.render(16000);
    twin.render(16000);
    double rmsMuted = 0.0, rmsTwin = 0.0;
    muted.render(4800, &rmsMuted);
    twin.render(4800, &rmsTwin);
    CHECK(rmsTwin > 1.0e-3);
    CHECK(rmsMuted == Catch::Approx(rmsTwin).epsilon(0.05));

    // Volume zero silences both channels; restoring it brings the same tail back.
    muted.manager->setGlobalVolume(0.0f);
    muted.render(24000);
    double zeroRms = 1.0;
    muted.render(1024, &zeroRms);
    CHECK(zeroRms < 1.0e-6);
    muted.manager->setGlobalVolume(0.6f);
    muted.render(16000);
    double rmsBack = 0.0;
    muted.render(4800, &rmsBack);
    CHECK(rmsBack > 1.0e-3);
}

TEST_CASE("Odd, empty, oversized and overlapping blocks all give the same bus", "[master][master_bus][reverb_bus]")
{
    const auto build = [] {
        auto manager = std::make_unique<VoiceManager>(1);
        const auto id = manager->addVoice(busPatch());
        manager->setGlobalVolume(0.6f);
        manager->setDelayMix(0.5f);
        manager->setDelayTime(0.02f);
        manager->applyReverbSettings(busReverb(0.7f));
        manager->init(kSampleRate);
        manager->updateVoiceState(id, busNote());
        return manager;
    };
    constexpr uint32_t total = 6000;
    auto whole = build();
    std::vector<float> wholeL(total), wholeR(total);
    whole->processStereoBlock(wholeL.data(), wholeR.data(), total); // oversized: chunked internally

    auto split = build();
    std::vector<float> splitL(total, 99.0f), splitR(total, 99.0f);
    constexpr std::array<uint32_t, 10> sizes{1, 7, 255, 256, 257, 513, 1000, 3, 64, 0};
    uint32_t at = 0;
    for (unsigned call = 0; at < total; ++call)
    {
        const uint32_t n = std::min(sizes[call % sizes.size()], total - at);
        split->processStereoBlock(splitL.data() + at, splitR.data() + at, n);
        at += n;
    }
    // IEEE builds compare bit for bit. A -ffast-math build may contract or reassociate
    // the block and per-sample reverb loops differently (rpdsp's own tests allow the same
    // slack), so it compares within 2e-4 relative; GCC 13.3 x86-64 was bit-exact as well.
    for (uint32_t i = 0; i < total; ++i)
    {
#ifdef __FAST_MATH__
        REQUIRE(std::fabs(wholeL[i] - splitL[i]) <= 2.0e-4f * (1.0f + std::fabs(wholeL[i])));
        REQUIRE(std::fabs(wholeR[i] - splitR[i]) <= 2.0e-4f * (1.0f + std::fabs(wholeR[i])));
#else
        REQUIRE(std::memcmp(&wholeL[i], &splitL[i], sizeof(float)) == 0);
        REQUIRE(std::memcmp(&wholeR[i], &splitR[i], sizeof(float)) == 0);
#endif
    }

    // Zero-length calls touch nothing, even with null pointers.
    std::array<float, 4> sentinel{7, 7, 7, 7};
    build()->processStereoBlock(sentinel.data(), sentinel.data() + 2, 0);
    for (float value : sentinel) CHECK(value == 7.0f);
    build()->processStereoBlock(nullptr, nullptr, 0);

    // Overlapping or missing right channels degrade to the mono downmix.
    auto viaAlias = build();
    auto viaNull = build();
    auto viaMono = build();
    std::vector<float> a(700), b(700), c(700);
    viaAlias->processStereoBlock(a.data(), a.data(), 700);
    viaNull->processStereoBlock(b.data(), nullptr, 700);
    viaMono->processBlock(c.data(), 700);
    for (uint32_t i = 0; i < 700; ++i)
    {
        REQUIRE(a[i] == c[i]);
        REQUIRE(b[i] == c[i]);
        // The mono wrapper is the downmix of the stereo bus.
        REQUIRE(std::fabs(c[i] - 0.5f * (wholeL[i] + wholeR[i])) < 1.0e-6f);
    }
}

TEST_CASE("init() clears the reverb tail and keeps the published reverb targets", "[master][master_bus][reverb_bus]")
{
    auto manager = std::make_unique<VoiceManager>(1);
    const auto id = manager->addVoice(busPatch());
    manager->setReverbMix(0.75f);
    manager->setReverbDecaySeconds(9.0f);
    manager->init(kSampleRate);
    CHECK(manager->getReverbSettings().mix == 0.75f);
    CHECK(manager->getReverbSettings().decaySeconds == 9.0f);
    manager->updateVoiceState(id, busNote());
    float peak = 0.0f;
    std::vector<float> left(512), right(512);
    for (unsigned i = 0; i < 90; ++i)
    {
        manager->processStereoBlock(left.data(), right.data(), 512);
        for (float v : left) peak = std::max(peak, std::fabs(v));
    }
    REQUIRE(peak > 0.01f);
    manager->disableVoice(id);
    manager->init(kSampleRate); // setup-time: wipes the tank and the delay
    manager->processStereoBlock(left.data(), right.data(), 512); // drain the queued disable
    for (unsigned i = 0; i < 20; ++i)
    {
        manager->processStereoBlock(left.data(), right.data(), 512);
        for (uint32_t k = 0; k < 512; ++k)
        {
            REQUIRE(left[k] == 0.0f);
            REQUIRE(right[k] == 0.0f);
        }
    }
    CHECK(manager->getReverbSettings().mix == 0.75f);
}
