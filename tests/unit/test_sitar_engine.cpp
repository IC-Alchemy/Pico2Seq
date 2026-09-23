// ENGINE_SITAR integration tests: the rpdsp::SitarStringVoice model wired into
// Voice's engine dispatch (note-on pluck, natural tails, staged controls,
// meend slide routing, idle-skip carve-out). The model itself is covered by
// test_sitar_voice.cpp ([rpdsp][sitar]); this file tests the Voice layer.
#include <catch2/catch_test_macros.hpp>
#include "voice/Voice.h"
#include "voice/VoicePresets.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr float kSampleRate = 48000.0f;

// A raw sitar engine config (the factory preset is a later task). Strip the
// shared chain back so measurements see the model output directly.
VoiceConfig sitarConfig()
{
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_SITAR;
    c.hasEnvelope = false; // natural string/jawari/body decay instead of a VCA
    c.hasFilter = false;
    c.highPassFreq = 0.0f;
    c.highPassRes = 0.0f;
    c.outputLevel = 1.0f;
    c.sitarDecay = 0.5f;       // short T60: natural fall to silence inside test renders
    c.sitarTarafAmount = 0.0f; // string-only: fast, deterministic fall to silence
    return c;
}

VoiceState gatedState(float noteIndex, float velocity = 0.8f)
{
    VoiceState s{};
    s.noteIndex = noteIndex;
    s.velocityLevel = velocity;
    s.filterCutoff = 0.37f;
    s.attackTimeSeconds = 0.01f;
    s.decayTimeSeconds = 0.1f;
    s.octaveOffset = 0;
    s.isGateHigh = true;
    s.hasSlide = false;
    s.shouldRetrigger = false;
    return s;
}

// Goertzel bin magnitude for "which pitch dominates this window" checks.
double goertzelMagnitude(const float *samples, size_t count, float hz, float fs)
{
    const double k = 2.0 * std::cos(2.0 * 3.14159265358979323846 *
                                    static_cast<double>(hz) / static_cast<double>(fs));
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        const double s0 = static_cast<double>(samples[i]) + k * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - k * s1 * s2;
    return std::sqrt(std::max(0.0, power)) / static_cast<double>(count) * 2.0;
}

double windowRms(const float *samples, size_t count)
{
    double energy = 0.0;
    for (size_t i = 0; i < count; ++i) energy += static_cast<double>(samples[i]) * samples[i];
    return std::sqrt(energy / static_cast<double>(count));
}
} // namespace

TEST_CASE("Sitar engine lifecycle: pluck rings past gate-off then settles silent",
          "[sitar_engine]")
{
    auto config = sitarConfig();
    Voice voice(0, config);
    voice.init(kSampleRate);
    voice.updateParameters(gatedState(9.0f)); // note 9 -> 880 Hz base pitch
    float peak = 0.0f;
    for (int i = 0; i < 9600; ++i)
    {
        const float s = voice.process();
        REQUIRE(std::isfinite(s));
        peak = std::max(peak, std::abs(s));
    }
    REQUIRE(peak > 0.01f); // note-on speaks

    // hasEnvelope == false: gate-off must NOT silence the string; the tail
    // keeps ringing through the shared chain (env stage stays at 1.0).
    voice.setGate(false);
    std::array<float, 24000> tail{};
    for (size_t i = 0; i < tail.size(); ++i)
    {
        const float s = voice.process();
        REQUIRE(std::isfinite(s));
        tail[i] = s;
    }
    REQUIRE(windowRms(tail.data(), tail.size()) > 0.0006);

    // The natural decay then reaches exact silence and stays there: the
    // engine reports inactive and renders exact zeros (idle-skip never
    // engages for hasEnvelope == false, so this is the live engine path).
    for (int i = 0; i < 144000; ++i) (void)voice.process(); // 3 s of decay
    for (int i = 0; i < 4800; ++i) REQUIRE(voice.process() == 0.0f);

    // A gate-on re-plucks.
    voice.setGate(true);
    peak = 0.0f;
    for (int i = 0; i < 4800; ++i)
    {
        const float s = voice.process();
        REQUIRE(std::isfinite(s));
        peak = std::max(peak, std::abs(s));
    }
    REQUIRE(peak > 0.01f);
}

TEST_CASE("Sitar retriggers re-excite cleanly while gated", "[sitar_engine]")
{
    auto config = sitarConfig();
    Voice voice(0, config);
    voice.init(kSampleRate);
    auto state = gatedState(5.0f);
    voice.updateParameters(state);
    for (unsigned retrigger = 0; retrigger < 8; ++retrigger)
    {
        state.shouldRetrigger = true;
        voice.updateParameters(state);
        state.shouldRetrigger = false;
        float peak = 0.0f;
        for (int i = 0; i < 2400; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
            peak = std::max(peak, std::abs(s));
        }
        INFO("retrigger " << retrigger);
        REQUIRE(peak > 0.01f);  // each re-pluck speaks again
        REQUIRE(peak < 8.0f);   // and the loop stays bounded
    }
    REQUIRE(voice.getState().shouldRetrigger == false);
}

TEST_CASE("Repeated sitar controls preserve the sounding tail", "[sitar_engine]")
{
    const auto config = sitarConfig();
    Voice repeated(0, config), unchanged(0, config);
    repeated.init(kSampleRate);
    unchanged.init(kSampleRate);
    const auto state = gatedState(9.0f);
    repeated.updateParameters(state);
    unchanged.updateParameters(state);
    float peak = 0.0f;
    for (int sample = 0; sample < 8192; ++sample)
    {
        if (sample > 0 && sample % 32 == 0) repeated.updateParameters(state);
        const float actual = repeated.process();
        REQUIRE(actual == unchanged.process()); // staged applies are invisible
        peak = std::max(peak, std::abs(actual));
    }
    REQUIRE(peak > 0.01f);
}

TEST_CASE("Sitar controls stage through the queue and shape the voice",
          "[sitar_engine]")
{
    SECTION("Jawari changes the emitted waveform")
    {
        const auto base = sitarConfig();
        Voice clean(0, base), buzz(0, base);
        clean.init(kSampleRate);
        buzz.init(kSampleRate);
        VoiceConfig buzzing = base;
        buzzing.sitarJawari = 0.9f;
        buzz.setConfig(buzzing); // queued; the first process() drains it
        (void)buzz.process();
        REQUIRE(buzz.getConfig().sitarJawari == 0.9f);
        const auto state = gatedState(9.0f);
        clean.updateParameters(state);
        buzz.updateParameters(state);
        double difference = 0.0;
        for (int i = 0; i < 4096; ++i)
        {
            const float a = clean.process(), b = buzz.process();
            REQUIRE(std::isfinite(a));
            REQUIRE(std::isfinite(b));
            difference += std::abs(a - b);
        }
        REQUIRE(difference / 4096 > 0.001);
    }

    SECTION("Decay time changes the tail length")
    {
        const auto base = sitarConfig();
        VoiceConfig fastConfig = base;
        fastConfig.sitarDecay = 0.3f;
        VoiceConfig slowConfig = base;
        slowConfig.sitarDecay = 2.0f;
        Voice fast(0, fastConfig), slow(0, slowConfig);
        fast.init(kSampleRate);
        slow.init(kSampleRate);
        const auto state = gatedState(9.0f);
        fast.updateParameters(state);
        slow.updateParameters(state);
        // Late window (0.5-0.75 s after the pluck): the long-T60 string is
        // still ringing, the short-T60 string has finished its natural decay.
        std::array<float, 12000> fastRender{}, slowRender{};
        for (int i = 0; i < 24000; ++i)
        {
            (void)fast.process();
            (void)slow.process();
        }
        for (size_t i = 0; i < fastRender.size(); ++i)
        {
            fastRender[i] = fast.process();
            slowRender[i] = slow.process();
        }
        const double fastRms = windowRms(fastRender.data(), fastRender.size());
        const double slowRms = windowRms(slowRender.data(), slowRender.size());
        INFO("fastRms " << fastRms << " slowRms " << slowRms);
        REQUIRE(slowRms > 0.0005);
        REQUIRE(slowRms > 20.0 * (fastRms + 1.0e-6));
    }
}

TEST_CASE("Sitar settings survive engine resets and sample rate changes",
          "[sitar_engine]")
{
    bool switchEngine = false;
    float sampleRate = kSampleRate;
    SECTION("Switch away and back to the same string settings") { switchEngine = true; }
    SECTION("Prepare again at the same sample rate") {}
    SECTION("Prepare at a different sample rate") { sampleRate = 96000.0f; }

    const auto config = sitarConfig();
    Voice reused(0, config);
    reused.init(kSampleRate);
    const auto state = gatedState(9.0f);
    reused.updateParameters(state);
    for (int sample = 0; sample < 512; ++sample) reused.process();
    reused.setGate(false);
    reused.process();

    if (switchEngine)
    {
        reused.setConfig(VoicePresets::getPresetConfigByName("FMGlass"));
        reused.process();
        reused.setConfig(config);
        reused.process();
    }
    else
    {
        reused.init(sampleRate);
    }

    Voice fresh(0, config);
    fresh.init(sampleRate);
    reused.updateParameters(state);
    fresh.updateParameters(state);
    float peak = 0.0f;
    for (int sample = 0; sample < 4096; ++sample)
    {
        const float actual = reused.process();
        REQUIRE(actual == fresh.process());
        peak = std::max(peak, std::abs(actual));
    }
    REQUIRE(peak > 0.01f);
}

TEST_CASE("Ringing sitar string bends smoothly to a repitched target",
          "[sitar_engine]")
{
    auto config = sitarConfig();
    config.sitarDecay = 8.0f;  // keep the string ringing through the bend
    config.sitarJawari = 0.1f; // near-pure string for clean pitch reads
    Voice voice(0, config);
    voice.init(kSampleRate);
    voice.setSlideTime(0.8f); // slow meend; SlideChanged must reach the model
    (void)voice.process();    // drain the queued slide change
    voice.updateParameters(gatedState(9.0f)); // pluck at 880 Hz
    for (int i = 0; i < 4800; ++i) (void)voice.process();

    voice.setFrequency(440.0f); // pitch change while gated -> meend bend
    float maxDelta = 0.0f;
    auto trackDelta = [&maxDelta](float previous, float current)
    { maxDelta = std::max(maxDelta, std::abs(current - previous)); };

    // Early window (0.025-0.075 s in): a 0.8 s bend is barely under way, so
    // the string must still sound near the old pitch (no instant retune), and
    // the waveform must stay continuous throughout.
    std::array<float, 2400> early{};
    float previous = voice.process();
    for (int i = 0; i < 4800; ++i)
    {
        const float s = voice.process();
        trackDelta(previous, s);
        previous = s;
        if (i >= 1200 && i < 3600) early[i - 1200] = s; // [0.025..0.075] s
    }
    for (int i = 0; i < 33600; ++i) // slide out to 0.8 s post-set (~4 time constants)
    {
        const float s = voice.process();
        trackDelta(previous, s);
        previous = s;
    }
    std::array<float, 4800> settled{};
    settled[0] = previous;
    for (size_t i = 1; i < settled.size(); ++i)
    {
        const float s = voice.process();
        trackDelta(settled[i - 1], s);
        settled[i] = s;
    }
    REQUIRE(maxDelta < 0.4f); // continuity: no sample-to-sample glitch

    const double oldPitch = goertzelMagnitude(early.data(), early.size(), 880.0f, kSampleRate);
    const double targetEarly = goertzelMagnitude(early.data(), early.size(), 440.0f, kSampleRate);
    INFO("early old-pitch bin " << oldPitch << " target bin " << targetEarly);
    REQUIRE(oldPitch > targetEarly); // the bend starts from the old pitch

    const double landed = goertzelMagnitude(settled.data(), settled.size(), 440.0f, kSampleRate);
    const double residue = goertzelMagnitude(settled.data(), settled.size(), 880.0f, kSampleRate);
    INFO("settled target bin " << landed << " residue bin " << residue);
    REQUIRE(landed > 2.0 * residue); // and it lands on the new pitch
}

TEST_CASE("Over-unity velocity cannot blow up the sitar pluck", "[sitar_engine]")
{
    auto config = sitarConfig();
    Voice voice(0, config);
    voice.init(kSampleRate);
    voice.updateParameters(gatedState(9.0f, 2.5f)); // out-of-range velocity
    float peak = 0.0f;
    for (int i = 0; i < 9600; ++i)
    {
        const float s = voice.process();
        REQUIRE(std::isfinite(s));
        peak = std::max(peak, std::abs(s));
    }
    REQUIRE(peak > 0.01f);
    REQUIRE(peak < 8.0f); // pluck amplitude is clamped, so excitation cannot run away
}

TEST_CASE("Sitar tail outlives a released envelope under the idle-skip guard",
          "[sitar_engine]")
{
    auto config = sitarConfig();
    config.hasEnvelope = true; // the skip logic only ever engages with a VCA
    config.defaultAttack = 0.002f;
    config.defaultDecay = 0.06f;
    config.defaultSustain = 0.0f;
    config.defaultRelease = 0.08f;
    Voice voice(0, config);
    voice.init(kSampleRate);
    voice.updateParameters(gatedState(9.0f));
    float voiced = 0.0f;
    for (int i = 0; i < 4800; ++i)
        voiced = std::max(voiced, std::abs(voice.process()));
    REQUIRE(voiced > 0.005f); // the VCA opens onto the pluck

    voice.setGate(false);
    for (int i = 0; i < 48000; ++i) (void)voice.process(); // release + quiet-hold window
    // Once the sitar itself has decayed the voice settles bit-exact silent,
    // whether the span renderer takes the skip path or the live one.
    for (int i = 0; i < 144000; ++i) REQUIRE(voice.process() == 0.0f);

    voice.setGate(true);
    float repluck = 0.0f;
    for (int i = 0; i < 4800; ++i)
        repluck = std::max(repluck, std::abs(voice.process()));
    REQUIRE(repluck > 0.005f);
}
