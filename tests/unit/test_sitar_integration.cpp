// Cross-cutting ENGINE_SITAR integration: behaviors that span the engine
// wiring (test_sitar_engine.cpp [sitar_engine]), the JAWARI/PICK/TARAF lanes
// (test_sitar_lanes.cpp [sitar_lanes]), the factory preset
// (test_sitar_preset.cpp [sitar_preset]) and the DSP model
// (test_sitar_voice.cpp [rpdsp][sitar]). Preset registration, lane mapping,
// seeding and the raw-config lifecycle are deliberately NOT repeated here:
// this file covers preset switching without stale DSP state, lane-driven
// render differences, retriggering with the full chain engaged, silent-voice
// mixing through VoiceManager, and lane-driven meend continuity.
#include <catch2/catch_test_macros.hpp>
#include "voice/Voice.h"
#include "voice/VoiceManager.h"
#include "voice/VoicePresets.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr float kSampleRate = 48000.0f;

const VoiceConfig &sitarPreset() { return VoicePresets::getSitarVoice(); }

// Lane fields sit at their 0.5 midpoints, which map to the preset's resting
// timbre, so a test only has to move the lane it is studying.
VoiceState gatedState(float noteIndex, float velocity = 0.8f)
{
    VoiceState s{};
    s.noteIndex = noteIndex;
    s.velocityLevel = velocity;
    s.filterCutoff = 0.5f;
    s.attackTimeSeconds = 0.5f;
    s.decayTimeSeconds = 0.5f;
    s.octaveOffset = 0;
    s.isGateHigh = true;
    s.hasSlide = false;
    s.shouldRetrigger = false;
    return s;
}

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

// Mean absolute sample difference between two identically driven voices whose
// lane values differ; the shared-chain render is what must move.
double laneRenderDifference(float VoiceState::*lane, float low, float high)
{
    const auto &preset = sitarPreset();
    Voice quietLane(0, preset), hotLane(0, preset);
    quietLane.init(kSampleRate);
    hotLane.init(kSampleRate);
    VoiceState lo = gatedState(9.0f), hi = gatedState(9.0f);
    lo.*lane = low;
    hi.*lane = high;
    quietLane.updateParameters(lo);
    hotLane.updateParameters(hi);
    double difference = 0.0;
    for (int i = 0; i < 8192; ++i)
    {
        const float a = quietLane.process(), b = hotLane.process();
        REQUIRE(std::isfinite(a));
        REQUIRE(std::isfinite(b));
        difference += std::abs(a - b);
    }
    return difference / 8192.0;
}
} // namespace

TEST_CASE("Switching presets away from a ringing sitar leaves no stale state",
          "[sitar_integration]")
{
    const auto &sitar = sitarPreset();
    const auto wgPluck = VoicePresets::getPresetConfigByName("WgPluck");
    const auto square = VoicePresets::getPresetConfigByName("Square");

    SECTION("An idle-gate swap clears the sitar tail before the next engine speaks")
    {
        Voice voice(0, sitar);
        voice.init(kSampleRate);
        voice.updateParameters(gatedState(9.0f));
        float peak = 0.0f;
        for (int i = 0; i < 9600; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
            peak = std::max(peak, std::abs(s));
        }
        REQUIRE(peak > 0.01f); // the sitar is ringing

        voice.setGate(false);
        (void)voice.process();    // consume the gate edge
        voice.setConfig(wgPluck); // gate is low, so the structural swap applies now
        // WgPluck also bypasses the ADSR, so any leaked sitar tail would be
        // audible in the raw output. The shared high-pass holds a little of
        // the old signal's state, which decays well below the PCM16 step
        // (3e-5) within ~1300 samples; require that settling shape.
        float flushPeak = 0.0f;
        for (int i = 0; i < 240; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
            flushPeak = std::max(flushPeak, std::abs(s));
        }
        REQUIRE(flushPeak < 0.2f);
        for (int i = 0; i < 1680; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
            REQUIRE(std::abs(s) < 0.01f);
        }
        for (int i = 0; i < 2880; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
            REQUIRE(std::abs(s) < 1.0e-6f);
        }
        REQUIRE(voice.getConfig().engine == ENGINE_WAVEGUIDE);

        voice.setGate(true);
        voice.updateParameters(gatedState(9.0f));
        peak = 0.0f;
        for (int i = 0; i < 4800; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
            peak = std::max(peak, std::abs(s));
        }
        REQUIRE(peak > 0.01f); // the next engine speaks from its own gate rise
    }

    SECTION("A gated swap keeps a natural-tail note sounding until the gate falls")
    {
        Voice voice(0, sitar);
        voice.init(kSampleRate);
        voice.updateParameters(gatedState(9.0f));
        float peak = 0.0f;
        for (int i = 0; i < 4800; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
            peak = std::max(peak, std::abs(s));
        }
        REQUIRE(peak > 0.01f);

        // WgPluck shares the sitar's chain toggles (no ADSR, no filter), so
        // nothing mutes the held note: the swap itself must wait for the gate.
        voice.setConfig(wgPluck);
        REQUIRE(voice.getRequestedConfig().engine == ENGINE_WAVEGUIDE);
        peak = 0.0f;
        for (int i = 0; i < 4800; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
            peak = std::max(peak, std::abs(s));
        }
        REQUIRE(peak > 0.01f); // the held note keeps sounding, click-free

        voice.setGate(false);
        // The swap applies on the gate fall and the old tail is cleared; the
        // high-pass residue of the ringing note settles as above.
        for (int i = 0; i < 240; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
        }
        REQUIRE(voice.getConfig().engine == ENGINE_WAVEGUIDE); // applied on gate fall
        for (int i = 0; i < 1680; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
            REQUIRE(std::abs(s) < 0.01f);
        }
        for (int i = 0; i < 2880; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
            REQUIRE(std::abs(s) < 1.0e-6f); // no sitar tail followed the swap
        }

        voice.setGate(true);
        voice.updateParameters(gatedState(9.0f));
        peak = 0.0f;
        for (int i = 0; i < 4800; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
            peak = std::max(peak, std::abs(s));
        }
        REQUIRE(peak > 0.01f); // the waveguide speaks from its own gate rise
    }

    SECTION("Switching to an enveloped preset hands the level to the ADSR at once")
    {
        Voice voice(0, sitar);
        voice.init(kSampleRate);
        voice.updateParameters(gatedState(9.0f));
        float peak = 0.0f;
        for (int i = 0; i < 4800; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
            peak = std::max(peak, std::abs(s));
        }
        REQUIRE(peak > 0.01f);

        // hasEnvelope is a scalar that applies immediately, unlike the staged
        // engine switch: the still-idle ADSR mutes the natural tail until the
        // next gate rise. This is shared Voice behavior for every natural-tail
        // preset (WgPluck to Square behaves identically), pinned here so the
        // sitar preset cannot regress to something stranger.
        voice.setConfig(square);
        // Muted at once: one small residue sample, then the idle-ADSR decay
        // reaches exact zero inside a few hundred samples.
        for (int i = 0; i < 480; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
            REQUIRE(std::abs(s) < 0.01f);
        }
        for (int i = 0; i < 1920; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
            REQUIRE(std::abs(s) < 1.0e-6f);
        }

        voice.setGate(false);
        for (int i = 0; i < 240; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
        }
        REQUIRE(voice.getConfig().engine == ENGINE_OSC); // swap completed on gate fall

        voice.setGate(true);
        voice.updateParameters(gatedState(9.0f));
        peak = 0.0f;
        for (int i = 0; i < 4800; ++i)
        {
            const float s = voice.process();
            REQUIRE(std::isfinite(s));
            peak = std::max(peak, std::abs(s));
        }
        REQUIRE(peak > 0.01f); // the enveloped preset speaks from its own gate rise
    }

    SECTION("Switching back renders bit-identical to a fresh sitar voice")
    {
        Voice swapped(0, sitar);
        swapped.init(kSampleRate);
        swapped.updateParameters(gatedState(9.0f));
        for (int i = 0; i < 2400; ++i) (void)swapped.process();
        swapped.setGate(false);
        (void)swapped.process();
        swapped.setConfig(square);
        (void)swapped.process();
        swapped.setConfig(sitar); // sitar again, through the same path presets use
        (void)swapped.process();

        Voice fresh(0, sitar);
        fresh.init(kSampleRate);
        const auto state = gatedState(9.0f);
        swapped.updateParameters(state);
        fresh.updateParameters(state);
        // The re-plucked strings agree immediately except for the decaying
        // high-pass residue carried across the swap; once it has settled the
        // voices must be bit-identical, proving no stale engine state.
        float maxDifference = 0.0f;
        for (int i = 0; i < 4096; ++i)
        {
            const float a = swapped.process();
            maxDifference = std::max(maxDifference, std::abs(a - fresh.process()));
        }
        INFO("max difference while settling " << maxDifference);
        REQUIRE(maxDifference < 0.05f);
        for (int i = 0; i < 4096; ++i)
        {
            (void)swapped.process();
            (void)fresh.process();
        }
        float peak = 0.0f;
        for (int i = 0; i < 2048; ++i)
        {
            const float a = swapped.process();
            REQUIRE(a == fresh.process());
            REQUIRE(std::isfinite(a));
            peak = std::max(peak, std::abs(a));
        }
        REQUIRE(peak > 0.01f); // still a live pluck, not two silenced voices
    }
}

TEST_CASE("A ringing waveguide voice swaps cleanly into the sitar engine",
          "[sitar_integration]")
{
    const auto &sitar = sitarPreset();
    Voice voice(0, VoicePresets::getPresetConfigByName("WgPluck"));
    voice.init(kSampleRate);
    voice.updateParameters(gatedState(9.0f));
    float peak = 0.0f;
    for (int i = 0; i < 4800; ++i)
    {
        const float s = voice.process();
        REQUIRE(std::isfinite(s));
        peak = std::max(peak, std::abs(s));
    }
    REQUIRE(peak > 0.01f); // the waveguide string is ringing

    voice.setGate(false);
    (void)voice.process();
    voice.setConfig(sitar); // gate is low: the swap resets the waveguide tail
    float flushPeak = 0.0f;
    for (int i = 0; i < 240; ++i)
    {
        const float s = voice.process();
        REQUIRE(std::isfinite(s));
        flushPeak = std::max(flushPeak, std::abs(s));
    }
    REQUIRE(flushPeak < 0.2f);
    for (int i = 0; i < 1680; ++i)
    {
        const float s = voice.process();
        REQUIRE(std::isfinite(s));
        REQUIRE(std::abs(s) < 0.01f); // the Karplus tail decays, no premature pluck
    }
    for (int i = 0; i < 2880; ++i)
    {
        const float s = voice.process();
        REQUIRE(std::isfinite(s));
        REQUIRE(std::abs(s) < 1.0e-6f);
    }
    REQUIRE(voice.getConfig().engine == ENGINE_SITAR);

    voice.setGate(true);
    voice.updateParameters(gatedState(9.0f));
    peak = 0.0f;
    for (int i = 0; i < 4800; ++i)
    {
        const float s = voice.process();
        REQUIRE(std::isfinite(s));
        peak = std::max(peak, std::abs(s));
    }
    REQUIRE(peak > 0.01f); // the sitar speaks on its own gate rise
}

TEST_CASE("Sequencer lanes measurably change the rendered sitar timbre",
          "[sitar_integration]")
{
    SECTION("JAWARI (Filter lane) reshapes the string loop")
    {
        const double difference =
            laneRenderDifference(&VoiceState::filterCutoff, 0.0f, 1.0f);
        INFO("JAWARI mean difference " << difference);
        REQUIRE(difference > 0.001);
    }

    SECTION("PICK (Attack lane) reshapes the next excitation")
    {
        const double difference =
            laneRenderDifference(&VoiceState::attackTimeSeconds, 0.0f, 1.0f);
        INFO("PICK mean difference " << difference);
        REQUIRE(difference > 0.001);
    }

    SECTION("TARAF (Decay lane) drives the sympathetic bank level")
    {
        const double difference =
            laneRenderDifference(&VoiceState::decayTimeSeconds, 0.0f, 1.0f);
        INFO("TARAF mean difference " << difference);
        REQUIRE(difference > 0.001);
    }
}

TEST_CASE("Gated retriggers stay bounded with the full sitar chain engaged",
          "[sitar_integration]")
{
    // Unlike the engine-level retrigger test (which silences the resonators),
    // this runs the preset values: taraf 0.45 and body 0.2 stay hot while the
    // string is re-plucked, so any resonator buildup would show here.
    Voice voice(0, sitarPreset());
    voice.init(kSampleRate);
    auto state = gatedState(9.0f);
    voice.updateParameters(state);
    float globalPeak = 0.0f;
    for (unsigned hit = 0; hit < 8; ++hit)
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
        INFO("retrigger " << hit << " peak " << peak);
        REQUIRE(peak > 0.01f); // every hit re-excites the string
        globalPeak = std::max(globalPeak, peak);
    }
    REQUIRE(globalPeak < 8.0f); // resonators never accumulate toward runaway
    REQUIRE(voice.getState().shouldRetrigger == false);
}

TEST_CASE("VoiceManager mixes a ringing sitar with silent voices without leakage",
          "[sitar_integration]")
{
    const auto &sitar = sitarPreset();

    VoiceManager full(4);
    const uint8_t sitarId = full.addVoice(sitar);
    REQUIRE(full.addVoice(VoiceConfig{}) != sitarId);
    REQUIRE(full.addVoice(VoiceConfig{}) != sitarId);
    REQUIRE(full.addVoice(VoiceConfig{}) != sitarId);
    full.init(kSampleRate);

    VoiceManager solo(1);
    const uint8_t soloId = solo.addVoice(sitar);
    solo.init(kSampleRate);

    // Before any note every voice is silent, so the four-voice mix is exact zero.
    for (int i = 0; i < 512; ++i)
        REQUIRE(full.processAllVoices() == 0.0f);

    const auto state = gatedState(9.0f);
    REQUIRE(full.updateVoiceState(sitarId, state));
    REQUIRE(solo.updateVoiceState(soloId, state));
    float peak = 0.0f;
    for (int i = 0; i < 9600; ++i)
    {
        const float mixed = full.processAllVoices();
        REQUIRE(std::isfinite(mixed));
        CHECK(mixed == solo.processAllVoices()); // silent voices add exact zeros
        peak = std::max(peak, std::abs(mixed));
    }
    REQUIRE(peak > 0.01f); // and the sitar itself was audible in the mix
}

TEST_CASE("A lane-driven note change bends a ringing sitar string", "[sitar_integration]")
{
    // Preset chain minus the resonators: this test reads the string pitch, so
    // keep the loop clean and the tail long like the engine-level slide test.
    auto config = sitarPreset();
    config.sitarJawari = 0.1f;
    config.sitarTarafAmount = 0.0f;
    config.sitarBodyAmount = 0.0f;
    config.sitarDecay = 8.0f;
    Voice voice(0, config);
    voice.init(kSampleRate);
    voice.setSlideTime(0.6f); // slow meend; reaches the model via SlideChanged
    (void)voice.process();

    voice.updateParameters(gatedState(9.0f)); // plucks at 880 Hz
    for (int i = 0; i < 4800; ++i) (void)voice.process();

    VoiceState bent = gatedState(9.0f);
    bent.noteIndex = 21.0f; // twelve semitones up, arriving through the Note lane
    bent.hasSlide = true;
    voice.updateParameters(bent);

    float maxDelta = 0.0f;
    std::array<float, 2400> early{};
    float previous = voice.process();
    for (int i = 0; i < 36000; ++i)
    {
        const float s = voice.process();
        maxDelta = std::max(maxDelta, std::abs(s - previous));
        previous = s;
        if (i >= 1200 && i < 3600) early[i - 1200] = s; // 0.025-0.075 s into the bend
    }
    std::array<float, 4800> settled{};
    settled[0] = previous;
    for (size_t i = 1; i < settled.size(); ++i)
    {
        const float s = voice.process();
        maxDelta = std::max(maxDelta, std::abs(s - previous));
        previous = s;
        settled[i] = s;
    }
    REQUIRE(maxDelta < 0.4f); // continuous bend, no retune glitch

    const double oldPitch = goertzelMagnitude(early.data(), early.size(), 880.0f, kSampleRate);
    const double targetEarly =
        goertzelMagnitude(early.data(), early.size(), 1760.0f, kSampleRate);
    INFO("early old-pitch bin " << oldPitch << " target bin " << targetEarly);
    REQUIRE(oldPitch > targetEarly); // the bend starts from the old pitch

    const double landed =
        goertzelMagnitude(settled.data(), settled.size(), 1760.0f, kSampleRate);
    const double residue =
        goertzelMagnitude(settled.data(), settled.size(), 880.0f, kSampleRate);
    INFO("settled target bin " << landed << " residue bin " << residue);
    REQUIRE(landed > 2.0 * residue); // and it lands on the new pitch
}
