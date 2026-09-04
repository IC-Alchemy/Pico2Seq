#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <string>

#include "voice/Voice.h"
#include "voice/VoicePresets.h"
#include "scales/scales.h"

using namespace Catch::Matchers;

static VoiceConfig defaultConfig() {
    VoiceConfig cfg;
    cfg.oscillatorCount = 1;
    cfg.oscWaveforms[0] = WAVE_SIN;
    cfg.oscAmplitudes[0] = 1.0f;
    cfg.oscDetuning[0] = 0.0f;
    cfg.hasOverdrive = false;
    cfg.hasEnvelope = true;
    cfg.enabled = true;
    return cfg;
}

// Shared scale index — address must remain valid for the lifetime of the voice.
static uint8_t s_scaleIdx = 0;

static void initVoiceWithScale(Voice& v, float sampleRate = 48000.0f) {
    v.setScaleTable(scale, SCALES_COUNT);
    v.setCurrentScalePointer(&s_scaleIdx);
    v.init(sampleRate);
}

// ─── Construction & Initialization ───────────────────────────────────────────

TEST_CASE("Voice constructs without crash", "[voice]") {
    Voice v(0, defaultConfig());
    REQUIRE(v.getId() == 0);
}

TEST_CASE("Voice init does not crash", "[voice]") {
    Voice v(0, defaultConfig());
    REQUIRE_NOTHROW(v.init(48000.0f));
}

TEST_CASE("Voice is enabled by default", "[voice]") {
    Voice v(0, defaultConfig());
    REQUIRE(v.isEnabled());
}

TEST_CASE("Voice enable/disable toggles correctly", "[voice]") {
    Voice v(0, defaultConfig());
    v.setEnabled(false);
    REQUIRE_FALSE(v.isEnabled());
    v.setEnabled(true);
    REQUIRE(v.isEnabled());
}

// ─── Gate ─────────────────────────────────────────────────────────────────────

TEST_CASE("Voice gate starts low", "[voice]") {
    Voice v(0, defaultConfig());
    REQUIRE_FALSE(v.getGate());
}

TEST_CASE("Voice gate toggles via setGate", "[voice]") {
    Voice v(0, defaultConfig());
    v.setGate(true);
    REQUIRE(v.getGate());
    REQUIRE(v.getState().isGateHigh);
    v.setGate(false);
    REQUIRE_FALSE(v.getGate());
    REQUIRE_FALSE(v.getState().isGateHigh);
}

// ─── Filter frequency ────────────────────────────────────────────────────────

TEST_CASE("Voice filter frequency can be set and read back", "[voice]") {
    Voice v(0, defaultConfig());
    v.setFilterFrequency(2000.0f);
    REQUIRE_THAT(v.getFilterFrequency(), WithinAbs(2000.0f, 0.01f));
}

// ─── Scale injection ─────────────────────────────────────────────────────────

TEST_CASE("Voice accepts scale table injection without crash", "[voice]") {
    Voice v(0, defaultConfig());
    REQUIRE_NOTHROW(initVoiceWithScale(v));
}

TEST_CASE("Voice scale injection enables chromatic fallback when nullptr", "[voice]") {
    Voice v(0, defaultConfig());
    v.setScaleTable(nullptr, 0);
    v.setCurrentScalePointer(nullptr);
    REQUIRE_NOTHROW(v.init(48000.0f));
}

// ─── Audio processing ────────────────────────────────────────────────────────

TEST_CASE("Voice process() returns finite float when gate is on", "[voice]") {
    Voice v(0, defaultConfig());
    initVoiceWithScale(v);
    v.setGate(true);

    for (int i = 0; i < 64; ++i) {
        float s = v.process();
        REQUIRE(std::isfinite(s));
    }
}

TEST_CASE("Voice process() returns zero or near-zero when gate is off and envelope decays", "[voice]") {
    VoiceConfig cfg = defaultConfig();
    cfg.defaultAttack  = 0.001f;
    cfg.defaultDecay   = 0.001f;
    cfg.defaultSustain = 0.0f;
    cfg.defaultRelease = 0.001f;

    Voice v(0, cfg);
    initVoiceWithScale(v);

    v.setGate(true);
    for (int i = 0; i < 50; ++i) v.process();
    v.setGate(false);
    for (int i = 0; i < 2000; ++i) v.process();

    REQUIRE(std::abs(v.process()) < 0.01f);
}

// ─── updateParameters ────────────────────────────────────────────────────────

TEST_CASE("updateParameters updates state velocity", "[voice]") {
    Voice v(0, defaultConfig());
    v.init(48000.0f);

    VoiceState vs;
    vs.velocityLevel = 0.42f;
    vs.noteIndex = 0.0f;
    vs.filterCutoff = 0.5f;
    vs.attackTimeSeconds = 0.01f;
    vs.decayTimeSeconds = 0.1f;
    vs.octaveOffset = 0;
    vs.gateLengthTicks = 12;
    vs.isGateHigh = false;
    vs.hasSlide = false;
    vs.shouldRetrigger = false;

    v.updateParameters(vs);
    v.process();
    REQUIRE_THAT(v.getState().velocityLevel, WithinAbs(0.42f, 0.001f));
}

// ─── Preset registry (15 presets) ─────────────────────────────────────────────

TEST_CASE("Preset registry exposes 15 presets with stable names", "[voice][presets]") {
    REQUIRE(VoicePresets::getPresetCount() == 15);
    REQUIRE(std::string(VoicePresets::getPresetName(0)) == "Analog");
    REQUIRE(std::string(VoicePresets::getPresetName(6)) == "Percussion");
    REQUIRE(std::string(VoicePresets::getPresetName(7)) == "SubFunk");
    REQUIRE(std::string(VoicePresets::getPresetName(8)) == "RubberSub");
    REQUIRE(std::string(VoicePresets::getPresetName(9)) == "WgPluck");
    REQUIRE(std::string(VoicePresets::getPresetName(12)) == "WgShimmer");
    REQUIRE(std::string(VoicePresets::getPresetName(13)) == "Hypersaw");
    REQUIRE(std::string(VoicePresets::getPresetName(14)) == "NoiseStorm");
    REQUIRE(std::string(VoicePresets::getPresetName(15)) == "Unknown");
    REQUIRE(&VoicePresets::getPresetConfig(200) == &VoicePresets::getAnalogVoice());
}

TEST_CASE("New presets select the intended engines", "[voice][presets]") {
    REQUIRE(VoicePresets::getSubFunkVoice().engine == ENGINE_OSC);
    REQUIRE(VoicePresets::getRubberSubVoice().engine == ENGINE_OSC);
    REQUIRE(VoicePresets::getWaveguidePluckVoice().engine == ENGINE_WAVEGUIDE);
    REQUIRE(VoicePresets::getWaveguideNylonVoice().engine == ENGINE_WAVEGUIDE);
    REQUIRE(VoicePresets::getWaveguideBellVoice().engine == ENGINE_WAVEGUIDE);
    REQUIRE(VoicePresets::getWaveguideShimmerVoice().engine == ENGINE_WAVEGUIDE);
    REQUIRE(VoicePresets::getHypersawVoice().engine == ENGINE_OSC);
    REQUIRE(VoicePresets::getNoiseStormVoice().engine == ENGINE_NOISEFX);
}

// ─── Alternate engines (waveguide / noise-FX) ─────────────────────────────────

TEST_CASE("Every preset produces finite bounded audio while gated", "[voice][presets]") {
    for (uint8_t p = 0; p < VoicePresets::getPresetCount(); ++p)
    {
        Voice v(0, VoicePresets::getPresetConfig(p));
        v.init(48000.0f);
        v.setGate(true);

        bool allFinite = true;
        float maxAbs = 0.0f;
        for (int i = 0; i < 24000; ++i) // half a second
        {
            const float s = v.process();
            if (!std::isfinite(s))
                allFinite = false;
            maxAbs = std::max(maxAbs, std::abs(s));
        }
        INFO("preset index " << static_cast<int>(p)
                             << " (" << VoicePresets::getPresetName(p) << ")");
        REQUIRE(allFinite);
        REQUIRE(maxAbs > 0.0f);  // every preset makes sound when gated
        REQUIRE(maxAbs < 32.0f); // no preset diverges
    }
}

TEST_CASE("Waveguide engine plucks on gate rise and decays after gate fall", "[voice]") {
    Voice v(0, VoicePresets::getWaveguidePluckVoice());
    v.init(48000.0f);

    float maxAbs = 0.0f;
    v.setGate(true);
    for (int i = 0; i < 8000; ++i)
    {
        const float s = v.process();
        REQUIRE(std::isfinite(s));
        maxAbs = std::max(maxAbs, std::abs(s));
    }
    REQUIRE(maxAbs > 0.02f); // the string actually sounded

    v.setGate(false);
    for (int i = 0; i < 48000; ++i)
        v.process();
    REQUIRE(std::abs(v.process()) < 0.01f); // envelope release silences the tail
}

TEST_CASE("Bypassed envelope still plucks the waveguide on gate rise", "[voice]") {
    VoiceConfig cfg = VoicePresets::getWaveguidePluckVoice();
    cfg.hasEnvelope = false;
    Voice v(0, cfg);
    initVoiceWithScale(v);

    v.setGate(true);
    float maxAbs = 0.0f;
    for (int i = 0; i < 8000; ++i)
    {
        const float s = v.process();
        REQUIRE(std::isfinite(s));
        maxAbs = std::max(maxAbs, std::abs(s));
    }
    REQUIRE(maxAbs > 0.02f); // pluck armed even with the ADSR bypassed
}

TEST_CASE("Bypassed filter scales output by velocity", "[voice]") {
    VoiceConfig cfg = defaultConfig();
    cfg.hasFilter = false;
    cfg.hasEnvelope = false; // isolate the level path from the ADSR
    Voice v(0, cfg);
    initVoiceWithScale(v);

    VoiceState vs;
    vs.noteIndex = 0.0f;
    vs.isGateHigh = true;

    vs.velocityLevel = 1.0f;
    v.updateParameters(vs);
    v.setGate(true);
    float loud = 0.0f;
    for (int i = 0; i < 480; ++i)
        loud = std::max(loud, std::abs(v.process()));

    vs.velocityLevel = 0.25f;
    v.updateParameters(vs);
    // Discard the amplitude-step transient through the high-pass filter before
    // measuring the settled level.
    for (int i = 0; i < 2400; ++i)
        v.process();
    float quiet = 0.0f;
    for (int i = 0; i < 480; ++i)
        quiet = std::max(quiet, std::abs(v.process()));

    REQUIRE(loud > quiet * 2.0f);
}

TEST_CASE("Noise-FX engine produces finite textured output while gated", "[voice]") {
    Voice v(0, VoicePresets::getNoiseStormVoice());
    v.init(48000.0f);
    v.setGate(true);

    float maxAbs = 0.0f;
    for (int i = 0; i < 24000; ++i)
    {
        const float s = v.process();
        REQUIRE(std::isfinite(s));
        maxAbs = std::max(maxAbs, std::abs(s));
    }
    REQUIRE(maxAbs > 0.005f);  // noise + chaos + diffuser + swarm audible
    REQUIRE(maxAbs < 32.0f);
}

TEST_CASE("Staged config switch to waveguide engine plucks on next gate rise", "[voice]") {
    Voice v(0, defaultConfig());
    v.init(48000.0f);
    v.setGate(true);
    for (int i = 0; i < 100; ++i)
        v.process();

    // setConfig stages cross-core; applyPendingConfig_ runs at the top of process()
    v.setConfig(VoicePresets::getWaveguidePluckVoice());
    v.setGate(false);
    for (int i = 0; i < 100; ++i)
        v.process();

    v.setGate(true); // rising edge after the switch drives the pluck
    float maxAbs = 0.0f;
    for (int i = 0; i < 8000; ++i)
    {
        const float s = v.process();
        REQUIRE(std::isfinite(s));
        maxAbs = std::max(maxAbs, std::abs(s));
    }
    REQUIRE(maxAbs > 0.02f);
}
