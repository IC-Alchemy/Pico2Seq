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
#include <rpdsp/envelope.h>
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

// ─── Hot-path kernels that replaced libm calls (padeTanh, sinNormalizedPhase) ─

TEST_CASE("padeTanh tracks std::tanh to 1e-4 for all inputs", "[rpdsp]") {
    // Worst case is 7.1e-5 at the 4.79 clamp (balanced against the tail);
    // fastTanh (the cheaper cousin) is only good to ~2e-2. Sweep well past
    // the clamp so the saturated tail is covered too.
    float maxErr = 0.0f;
    float maxErrInner = 0.0f; // |x| <= 4: the range Waveshaper drives reach
    for (int i = -800; i <= 800; ++i) {
        const float x = static_cast<float>(i) / 100.0f;
        const float err = std::fabs(rpdsp::padeTanh(x) - std::tanh(x));
        maxErr = std::max(maxErr, err);
        if (std::fabs(x) <= 4.0f) {
            maxErrInner = std::max(maxErrInner, err);
        }
    }
    REQUIRE(maxErr < 1e-4f);
    REQUIRE(maxErrInner < 2e-5f);
    // Odd symmetry and monotonic up to the clamp (Waveshaper's unity
    // normalization relies on padeTanh(x*d) <= padeTanh(d) for |x| <= 1).
    REQUIRE_THAT(rpdsp::padeTanh(-2.0f), WithinAbs(-rpdsp::padeTanh(2.0f), 1e-7f));
    float prev = rpdsp::padeTanh(-6.0f);
    for (int i = -600; i <= 600; ++i) {
        const float y = rpdsp::padeTanh(static_cast<float>(i) / 100.0f);
        REQUIRE(y >= prev);
        prev = y;
    }
}

TEST_CASE("sinNormalizedPhase tracks sin(2*pi*p) to 1e-6 over one cycle", "[rpdsp]") {
    float maxErr = 0.0f;
    for (int i = 0; i < 100000; ++i) {
        const float p = static_cast<float>(i) / 100000.0f;
        const float ref = std::sin(2.0 * 3.14159265358979323846 * p);
        maxErr = std::max(maxErr, std::fabs(rpdsp::sinNormalizedPhase(p) - ref));
    }
    REQUIRE(maxErr < 1e-6f);
    // Quadrant boundaries land on exact values
    REQUIRE_THAT(rpdsp::sinNormalizedPhase(0.0f), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(rpdsp::sinNormalizedPhase(0.25f), WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(rpdsp::sinNormalizedPhase(0.5f), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(rpdsp::sinNormalizedPhase(0.75f), WithinAbs(-1.0f, 1e-6f));
}

TEST_CASE("ADSR reciprocal stage steps still land exactly on stage ends", "[rpdsp]") {
    rpdsp::ADSR env;
    env.prepare(48000.0f);
    env.setAttack(0.01f);   // 480 samples
    env.setDecay(0.1f);     // 4800 samples
    env.setSustain(0.5f);
    env.setRelease(0.2f);   // 9600 samples
    env.noteOn();
    float prev = 0.0f;
    for (int i = 0; i < 479; ++i) {
        const float v = env.process();
        REQUIRE(v >= prev); // monotonic attack
        REQUIRE(v <= 1.0f);
        prev = v;
    }
    REQUIRE_THAT(env.process(), WithinAbs(1.0f, 1e-7f)); // sample 480: attack end pinned to 1.0
    REQUIRE(env.stage() == rpdsp::ADSR::Stage::kDecay);
    for (int i = 0; i < 4800; ++i) {
        env.process();
    }
    REQUIRE(env.stage() == rpdsp::ADSR::Stage::kSustain);
    REQUIRE_THAT(env.value(), WithinAbs(0.5f, 1e-7f));
    env.noteOff();
    for (int i = 0; i < 9600; ++i) {
        env.process();
    }
    REQUIRE(env.stage() == rpdsp::ADSR::Stage::kIdle);
    REQUIRE_THAT(env.value(), WithinAbs(0.0f, 1e-7f));
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

// New recipes: test spectral, energy, history and timing contracts rather
// than just checking that a few output samples are finite.

TEST_CASE("Prism isolates the selected harmonic with narrow focus", "[rpdsp][recipes]") {
    constexpr double pi = 3.14159265358979323846;
    for (int harmonic : {1, 3, 6}) {
        for (float direction : {-1.0f, 1.0f}) {
            CAPTURE(harmonic, direction);
            std::array<float, 1> state{};
            for (int i = 0; i < 1024; ++i) {
                const float y = rpdsp::osc_prism(direction / 256.0f,
                                                (harmonic - 1) / 5.0f, 0.0f, state.data());
                const double reference = std::sin(2.0 * pi * direction * harmonic * (i + 1) / 256.0);
                REQUIRE_THAT(y, WithinAbs(reference, 3e-6));
            }
        }
    }
}

TEST_CASE("Prism fades partials approaching Nyquist and bounds modulation", "[rpdsp][recipes]") {
    std::array<float, 1> state{};
    for (int i = 0; i < 32; ++i) {
        // Harmonic 2 of fs/4 is at Nyquist and must be omitted.
        REQUIRE(rpdsp::osc_prism(0.25f, 0.2f, 0.0f, state.data()) == 0.0f);
    }
    state.fill(0.0f);
    // A fundamental at 0.475 cycles/sample is halfway through its fade.
    const float faded = rpdsp::osc_prism(0.475f, 0.0f, 0.0f, state.data());
    REQUIRE_THAT(faded, WithinAbs(0.5 * std::sin(2.0 * 3.141592653589793 * 0.475), 2e-6));
    for (int i = 0; i < 8192; ++i) {
        const float inc = 0.75f * std::sin(i * 0.017f);
        const float y = rpdsp::osc_prism(inc, 1.5f * std::sin(i * 0.011f),
                                       1.5f * std::cos(i * 0.007f), state.data());
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) <= 1.000003f);
        REQUIRE(state[0] >= 0.0f);
        REQUIRE(state[0] < 1.0f);
    }
}

TEST_CASE("Braid decouples into an accurately tuned damped mode", "[rpdsp][recipes]") {
    constexpr double pi = 3.14159265358979323846;
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        CAPTURE(fs);
        std::array<float, 4> state{};
        const float g = static_cast<float>(std::tan(pi * 440.0 / fs));
        constexpr float loss = 0.002f;
        for (int i = 0; i < 2048; ++i) {
            const float y = rpdsp::res_braid(i == 0 ? 1.0f : 0.0f, g, 0.2f,
                                           loss, 0.0f, state.data());
            const double reference = std::sqrt(0.5) * std::pow(1.0 - loss, i + 1)
                                   * std::cos(2.0 * pi * 440.0 * (i + 1) / fs);
            REQUIRE_THAT(y, WithinAbs(reference, 2e-5));
            REQUIRE(state[2] == 0.0f);
            REQUIRE(state[3] == 0.0f);
        }
    }
}

TEST_CASE("Braid transfers energy without adding it during modulation", "[rpdsp][recipes]") {
    std::array<float, 4> state{};
    double previousEnergy = 1.0;
    float secondModePeak = 0.0f;
    for (int i = 0; i < 16384; ++i) {
        const float y = rpdsp::res_braid(i == 0 ? 1.0f : 0.0f,
            0.5f + 0.5f * std::sin(i * 0.013f), 0.5f + 0.5f * std::cos(i * 0.021f),
            0.001f, 0.1f * std::sin(i * 0.019f), state.data());
        double energy = 0.0;
        for (float s : state) {
            REQUIRE(std::isfinite(s));
            energy += static_cast<double>(s) * s;
        }
        REQUIRE(energy <= previousEnergy * 0.99801 + 1e-25);
        REQUIRE(std::fabs(y) <= 1.000001f);
        previousEnergy = energy;
        secondModePeak = std::max(secondModePeak, std::fabs(state[2]));
    }
    REQUIRE(secondModePeak > 0.05f);
    REQUIRE(previousEnergy < 1e-12);
    REQUIRE(rpdsp::res_braid(1.0f, 0.1f, 0.2f, 1.0f, 0.1f, state.data()) == 0.0f);
}

TEST_CASE("Braid control endpoints remain finite on sustained excitation", "[rpdsp][recipes]") {
    for (float loss : {0.0f, 0.00001f, 0.1f, 1.0f}) {
        for (float couple : {-0.1f, 0.0f, 0.1f}) {
            CAPTURE(loss, couple);
            std::array<float, 4> state{};
            for (int i = 0; i < 4096; ++i) {
                const float y = rpdsp::res_braid(0.1f, 0.0f, 1.0f, loss, couple, state.data());
                REQUIRE(std::isfinite(y));
                for (float s : state) REQUIRE(std::isfinite(s));
            }
        }
    }
}

TEST_CASE("Memoryfold is transparent at minimum drive without memory", "[rpdsp][recipes]") {
    std::array<float, 1> state{};
    for (int i = -1000; i <= 1000; ++i) {
        const float x = i / 1000.0f;
        REQUIRE_THAT(rpdsp::fx_memoryfold(x, 1.0f, 0.0f, 0.1f, state.data()), WithinAbs(x, 2e-7));
    }
}

TEST_CASE("Memoryfold responds to history and stays bounded through silence", "[rpdsp][recipes]") {
    std::array<float, 1> rising{{-0.8f}}, falling{{0.8f}};
    const float a = rpdsp::fx_memoryfold(0.1f, 4.0f, 1.0f, 0.0f, rising.data());
    const float b = rpdsp::fx_memoryfold(0.1f, 4.0f, 1.0f, 0.0f, falling.data());
    REQUIRE(std::fabs(a - b) > 0.1f);

    for (float drive : {1.0f, 4.0f, 8.0f}) {
        for (float memory : {0.0f, 0.5f, 1.0f}) {
            for (float rate : {0.0f, 0.001f, 1.0f}) {
                CAPTURE(drive, memory, rate);
                std::array<float, 1> state{};
                for (int i = 0; i < 2048; ++i) {
                    const float x = 2.0f * std::sin(i * 0.037f);
                    const float y = rpdsp::fx_memoryfold(x, drive, memory, rate, state.data());
                    REQUIRE(std::isfinite(y));
                    REQUIRE(std::fabs(y) <= 1.000001f);
                }
                for (int i = 0; i < 256; ++i) {
                    REQUIRE_THAT(rpdsp::fx_memoryfold(0.0f, drive, memory, rate, state.data()),
                                 WithinAbs(0.0, 2e-7));
                }
            }
        }
    }
}

TEST_CASE("Hesitate holds then glides monotonically on the segment clock", "[rpdsp][recipes]") {
    std::array<float, 4> state{};
    uint32_t rng = 0;
    // Dyadic increment gives exact sample boundaries, independent of roundoff.
    for (int i = 0; i < 64; ++i) {
        REQUIRE(rpdsp::lfo_hesitate(1.0f / 128.0f, 0.5f, 0.0f, &rng, state.data()) == 0.0f);
    }
    const uint32_t firstRandom = rng;
    REQUIRE(firstRandom != 0u);
    float previous = 0.0f;
    for (int i = 64; i < 128; ++i) {
        const float y = rpdsp::lfo_hesitate(1.0f / 128.0f, 0.5f, 0.0f, &rng, state.data());
        REQUIRE(y <= previous + 1e-6f);  // Seed 0's first target is negative.
        REQUIRE(y >= -1.0f);
        if (i < 127) REQUIRE(rng == firstRandom);
        previous = y;
    }
    REQUIRE(previous < -0.1f);
    REQUIRE(rng != firstRandom);
    for (int i = 0; i < 64; ++i) {
        REQUIRE_THAT(rpdsp::lfo_hesitate(1.0f / 128.0f, 0.5f, 0.0f, &rng, state.data()),
                     WithinAbs(previous, 1e-7));
    }
    const uint32_t pausedRandom = rng;
    for (int i = 0; i < 256; ++i) {
        REQUIRE_THAT(rpdsp::lfo_hesitate(0.0f, 0.5f, 0.0f, &rng, state.data()),
                     WithinAbs(previous, 1e-7));
        REQUIRE(rng == pausedRandom);
    }
}

TEST_CASE("Hesitate is deterministic and bounded for both memory polarities", "[rpdsp][recipes]") {
    for (float memory : {-0.99f, 0.0f, 0.99f}) {
        for (float linger : {0.0f, 0.5f, 0.95f}) {
            CAPTURE(memory, linger);
            std::array<float, 4> a{}, b{}, other{};
            uint32_t seedA = 0, seedB = 0, seedOther = 7;
            float difference = 0.0f;
            for (int i = 0; i < 8192; ++i) {
                const float rate = (i % 5 == 0) ? 1.0f : 1.0f / 128.0f;
                const float y = rpdsp::lfo_hesitate(rate, linger, memory, &seedA, a.data());
                REQUIRE(y == rpdsp::lfo_hesitate(rate, linger, memory, &seedB, b.data()));
                REQUIRE(std::isfinite(y));
                REQUIRE(std::fabs(y) <= 1.000001f);
                const float z = rpdsp::lfo_hesitate(rate, linger, memory, &seedOther, other.data());
                difference = std::max(difference, std::fabs(y - z));
            }
            REQUIRE(difference > 0.001f);
        }
    }
}
