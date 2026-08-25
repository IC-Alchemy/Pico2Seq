#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

#include "voice/VoiceOscillator.h"

using namespace Catch::Matchers;

namespace {

constexpr float kSampleRate = 48000.0f;

// Count sign flips over n processed samples (crude pitch/duty probe).
int countTransitions(VoiceOscillator &osc, int n) {
    int transitions = 0;
    float prev = osc.process();
    for (int i = 1; i < n; ++i) {
        const float s = osc.process();
        if ((s > 0.0f) != (prev > 0.0f)) {
            ++transitions;
        }
        prev = s;
    }
    return transitions;
}

} // namespace

// ─── Waveform basics ─────────────────────────────────────────────────────────

TEST_CASE("VoiceOscillator produces non-zero bounded output per waveform", "[voiceosc]") {
    const uint8_t waveforms[] = {WAVE_SIN, WAVE_TRI, WAVE_SAW, WAVE_SQUARE,
                                 WAVE_BSP_SAW, WAVE_BSP_SQUARE, WAVE_NOISE};

    for (uint8_t wf : waveforms) {
        CAPTURE(wf);
        VoiceOscillator osc;
        osc.prepare(kSampleRate);
        osc.setWaveform(wf);
        REQUIRE(osc.waveform() == wf);

        float peak = 0.0f;
        for (int i = 0; i < 1024; ++i) {
            const float s = osc.process();
            REQUIRE(std::isfinite(s));
            REQUIRE(std::fabs(s) <= 1.5f);
            peak = std::max(peak, std::fabs(s));
        }
        REQUIRE(peak > 0.01f);
    }
}

TEST_CASE("VoiceOscillator noise output is bipolar", "[voiceosc]") {
    VoiceOscillator osc;
    osc.prepare(kSampleRate);
    osc.setWaveform(WAVE_NOISE);

    bool sawPositive = false;
    bool sawNegative = false;
    for (int i = 0; i < 512; ++i) {
        const float s = osc.process();
        sawPositive |= (s > 0.1f);
        sawNegative |= (s < -0.1f);
    }
    REQUIRE(sawPositive);
    REQUIRE(sawNegative);
}

TEST_CASE("VoiceOscillator unknown waveform ids normalize to band-limited saw", "[voiceosc]") {
    VoiceOscillator osc;
    osc.prepare(kSampleRate);
    osc.setWaveform(42);
    REQUIRE(osc.waveform() == WAVE_BSP_SAW);

    float peak = 0.0f;
    for (int i = 0; i < 256; ++i) {
        peak = std::max(peak, std::fabs(osc.process()));
    }
    REQUIRE(peak > 0.01f);
}

// ─── Pitch & pulse width ─────────────────────────────────────────────────────

TEST_CASE("VoiceOscillator setFreq changes pitch", "[voiceosc]") {
    VoiceOscillator slow;
    slow.prepare(kSampleRate);
    slow.setWaveform(WAVE_SQUARE);
    slow.setFreq(100.0f);
    // 4800 samples = 0.1 s = 10 cycles of 100 Hz square = 20 transitions
    const int slowTransitions = countTransitions(slow, 4800);

    VoiceOscillator fast;
    fast.prepare(kSampleRate);
    fast.setWaveform(WAVE_SQUARE);
    fast.setFreq(200.0f);
    const int fastTransitions = countTransitions(fast, 4800);

    REQUIRE(slowTransitions > 0);
    REQUIRE_THAT(static_cast<float>(fastTransitions),
                 WithinRel(2.0f * slowTransitions, 0.15f));
}

TEST_CASE("VoiceOscillator pulse width sets square duty cycle", "[voiceosc]") {
    VoiceOscillator osc;
    osc.prepare(kSampleRate);
    osc.setWaveform(WAVE_SQUARE);
    osc.setPulseWidth(0.25f);
    osc.setFreq(100.0f);

    constexpr int N = 48000; // exactly 1 s of 100 Hz = 100 cycles
    int highSamples = 0;
    for (int i = 0; i < N; ++i) {
        highSamples += (osc.process() > 0.0f) ? 1 : 0;
    }
    REQUIRE_THAT(static_cast<float>(highSamples) / N, WithinAbs(0.25f, 0.02f));
}

TEST_CASE("VoiceOscillator pulse width survives a later waveform switch", "[voiceosc]") {
    VoiceOscillator osc;
    osc.prepare(kSampleRate);
    osc.setPulseWidth(0.2f); // set while the default saw is active
    osc.setWaveform(WAVE_BSP_SQUARE);
    osc.setFreq(100.0f);

    constexpr int N = 48000;
    int highSamples = 0;
    for (int i = 0; i < N; ++i) {
        highSamples += (osc.process() > 0.0f) ? 1 : 0;
    }
    // Band-limited edges smear the transition by a few samples; keep tolerance loose
    REQUIRE_THAT(static_cast<float>(highSamples) / N, WithinAbs(0.2f, 0.05f));
}

// ─── Live reconfiguration ────────────────────────────────────────────────────

TEST_CASE("VoiceOscillator reconfiguration mid-stream stays bounded", "[voiceosc]") {
    VoiceOscillator osc;
    osc.prepare(kSampleRate);
    osc.setWaveform(WAVE_BSP_SAW);
    osc.setFreq(220.0f);

    for (int i = 0; i < 100; ++i) {
        osc.process();
    }

    osc.setWaveform(WAVE_SIN);
    // Pitch must be re-applied after the class swap: output should still oscillate
    float peak = 0.0f;
    for (int i = 0; i < 512; ++i) {
        const float s = osc.process();
        REQUIRE(std::isfinite(s));
        REQUIRE(std::fabs(s) <= 1.5f);
        peak = std::max(peak, std::fabs(s));
    }
    REQUIRE(peak > 0.5f); // 220 Hz sine reaches near full amplitude within 512 samples
}
