#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <array>
#include <cmath>

// rpdsp additions made during the src/dsp -> rpdsp migration:
// Wavefolder, fmap/Mapping, fastTanh-based Waveshaper, plus smoke tests for
// the headers vendored from the Pico-DSP-Garden rpdsp copy.
#include <rpdsp/algorithm.h>
#include <rpdsp/effects.h>
#include <rpdsp/wavefolder.h>
#include <rpdsp/DSPFunctions.h>

using namespace Catch::Matchers;

// ─── Wavefolder ──────────────────────────────────────────────────────────────

TEST_CASE("Wavefolder is identity for inputs inside [-1, 1]", "[rpdsp]") {
    rpdsp::Wavefolder wf; // gain 1.0, offset 0.0 by default
    REQUIRE_THAT(wf.process(0.0f), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(wf.process(0.5f), WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(wf.process(-0.5f), WithinAbs(-0.5f, 1e-6f));
    REQUIRE_THAT(wf.process(0.99f), WithinAbs(0.99f, 1e-6f));
}

TEST_CASE("Wavefolder folds inputs beyond [-1, 1] back inside", "[rpdsp]") {
    rpdsp::Wavefolder wf;
    // x=1.5: ft=floor(1.25)=1 (odd) -> -(1.5-2) = 0.5
    REQUIRE_THAT(wf.process(1.5f), WithinAbs(0.5f, 1e-6f));
    // x=-1.5: ft=floor(-0.25)=-1 (odd) -> -(-1.5+2) = -0.5
    REQUIRE_THAT(wf.process(-1.5f), WithinAbs(-0.5f, 1e-6f));
    // x=3: ft=2 (even) -> 3-4 = -1
    REQUIRE_THAT(wf.process(3.0f), WithinAbs(-1.0f, 1e-6f));

    for (float x = -8.0f; x <= 8.0f; x += 0.05f) {
        const float y = wf.process(x);
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) <= 1.0f + 1e-6f);
    }
}

TEST_CASE("Wavefolder gain drives folding depth", "[rpdsp]") {
    rpdsp::Wavefolder wf;
    wf.setGain(2.0f);
    // x=0.75 * 2 = 1.5 -> folds to 0.5
    REQUIRE_THAT(wf.process(0.75f), WithinAbs(0.5f, 1e-6f));
}

TEST_CASE("Wavefolder offset shifts the fold point", "[rpdsp]") {
    rpdsp::Wavefolder wf;
    wf.setOffset(0.5f);
    // x = (0.25 + 0.5) * 1.0 = 0.75 -> identity region
    REQUIRE_THAT(wf.process(0.25f), WithinAbs(0.75f, 1e-6f));
}

// ─── fmap / Mapping (ported from DaisySP semantics) ──────────────────────────

TEST_CASE("fmap LINEAR interpolates endpoints", "[rpdsp]") {
    REQUIRE_THAT(rpdsp::fmap(0.0f, 100.0f, 300.0f), WithinAbs(100.0f, 1e-5f));
    REQUIRE_THAT(rpdsp::fmap(0.5f, 100.0f, 300.0f), WithinAbs(200.0f, 1e-5f));
    REQUIRE_THAT(rpdsp::fmap(1.0f, 100.0f, 300.0f), WithinAbs(300.0f, 1e-5f));
}

TEST_CASE("fmap EXP applies the square curve", "[rpdsp]") {
    // min + in^2 * (max - min)
    REQUIRE_THAT(rpdsp::fmap(0.5f, 100.0f, 300.0f, rpdsp::Mapping::EXP),
                 WithinAbs(150.0f, 1e-4f));
    REQUIRE_THAT(rpdsp::fmap(0.0f, 100.0f, 300.0f, rpdsp::Mapping::EXP),
                 WithinAbs(100.0f, 1e-4f));
}

TEST_CASE("fmap LOG spans decades and matches the old filter mapping", "[rpdsp]") {
    // min=100, max=10000: two decades, so in=0.5 lands on 10000*... = max
    REQUIRE_THAT(rpdsp::fmap(0.0f, 100.0f, 10000.0f, rpdsp::Mapping::LOG),
                 WithinAbs(100.0f, 1e-3f));
    REQUIRE_THAT(rpdsp::fmap(1.0f, 100.0f, 10000.0f, rpdsp::Mapping::LOG),
                 WithinAbs(10000.0f, 1.0f));
    // One decade: in=0.5 -> sqrt(10) scaling
    REQUIRE_THAT(rpdsp::fmap(0.5f, 100.0f, 1000.0f, rpdsp::Mapping::LOG),
                 WithinAbs(100.0f * 3.16227766f, 0.01f));
}

TEST_CASE("fmap clamps out-of-range inputs", "[rpdsp]") {
    REQUIRE_THAT(rpdsp::fmap(-1.0f, 100.0f, 300.0f), WithinAbs(100.0f, 1e-5f));
    REQUIRE_THAT(rpdsp::fmap(2.0f, 100.0f, 300.0f), WithinAbs(300.0f, 1e-5f));
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
