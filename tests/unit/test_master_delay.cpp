// Unit tests for the master-bus delay (src/voice/MasterDelay.h): fractional
// timing, feedback filtering, stability at heavy feedback, and eased mix.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "voice/MasterDelay.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using Catch::Approx;

namespace
{
constexpr float kSampleRate = 48000.0f;
constexpr float kTwoPiF = 6.28318530718f;

float peakRange(const std::vector<float> &y, size_t from, size_t to)
{
    float peak = 0.0f;
    for (size_t i = from; i < to && i < y.size(); ++i)
        peak = std::max(peak, std::fabs(y[i]));
    return peak;
}

// Runs a sine burst followed by silence and returns the wet signal (output
// minus input), so the repeats can be measured window by window.
std::vector<float> burstRepeats(MasterDelay &delay, float freqHz,
                                int burstSamples, int totalSamples)
{
    std::vector<float> wet;
    wet.reserve(static_cast<size_t>(totalSamples));
    for (int n = 0; n < totalSamples; ++n)
    {
        const float x = n < burstSamples
                            ? 0.5f * std::sin(kTwoPiF * freqHz * n / kSampleRate)
                            : 0.0f;
        wet.push_back(delay.process(x) - x);
    }
    return wet;
}
} // namespace

TEST_CASE("MasterDelay is transparent at zero mix", "[master_delay]")
{
    MasterDelay delay;
    delay.prepare(kSampleRate);
    delay.setMix(0.0f);
    for (int n = 0; n < 2000; ++n)
    {
        const float x = 0.4f * std::sin(0.05f * n);
        // Dry path untouched while the feedback loop warms up behind it.
        CHECK(delay.process(x) == x);
    }
}

TEST_CASE("MasterDelay delays by a fractional sample count", "[master_delay]")
{
    // Targets must be set before prepare(): prepare() snaps the eased state
    // to the current targets, which is what a boot-time configuration does.
    MasterDelay delay;
    delay.setMix(1.0f);
    delay.setFeedback(0.0f);
    // 520.3 samples = ~10.8 ms, just above the 10 ms floor.
    delay.setDelaySeconds(520.3f / kSampleRate);
    delay.prepare(kSampleRate);

    std::vector<float> wet;
    wet.push_back(delay.process(1.0f) - 1.0f); // impulse at n = 0
    for (int n = 1; n < 560; ++n)
        wet.push_back(delay.process(0.0f));

    // Peak lands one sample before the integer grid would put it for D = 521
    // and is spread across neighbors: that is the fractional read.
    const auto peak = std::max_element(wet.begin(), wet.end());
    REQUIRE(peak != wet.end());
    CHECK(std::distance(wet.begin(), peak) == 520);
    CHECK(*peak > 0.25f);
    CHECK(std::fabs(wet[521]) > 1e-3f); // energy on both sides of the peak

    // Cubic Lagrange is a partition of unity: the impulse's area survives.
    const float area = std::accumulate(wet.begin(), wet.end(), 0.0f);
    CHECK(area == Approx(1.0f).epsilon(0.001));
    // Pre-/post-ring of the interpolator stays tiny.
    CHECK(peakRange(wet, 0, 518) < 0.1f);
    CHECK(peakRange(wet, 523, wet.size()) < 0.1f);
}

TEST_CASE("MasterDelay feedback lowpass darkens each repeat", "[master_delay]")
{
    constexpr int kDelaySamples = 480; // 10 ms

    MasterDelay dark;
    dark.setMix(1.0f);
    dark.setFeedback(MasterDelay::kDefaultFeedback);
    dark.setDelaySeconds(kDelaySamples / kSampleRate);
    dark.prepare(kSampleRate);
    const std::vector<float> hf =
        burstRepeats(dark, 8000.0f, 400, kDelaySamples * 4);
    const float hf1 = peakRange(hf, kDelaySamples, kDelaySamples * 2);
    const float hf2 = peakRange(hf, kDelaySamples * 2, kDelaySamples * 3);
    const float hf3 = peakRange(hf, kDelaySamples * 3, kDelaySamples * 4);
    REQUIRE(hf1 > 0.2f);
    // 8 kHz sits well above the 2.8 kHz feedback cutoff: every pass loses
    // most of its highs, so repeats fall off much faster than feedback alone.
    CHECK(hf2 / hf1 < 0.5f);
    CHECK(hf3 / hf2 < 0.6f);

    MasterDelay round;
    round.setMix(1.0f);
    round.setFeedback(MasterDelay::kDefaultFeedback);
    round.setDelaySeconds(kDelaySamples / kSampleRate);
    round.prepare(kSampleRate);
    const std::vector<float> lf =
        burstRepeats(round, 200.0f, 400, kDelaySamples * 4);
    const float lf1 = peakRange(lf, kDelaySamples, kDelaySamples * 2);
    const float lf2 = peakRange(lf, kDelaySamples * 2, kDelaySamples * 3);
    const float lf3 = peakRange(lf, kDelaySamples * 3, kDelaySamples * 4);
    REQUIRE(lf1 > 0.2f);
    // 200 Hz passes the lowpass: repeats decay at roughly the feedback rate.
    CHECK(lf2 / lf1 > 0.7f);
    CHECK(lf3 / lf2 > 0.7f);
}

TEST_CASE("MasterDelay stays bounded and drains under hot input", "[master_delay]")
{
    MasterDelay delay;
    delay.setMix(1.0f);
    delay.setFeedback(0.98f); // worst case the setter allows
    delay.setDelaySeconds(0.010f);
    delay.prepare(kSampleRate);

    float worst = 0.0f;
    for (int n = 0; n < static_cast<int>(kSampleRate) * 4; ++n)
    {
        const float out = delay.process(0.9f);
        REQUIRE(std::isfinite(out));
        worst = std::max(worst, std::fabs(out));
    }
    // fastTanh bounds the loop and the DC blocker keeps DC from piling up:
    // output stays at dry + one clean wet copy, never a runaway.
    CHECK(worst < 3.0f);

    float steady = 0.0f;
    for (int n = 0; n < 480; ++n)
        steady = std::max(steady, std::fabs(delay.process(0.9f)));
    CHECK(steady > 1.7f); // dry 0.9 + wet 0.9: additive mix, not a runaway
    CHECK(steady < 2.0f);

    // After input stops the loop drains to silence.
    for (int n = 0; n < static_cast<int>(kSampleRate) * 3; ++n)
        delay.process(0.0f);
    float tail = 0.0f;
    for (int n = 0; n < 480; ++n)
        tail = std::max(tail, std::fabs(delay.process(0.0f)));
    CHECK(tail < 0.05f);
}

TEST_CASE("MasterDelay eases mix without stepping", "[master_delay]")
{
    MasterDelay delay;
    delay.setMix(0.0f);
    delay.setFeedback(0.0f);
    delay.setDelaySeconds(0.010f);
    delay.prepare(kSampleRate);
    for (int n = 0; n < 1000; ++n)
        delay.process(0.25f); // line filled with 0.25, output is dry-only

    delay.setMix(1.0f);
    float previousOut = 0.25f;
    float maxStep = 0.0f;
    for (int n = 0; n < 9600; ++n)
    {
        const float out = delay.process(0.25f);
        maxStep = std::max(maxStep, std::fabs(out - previousOut));
        CHECK(out >= previousOut - 1e-6f); // monotonic glide toward dry+wet
        previousOut = out;
    }
    CHECK(previousOut == Approx(0.5f).epsilon(0.001)); // converged
    CHECK(maxStep < 0.01f);                            // never a jump
}

TEST_CASE("MasterDelay clamps its time target to the line", "[master_delay]")
{
    MasterDelay delay;
    delay.prepare(kSampleRate);
    delay.setDelaySeconds(30.0f);
    CHECK(delay.delaySamplesTarget() ==
          Approx(delay.maxDelaySeconds() * kSampleRate).margin(0.5f));
    delay.setDelaySeconds(-5.0f);
    CHECK(delay.delaySamplesTarget() ==
          Approx(MasterDelay::kMinDelaySeconds * kSampleRate).margin(0.5f));
}
