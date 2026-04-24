#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

#include "voice/Voice.h"
#include "scales/scales.h"

using namespace Catch::Matchers;

static VoiceConfig defaultConfig() {
    VoiceConfig cfg;
    cfg.oscillatorCount = 1;
    cfg.oscWaveforms[0] = daisysp::Oscillator::WAVE_SIN;
    cfg.oscAmplitudes[0] = 1.0f;
    cfg.oscDetuning[0] = 0.0f;
    cfg.hasOverdrive = false;
    cfg.hasWavefolder = false;
    cfg.hasEnvelope = true;
    cfg.enabled = true;
    return cfg;
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
    v.setScaleTable(scale, SCALES_COUNT);

    uint8_t scaleIdx = 0;
    v.setCurrentScalePointer(&scaleIdx);

    REQUIRE_NOTHROW(v.init(48000.0f));
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
    v.setScaleTable(scale, SCALES_COUNT);
    uint8_t scaleIdx = 0;
    v.setCurrentScalePointer(&scaleIdx);
    v.init(48000.0f);
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
    v.setScaleTable(scale, SCALES_COUNT);
    uint8_t scaleIdx = 0;
    v.setCurrentScalePointer(&scaleIdx);
    v.init(48000.0f);

    // Brief gate on, then off
    v.setGate(true);
    for (int i = 0; i < 50; ++i) v.process();
    v.setGate(false);

    // Let envelope fully decay
    for (int i = 0; i < 2000; ++i) v.process();

    float tail = v.process();
    REQUIRE(std::abs(tail) < 0.01f);
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
    v.process(); // apply staged parameters
    REQUIRE_THAT(v.getState().velocityLevel, WithinAbs(0.42f, 0.001f));
}
