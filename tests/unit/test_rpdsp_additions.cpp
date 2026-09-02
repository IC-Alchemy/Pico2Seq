#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <array>
#include <cmath>

// rpdsp additions made during the src/dsp -> rpdsp migration:
// fastTanh-based Waveshaper and smoke tests for the headers vendored from the
// Pico-DSP-Garden rpdsp copy, plus dspmap::fmap tests (the local carry-over
// of the fmap/Mapping helper and the Wavefolder tests, both removed in the
// rpdsp submodule update — see src/utils/DspMapping.h).
#include <rpdsp/algorithm.h>
#include <rpdsp/effects.h>
#include <rpdsp/DSPFunctions.h>

#include "utils/DspMapping.h"

using namespace Catch::Matchers;

// ─── dspmap::fmap (local carry-over of the removed rpdsp helper) ─────────────

TEST_CASE("fmap LINEAR interpolates endpoints", "[rpdsp]") {
    REQUIRE_THAT(dspmap::fmap(0.0f, 100.0f, 300.0f), WithinAbs(100.0f, 1e-5f));
    REQUIRE_THAT(dspmap::fmap(0.5f, 100.0f, 300.0f), WithinAbs(200.0f, 1e-5f));
    REQUIRE_THAT(dspmap::fmap(1.0f, 100.0f, 300.0f), WithinAbs(300.0f, 1e-5f));
}

TEST_CASE("fmap EXP applies the square curve", "[rpdsp]") {
    // min + in^2 * (max - min)
    REQUIRE_THAT(dspmap::fmap(0.5f, 100.0f, 300.0f, dspmap::Mapping::EXP),
                 WithinAbs(150.0f, 1e-4f));
    REQUIRE_THAT(dspmap::fmap(0.0f, 100.0f, 300.0f, dspmap::Mapping::EXP),
                 WithinAbs(100.0f, 1e-4f));
}

TEST_CASE("fmap LOG spans decades and matches the old filter mapping", "[rpdsp]") {
    // min=100, max=10000: two decades, so in=0.5 lands on 10000*... = max
    REQUIRE_THAT(dspmap::fmap(0.0f, 100.0f, 10000.0f, dspmap::Mapping::LOG),
                 WithinAbs(100.0f, 1e-3f));
    REQUIRE_THAT(dspmap::fmap(1.0f, 100.0f, 10000.0f, dspmap::Mapping::LOG),
                 WithinAbs(10000.0f, 1.0f));
    // One decade: in=0.5 -> sqrt(10) scaling
    REQUIRE_THAT(dspmap::fmap(0.5f, 100.0f, 1000.0f, dspmap::Mapping::LOG),
                 WithinAbs(100.0f * 3.16227766f, 0.01f));
}

TEST_CASE("fmap clamps out-of-range inputs", "[rpdsp]") {
    REQUIRE_THAT(dspmap::fmap(-1.0f, 100.0f, 300.0f), WithinAbs(100.0f, 1e-5f));
    REQUIRE_THAT(dspmap::fmap(2.0f, 100.0f, 300.0f), WithinAbs(300.0f, 1e-5f));
}

// ─── Waveshaper (fastTanh) ───────────────────────────────────────────────────

TEST_CASE("Waveshaper normalizes unity input regardless of drive", "[rpdsp]") {
    rpdsp::Waveshaper ws;
    for (float drive : {1.0f, 2.0f, 4.0f}) {
        CAPTURE(drive);
        ws.setDrive(drive);
        REQUIRE_THAT(ws.process(1.0f), WithinAbs(1.0f, 1e-4f));
        REQUIRE_THAT(ws.process(-1.0f), WithinAbs(-1.0f, 1e-4f));
    }
}

TEST_CASE("Waveshaper stays bounded for bounded inputs", "[rpdsp]") {
    rpdsp::Waveshaper ws;
    ws.setDrive(4.0f);
    for (int i = -100; i <= 100; ++i) {
        const float x = static_cast<float>(i) / 100.0f;
        const float y = ws.process(x);
        REQUIRE(std::fabs(y) <= 1.0f + 1e-6f);
        REQUIRE(std::isfinite(y));
    }
}

// ─── Vendored Pico-DSP-Garden headers compile and run on host ───────────────

TEST_CASE("DSPFunctions adsr_analog attacks when gated and releases when not", "[rpdsp][vendored]") {
    std::array<float, 3> state{};
    float v = 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < 48000; ++i) { // 1 s gated
        v = rpdsp::adsr_analog(1.0f, 0.005f, 0.005f, 0.6f, 0.01f, state.data());
        REQUIRE(v >= -1e-6f);
        REQUIRE(v <= 1.5f);
        peak = std::max(peak, v);
    }
    REQUIRE(peak > 0.5f);

    float after = v;
    for (int i = 0; i < 48000; ++i) { // 1 s released
        after = rpdsp::adsr_analog(0.0f, 0.005f, 0.005f, 0.6f, 0.01f, state.data());
    }
    REQUIRE(after < 0.1f);
}

TEST_CASE("DSPFunctions comp_feedback tames loud input", "[rpdsp][vendored]") {
    std::array<float, 2> state{};
    float settledPeak = 0.0f;
    for (int i = 0; i < 48000; ++i) {
        const float y = rpdsp::comp_feedback(0.9f, 0.3f, 0.5f, state.data());
        REQUIRE(std::isfinite(y));
        // Skip the attack: the detector converges over the first few hundred
        // samples, so judge the steady state (last 1000 samples) only.
        if (i >= 47000) {
            settledPeak = std::max(settledPeak, std::fabs(y));
        }
    }
    // Converged GR for 0.9 in / 0.3 thresh / 0.5 amount is ~0.23 -> ~0.69 out
    REQUIRE(settledPeak < 0.85f);
    REQUIRE(settledPeak > 0.3f);
}
