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

// Wet signal (output minus input) for a unit impulse followed by silence.
std::vector<float> impulseResponse(MasterDelay &delay, int samples)
{
    std::vector<float> wet;
    wet.reserve(static_cast<size_t>(samples));
    for (int n = 0; n < samples; ++n)
    {
        const float x = n == 0 ? 1.0f : 0.0f;
        wet.push_back(delay.process(x) - x);
    }
    return wet;
}

// `prime` silent samples, then an impulse and `samples` more. Right after a mode
// switch the delay mutes its tap until it holds enough fresh samples, so the
// priming stretch is also where any leaked buffer contents would show up.
std::vector<float> primedImpulseResponse(MasterDelay &delay, int prime, int samples)
{
    std::vector<float> wet;
    wet.reserve(static_cast<size_t>(prime + samples));
    for (int n = 0; n < prime; ++n)
        wet.push_back(delay.process(0.0f));
    for (const float w : impulseResponse(delay, samples))
        wet.push_back(w);
    return wet;
}

float maxAbsDiff(const std::vector<float> &a, const std::vector<float> &b)
{
    REQUIRE(a.size() == b.size());
    float worst = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, std::fabs(a[i] - b[i]));
    return worst;
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
    // 8 kHz sits well above the 2.5 kHz feedback cutoff: every pass loses
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
    delay.setFeedback(1.0f); // full 100% feedback, with the existing saturated loop
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
    // The delay branch tuned the mix time constant to 45 ms. Allow 400 ms
    // (nearly nine time constants) before checking the settled value.
    for (int n = 0; n < 19200; ++n)
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

TEST_CASE("Tempo path reaches a whole note at 45 BPM without clamping", "[master_delay][delay_sync]")
{
    MasterDelay delay;
    delay.prepare(kSampleRate);
    delay.setSynced(true);
    delay.setMix(1.0f);
    delay.setFeedback(0.0f);
    constexpr float kWholeSeconds = 4.0f * 60.0f / 45.0f;
    REQUIRE(delay.maxSyncedDelaySeconds() >= kWholeSeconds);
    delay.setDelaySeconds(kWholeSeconds);
    REQUIRE(delay.delaySamplesTarget() == Approx(256000.0f).margin(0.1f));

    float peak = 0.0f;
    int peakAt = 0;
    for (int n = 0; n < 256040; ++n)
    {
        const float out = delay.process(n == 0 ? 1.0f : 0.0f);
        if (n < 255960) continue;
        if (std::fabs(out) > peak)
        {
            peak = std::fabs(out);
            peakAt = n;
        }
    }
    CHECK(peak > 0.005f);
    CHECK(std::abs(peakAt - 256000) <= 16);
}

TEST_CASE("Changing delay modes does not replay stale buffer contents", "[master_delay][delay_sync]")
{
    MasterDelay delay;
    delay.prepare(kSampleRate);
    delay.setMix(1.0f);
    delay.setFeedback(0.0f);
    delay.setDelaySeconds(0.010f);
    delay.reset();
    for (int n = 0; n < 2000; ++n)
        delay.process(0.8f);
    delay.setSynced(true);
    delay.setDelaySeconds(0.050f);
    for (int n = 0; n < 1200; ++n)
        CHECK(std::fabs(delay.process(0.0f)) < 1e-5f);
    delay.setSynced(false);
    delay.setDelaySeconds(0.010f);
    for (int n = 0; n < 300; ++n)
        CHECK(std::fabs(delay.process(0.0f)) < 1e-5f);
}

TEST_CASE("Synced repeat line stays bounded at full feedback", "[master_delay][delay_sync]")
{
    MasterDelay delay;
    delay.prepare(kSampleRate);
    delay.setSynced(true);
    delay.setDelaySeconds(0.0125f); // fastest 64th triplet at 200 BPM
    delay.setMix(1.0f);
    delay.setFeedback(1.0f);
    float peak = 0.0f;
    bool finite = true;
    for (int n = 0; n < 96000; ++n)
    {
        const float out = delay.process(n < 48000 ? 0.9f : 0.0f);
        finite = finite && std::isfinite(out);
        peak = std::max(peak, std::fabs(out));
    }
    CHECK(finite);
    CHECK(peak > 0.9f);
    CHECK(peak < 4.0f);
}

TEST_CASE("MasterDelay's two rings share one block of memory", "[master_delay][delay_sync]")
{
    // A private synced ring would add 64 KiB on top of the float ring. The margin
    // covers the filters, indices and targets (host pointers are wider than the
    // firmware's, so it is generous).
    constexpr size_t kFloatRingBytes = MasterDelay::kCapacitySamples * sizeof(float);
    CHECK(sizeof(MasterDelay) >= kFloatRingBytes);
    CHECK(sizeof(MasterDelay) < kFloatRingBytes + 512);
}

TEST_CASE("The delay line is 375 ms, half its old size, and the synced ring still fits", "[master_delay][delay_sync]")
{
    // 72,000 bytes of heap went to the looper: the shared block holds 18,004 words, not 36,004.
    static_assert(MasterDelay::kCapacitySamples == 18004);
    static_assert(MasterDelay::kMaxDelaySamples == 18000);
    static_assert(MasterDelay::kCapacitySamples * sizeof(uint32_t) == 72016);
    CHECK(sizeof(MasterDelay) < 72016 + 512);
    MasterDelay delay;
    delay.prepare(kSampleRate);
    CHECK(delay.maxDelaySeconds() == Approx(0.375f));
    delay.setDelaySeconds(0.75f); // the old ceiling now clamps to the new one
    CHECK(delay.delaySamplesTarget() == Approx(18000.0f).margin(0.5f));
    // The tempo-synced ring is untouched: a whole note at 45 BPM still fits.
    CHECK(delay.maxSyncedDelaySeconds() >= 4.0f * 60.0f / 45.0f);
}

TEST_CASE("Switching delay modes over the shared block matches a fresh delay", "[master_delay][delay_sync]")
{
    constexpr int kPrime = 2600;    // longer than the 2400-sample delay plus the guard
    constexpr int kResponse = 4000;
    // Targets go in before prepare(), which snaps the eased state to them.
    const auto configure = [](MasterDelay &d)
    {
        d.setMix(1.0f);
        d.setFeedback(0.0f);
        d.setDelaySeconds(0.05f); // 2400 samples
        d.prepare(kSampleRate);
    };

    MasterDelay freshFast;
    configure(freshFast);
    const std::vector<float> expectedFast = primedImpulseResponse(freshFast, kPrime, kResponse);
    MasterDelay freshSync;
    freshSync.setSynced(true);
    configure(freshSync);
    const std::vector<float> expectedSync = primedImpulseResponse(freshSync, kPrime, kResponse);
    REQUIRE(peakRange(expectedFast, kPrime + 2300, kPrime + 2500) > 0.25f);
    REQUIRE(peakRange(expectedSync, kPrime + 2300, kPrime + 2500) > 0.005f);
    REQUIRE(peakRange(expectedFast, 0, kPrime) == 0.0f);
    REQUIRE(peakRange(expectedSync, 0, kPrime) == 0.0f);

    MasterDelay delay;
    configure(delay);
    // Dirty the whole float ring with non-zero data, then switch to the synced
    // ring: its int16 slots sit on those same bytes, and none may be heard.
    for (int n = 0; n < 40000; ++n)
        delay.process(0.8f);
    delay.setSynced(true);
    delay.setDelaySeconds(0.05f);
    CHECK(maxAbsDiff(primedImpulseResponse(delay, kPrime, kResponse), expectedSync) < 1e-6f);

    // Dirty the synced ring the same way and go back: the float ring's slots hold
    // int16 pairs now, and again none may be heard.
    for (int n = 0; n < 40000; ++n)
        delay.process(0.8f);
    delay.setSynced(false);
    delay.setDelaySeconds(0.05f);
    CHECK(maxAbsDiff(primedImpulseResponse(delay, kPrime, kResponse), expectedFast) < 1e-6f);
}
