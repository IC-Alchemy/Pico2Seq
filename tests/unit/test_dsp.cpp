#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

#include "dsp/adsr.h"
#include "dsp/oscillator.h"
#include "dsp/ladder.h"
#include "dsp/svf.h"
#include "dsp/overdrive.h"
#include "dsp/wavefolder.h"

using namespace Catch::Matchers;

static constexpr float kPi = 3.141592653589793f;

// ─── ADSR ────────────────────────────────────────────────────────────────────

TEST_CASE("ADSR starts in idle stage", "[adsr]") {
    daisysp::Adsr env;
    env.Init(48000.0f);
    REQUIRE(env.GetCurrentSegment() == daisysp::ADSR_SEG_IDLE);
    REQUIRE_FALSE(env.IsRunning());
}

TEST_CASE("ADSR enters attack on first gate-high sample", "[adsr]") {
    daisysp::Adsr env;
    env.Init(48000.0f);
    env.SetAttackTime(0.01f);
    env.SetDecayTime(0.05f);
    env.SetSustainLevel(0.7f);
    env.SetReleaseTime(0.1f);

    float out = env.Process(true);
    REQUIRE(env.GetCurrentSegment() == daisysp::ADSR_SEG_ATTACK);
    REQUIRE(out >= 0.0f);
    REQUIRE(out <= 1.0f);
}

TEST_CASE("ADSR output is bounded [0, 1] throughout full cycle", "[adsr]") {
    daisysp::Adsr env;
    env.Init(48000.0f);
    env.SetAttackTime(0.001f);
    env.SetDecayTime(0.001f);
    env.SetSustainLevel(0.5f);
    env.SetReleaseTime(0.001f);

    const int hold_samples = 200;
    for (int i = 0; i < hold_samples; ++i) {
        float out = env.Process(true);
        REQUIRE(out >= -0.01f);
        REQUIRE(out <= 1.01f);
    }
    // Release
    for (int i = 0; i < hold_samples; ++i) {
        float out = env.Process(false);
        REQUIRE(out >= -0.01f);
        REQUIRE(out <= 1.01f);
    }
}

TEST_CASE("ADSR enters release when gate goes low", "[adsr]") {
    daisysp::Adsr env;
    env.Init(48000.0f);
    env.SetAttackTime(0.001f);
    env.SetDecayTime(0.001f);
    env.SetSustainLevel(0.7f);
    env.SetReleaseTime(0.5f);

    // Run through attack/decay/sustain
    for (int i = 0; i < 500; ++i) env.Process(true);

    // Gate off → release
    env.Process(false);
    REQUIRE(env.GetCurrentSegment() == daisysp::ADSR_SEG_RELEASE);
}

TEST_CASE("ADSR Retrigger resets envelope", "[adsr]") {
    daisysp::Adsr env;
    env.Init(48000.0f);
    env.SetAttackTime(0.5f);
    env.SetSustainLevel(0.5f);

    // Advance into attack
    for (int i = 0; i < 100; ++i) env.Process(true);
    float mid_val = env.Process(true);

    // Hard retrigger
    env.Retrigger(true);
    float after_retrigger = env.Process(true);
    REQUIRE(after_retrigger < mid_val); // reset to near 0
}

// ─── Oscillator ──────────────────────────────────────────────────────────────

TEST_CASE("Oscillator produces non-zero output after init", "[oscillator]") {
    daisysp::Oscillator osc;
    osc.Init(48000.0f);
    osc.SetFreq(440.0f);
    osc.SetAmp(1.0f);
    osc.SetWaveform(daisysp::Oscillator::WAVE_SIN);

    bool found_nonzero = false;
    for (int i = 0; i < 200; ++i) {
        if (osc.Process() != 0.0f) { found_nonzero = true; break; }
    }
    REQUIRE(found_nonzero);
}

TEST_CASE("Oscillator output stays within amplitude bounds", "[oscillator]") {
    daisysp::Oscillator osc;
    osc.Init(48000.0f);
    osc.SetFreq(220.0f);
    osc.SetAmp(0.5f);
    osc.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);

    for (int i = 0; i < 2000; ++i) {
        REQUIRE(std::abs(osc.Process()) <= 1.0f);
    }
}

TEST_CASE("mtof converts MIDI note 69 to A4 (440 Hz)", "[oscillator]") {
    float freq = daisysp::mtof(69.0f);
    REQUIRE_THAT(freq, WithinAbs(440.0f, 0.5f));
}

TEST_CASE("mtof converts MIDI note 60 to C4 (~261.63 Hz)", "[oscillator]") {
    float freq = daisysp::mtof(60.0f);
    REQUIRE_THAT(freq, WithinAbs(261.63f, 0.5f));
}

// ─── Ladder Filter ───────────────────────────────────────────────────────────

TEST_CASE("Ladder filter produces output within bounds", "[ladder]") {
    daisysp::LadderFilter filt;
    filt.Init(48000.0f);
    filt.SetFreq(1000.0f);
    filt.SetRes(0.5f);

    for (int i = 0; i < 512; ++i) {
        float input = (i % 2 == 0) ? 0.5f : -0.5f;
        float out = filt.Process(input);
        REQUIRE(std::isfinite(out));
    }
}

TEST_CASE("Ladder filter attenuates above cutoff in LP24 mode", "[ladder]") {
    daisysp::LadderFilter filt;
    filt.Init(48000.0f);
    filt.SetFreq(200.0f);
    filt.SetRes(0.1f);
    filt.SetFilterMode(daisysp::LadderFilter::FilterMode::LP24);

    // Feed a high-frequency signal and measure RMS after settling
    const float hf = 8000.0f;
    const float sr = 48000.0f;
    float sum_sq = 0.0f;
    const int N = 2048;
    for (int i = 0; i < N; ++i) {
        float s = std::sin(2.0f * kPi * hf * i / sr);
        float out = filt.Process(s);
        if (i > 512) sum_sq += out * out; // skip transient
    }
    float rms = std::sqrt(sum_sq / (N - 512));
    // Expect significant attenuation: RMS well below 0.1
    REQUIRE(rms < 0.1f);
}
