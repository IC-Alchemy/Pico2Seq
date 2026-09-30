#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "voice/MasterReverb.h"
#include "voice/VoiceManager.h"
#include <rpdsp/dark_reverb.h>
#include <rpdsp/realtime.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

// MasterReverb is the audio-owned adapter around rpdsp::DarkReverb. These tests
// pin its contract: mix zero is the legacy bus, controls cross threads lock-free
// and coherently, coefficient work is bounded, freeze does not click, and the
// render path never allocates.

// ---- allocation counting -------------------------------------------------------
// Global replacement (one definition for the whole test executable). Counting is
// off except inside an AllocationWindow, so Catch2 and every other suite are
// unaffected.
namespace
{
std::atomic<bool> g_countAllocations{false};
std::atomic<std::uint64_t> g_allocations{0};

void noteAllocation() noexcept
{
    if (g_countAllocations.load(std::memory_order_relaxed))
        g_allocations.fetch_add(1, std::memory_order_relaxed);
}

struct AllocationWindow
{
    AllocationWindow()
    {
        g_allocations.store(0);
        g_countAllocations.store(true);
    }
    ~AllocationWindow() { g_countAllocations.store(false); }
    std::uint64_t count() const { return g_allocations.load(); }
};
} // namespace

void *operator new(std::size_t size)
{
    noteAllocation();
    if (void *p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return operator new(size); }
void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    noteAllocation();
    return std::malloc(size ? size : 1);
}
void *operator new[](std::size_t size, const std::nothrow_t &) noexcept
{
    noteAllocation();
    return std::malloc(size ? size : 1);
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { std::free(p); }

namespace
{
using Catch::Approx;
constexpr float kRate = 48000.0f;

std::uint32_t bitsOf(float value)
{
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// Cutting a render into different calls must not change it. IEEE builds compare bit
// for bit. A -ffast-math build may contract or reassociate the block and per-sample
// loops differently (rpdsp's own tests allow the same slack), so it compares within
// 2e-4 relative; on GCC 13.3 x86-64 the fast-math run was bit-exact as well.
void requireSameSample(float split, float reference)
{
#ifdef __FAST_MATH__
    REQUIRE(std::fabs(split - reference) <= 2.0e-4f * (1.0f + std::fabs(reference)));
#else
    REQUIRE(bitsOf(split) == bitsOf(reference));
#endif
}

// A decaying-free burst of noise + a tone, then silence: something a tank can ring on.
std::vector<float> burst(std::size_t frames, std::size_t burstFrames, float amplitude, std::uint32_t seed = 41)
{
    rpdsp::XorShift32 noise(seed);
    std::vector<float> out(frames, 0.0f);
    for (std::size_t n = 0; n < std::min(frames, burstFrames); ++n)
        out[n] = amplitude * (0.5f * std::sin(6.2831853f * 220.0f * static_cast<float>(n) / kRate) +
                              0.5f * noise.nextBipolar());
    return out;
}

struct Wet
{
    std::vector<float> left, right;
};

// Renders `dry` through the adapter, cutting it into calls of the given sizes
// (cycled). The wet scratch is sized for the largest call.
Wet render(MasterReverb &reverb, const std::vector<float> &dry, const std::vector<std::uint32_t> &sizes)
{
    Wet out;
    out.left.resize(dry.size());
    out.right.resize(dry.size());
    std::vector<float> wl(*std::max_element(sizes.begin(), sizes.end()));
    std::vector<float> wr(wl.size());
    std::size_t at = 0;
    for (std::size_t call = 0; at < dry.size(); ++call)
    {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(sizes[call % sizes.size()], dry.size() - at));
        reverb.render(dry.data() + at, wl.data(), wr.data(), n);
        std::copy(wl.begin(), wl.begin() + n, out.left.begin() + at);
        std::copy(wr.begin(), wr.begin() + n, out.right.begin() + at);
        at += n;
    }
    return out;
}

// One control tick (64 frames) of silence, `ticks` times.
void tick(MasterReverb &reverb, unsigned ticks)
{
    std::array<float, MasterReverb::kControlQuantum> dry{}, wl{}, wr{};
    for (unsigned i = 0; i < ticks; ++i)
        reverb.render(dry.data(), wl.data(), wr.data(), MasterReverb::kControlQuantum);
}

double rmsOf(const std::vector<float> &x, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t n = from; n < to; ++n)
        sum += static_cast<double>(x[n]) * x[n];
    return std::sqrt(sum / static_cast<double>(to - from));
}

double maxSecondDifference(const std::vector<float> &y, std::size_t from, std::size_t to)
{
    double peak = 0.0;
    for (std::size_t n = from; n < to; ++n)
        peak = std::max(peak, std::fabs(static_cast<double>(y[n]) - 2.0 * y[n - 1] + y[n - 2]));
    return peak;
}

// Largest 64-sample-tick second difference in the ~200 ms after `event`, relative to
// the largest second difference in the 100 ms before it. A step in the recirculating
// signal shows as ticks well above that floor (the engine's own freeze measured
// 5x-13x on a dark tail, echoing through the tank for several ticks); a clean
// transition stays near 1.
double worstTickOverFloor(const std::vector<float> &y, std::size_t event)
{
    const double floor = maxSecondDifference(y, event - 4800, event);
    double worst = 0.0;
    for (std::size_t t = 0; t < 150; ++t)
        worst = std::max(worst, maxSecondDifference(y, event + t * 64, event + (t + 1) * 64));
    return floor > 0.0 ? worst / floor : 0.0;
}

ReverbSettings otherSettings()
{
    ReverbSettings s;
    s.mix = 0.8f;
    s.decaySeconds = 3.0f;
    s.dampingHz = 1500.0f;
    s.lowCutHz = 200.0f;
    s.diffusion = 0.2f;
    s.modDepth = 0.9f;
    s.modRateHz = 2.0f;
    s.width = 0.3f;
    return s;
}

bool sameSettings(const ReverbSettings &a, const ReverbSettings &b)
{
    return a == b;
}
} // namespace

TEST_CASE("Mix zero is the dry bus exactly while the tank keeps evolving", "[reverb][master_reverb]")
{
    auto reverb = std::make_unique<MasterReverb>();
    REQUIRE(reverb->settings().mix == 0.0f); // upgraded projects sound unchanged
    const auto dry = burst(24000, 6000, 0.3f);
    const Wet wet = render(*reverb, dry, {64});
    for (std::size_t n = 0; n < dry.size(); ++n)
    {
        REQUIRE(bitsOf(wet.left[n]) == bitsOf(dry[n]));
        REQUIRE(bitsOf(wet.right[n]) == bitsOf(dry[n]));
    }

    // Raising the mix reveals the tail the tank kept building underneath.
    reverb->setMix(1.0f);
    const Wet tail = render(*reverb, std::vector<float>(48000, 0.0f), {64});
    CHECK(rmsOf(tail.left, 9600, 24000) > 1.0e-3);
    CHECK(rmsOf(tail.right, 9600, 24000) > 1.0e-3);

    // Nothing was ever fed to a fresh tank: silence in is exactly silence out.
    auto fresh = std::make_unique<MasterReverb>();
    fresh->setMix(1.0f);
    const Wet quiet = render(*fresh, std::vector<float>(24000, 0.0f), {64});
    for (std::size_t n = 0; n < quiet.left.size(); ++n)
    {
        REQUIRE(quiet.left[n] == 0.0f);
        REQUIRE(quiet.right[n] == 0.0f);
    }
}

TEST_CASE("The wet path is the engine driven with the mono bus on both inputs", "[reverb][master_reverb]")
{
    ReverbSettings s;
    s.mix = 1.0f;
    s.decaySeconds = 7.0f;
    s.dampingHz = 2400.0f;
    s.lowCutHz = 60.0f;
    s.diffusion = 0.6f;
    s.modDepth = 0.3f;
    s.modRateHz = 1.5f;
    s.width = 1.3f;
    auto reverb = std::make_unique<MasterReverb>();
    reverb->publishSettings(s);
    reverb->prepare(kRate); // re-derives every coefficient from the published targets

    auto reference = std::make_unique<MasterReverb::Engine>();
    reference->prepare(kRate);
    reference->setMix(1.0f);
    reference->setDecaySeconds(s.decaySeconds);
    reference->setDampingHz(s.dampingHz);
    reference->setLowCutHz(s.lowCutHz);
    reference->setDiffusion(s.diffusion);
    reference->setModDepth(s.modDepth);
    reference->setModRateHz(s.modRateHz);
    reference->setWidth(s.width);

    const auto dry = burst(3 * 48000, 30000, 0.25f);
    // Irregular call sizes, including 1, primes and calls longer than one tick.
    const Wet wet = render(*reverb, dry, {1, 2, 3, 5, 7, 11, 64, 63, 65, 130, 257, 400, 1000});
    std::vector<float> refLeft(dry.size()), refRight(dry.size());
    reference->process(dry.data(), dry.data(), refLeft.data(), refRight.data(), dry.size());
    double worst = 0.0;
    for (std::size_t n = 0; n < dry.size(); ++n)
    {
        worst = std::max(worst, static_cast<double>(std::fabs(wet.left[n] - refLeft[n])));
        worst = std::max(worst, static_cast<double>(std::fabs(wet.right[n] - refRight[n])));
    }
    // The only difference is the engine's own wet-only blend rounding (dry + (wet - dry)).
    CHECK(worst < 2.0e-6);
    CHECK(rmsOf(wet.left, 40000, dry.size()) > 1.0e-3);
    // Width 1.3: the two wet channels are genuinely different signals.
    double difference = 0.0;
    for (std::size_t n = 40000; n < dry.size(); ++n)
        difference += std::fabs(wet.left[n] - wet.right[n]);
    CHECK(difference / static_cast<double>(dry.size() - 40000) > 1.0e-3);
}

TEST_CASE("A mono bus fed to both engine inputs reaches the tank at the intended level", "[reverb][master_reverb]")
{
    // The engine sums L+R. Driving both inputs from the one post-delay pointer must
    // give the tank exactly twice what a left-only feed would, and the adapter must
    // do the former.
    const auto dry = burst(24000, 12000, 0.05f); // far below the tank input clamp
    std::vector<float> zero(dry.size(), 0.0f), a(dry.size()), b(dry.size()), unused(dry.size());
    auto both = std::make_unique<MasterReverb::Engine>();
    auto leftOnly = std::make_unique<MasterReverb::Engine>();
    for (auto *engine : {both.get(), leftOnly.get()})
    {
        engine->prepare(kRate);
        engine->setMix(1.0f);
    }
    both->process(dry.data(), dry.data(), a.data(), unused.data(), dry.size());
    leftOnly->process(dry.data(), zero.data(), b.data(), unused.data(), dry.size());
    const double ratio = rmsOf(a, 12000, 24000) / rmsOf(b, 12000, 24000);
    CHECK(ratio == Approx(2.0).epsilon(0.01));

    auto adapter = std::make_unique<MasterReverb>();
    adapter->setMix(1.0f);
    adapter->prepare(kRate);
    const Wet wet = render(*adapter, dry, {64});
    CHECK(rmsOf(wet.left, 12000, 24000) / rmsOf(a, 12000, 24000) == Approx(1.0).epsilon(0.02));
}

TEST_CASE("Mix eases as one shared value and lands exactly on its target", "[reverb][master_reverb]")
{
    auto reverb = std::make_unique<MasterReverb>();
    reverb->setWidth(1.5f);
    reverb->prepare(kRate);
    auto reference = std::make_unique<MasterReverb::Engine>();
    reference->prepare(kRate);
    reference->setMix(1.0f);
    reference->setWidth(1.5f);

    const auto dry = burst(24000, 24000, 0.3f);
    // Establish the tank at mix zero, then raise the mix at a control-tick boundary.
    reverb->setMix(1.0f);
    const Wet wet = render(*reverb, dry, {64});
    std::vector<float> refLeft(dry.size()), refRight(dry.size());
    reference->process(dry.data(), dry.data(), refLeft.data(), refRight.data(), dry.size());

    const float alpha = 1.0f - std::exp(-1.0f / (MasterReverb::kEaseTauSeconds * kRate));
    float expected = 0.0f;
    std::size_t compared = 0;
    for (std::size_t n = 0; n < dry.size(); ++n)
    {
        if (expected != 1.0f)
        {
            expected += alpha * (1.0f - expected);
            if (1.0f - expected < 1.0e-5f)
                expected = 1.0f;
        }
        for (int channel = 0; channel < 2; ++channel)
        {
            const float wetRef = channel == 0 ? refLeft[n] : refRight[n];
            const float out = channel == 0 ? wet.left[n] : wet.right[n];
            if (std::fabs(wetRef - dry[n]) > 0.05f)
            {
                // Same implied mix on both channels, following the one-pole exactly.
                CHECK(((out - dry[n]) / (wetRef - dry[n])) == Approx(expected).margin(2.0e-3));
                ++compared;
            }
        }
    }
    CHECK(compared > 1000);
    CHECK(reverb->currentMix() == 1.0f); // exact at rest, not merely close

    reverb->setMix(0.0f);
    render(*reverb, std::vector<float>(48000, 0.0f), {64});
    CHECK(reverb->currentMix() == 0.0f);
    const Wet after = render(*reverb, dry, {64});
    for (std::size_t n = 0; n < dry.size(); ++n)
    {
        REQUIRE(bitsOf(after.left[n]) == bitsOf(dry[n]));
        REQUIRE(bitsOf(after.right[n]) == bitsOf(dry[n]));
    }
}

TEST_CASE("Reverb controls publish lock-free targets and clamp them", "[reverb][master_reverb]")
{
    auto reverb = std::make_unique<MasterReverb>();
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    constexpr float inf = std::numeric_limits<float>::infinity();
    using namespace ReverbParams;

    reverb->setMix(2.0f);
    CHECK(reverb->settings().mix == 1.0f);
    reverb->setMix(-1.0f);
    CHECK(reverb->settings().mix == 0.0f);
    reverb->setMix(nan);
    CHECK(reverb->settings().mix == kMixDefault);
    reverb->setDecaySeconds(0.0f);
    CHECK(reverb->settings().decaySeconds == kDecayMin);
    reverb->setDecaySeconds(1.0e9f);
    CHECK(reverb->settings().decaySeconds == kDecayMax);
    reverb->setDecaySeconds(inf);
    CHECK(reverb->settings().decaySeconds == kDecayDefault); // non-finite never becomes an extreme
    reverb->setDampingHz(50.0f);
    CHECK(reverb->settings().dampingHz == kDampingMin);
    reverb->setDampingHz(1.0e6f);
    CHECK(reverb->settings().dampingHz == kDampingMax);
    reverb->setLowCutHz(1.0f);
    CHECK(reverb->settings().lowCutHz == kLowCutMin);
    reverb->setLowCutHz(9000.0f);
    CHECK(reverb->settings().lowCutHz == kLowCutMax);
    reverb->setDiffusion(3.0f);
    CHECK(reverb->settings().diffusion == 1.0f);
    reverb->setModDepth(-2.0f);
    CHECK(reverb->settings().modDepth == 0.0f);
    reverb->setModRateHz(0.0f);
    CHECK(reverb->settings().modRateHz == kModRateMin);
    reverb->setModRateHz(100.0f);
    CHECK(reverb->settings().modRateHz == kModRateMax);
    reverb->setWidth(-1.0f);
    CHECK(reverb->settings().width == 0.0f);
    reverb->setWidth(9.0f);
    CHECK(reverb->settings().width == 2.0f);
    reverb->setFreeze(true);
    CHECK(reverb->settings().freeze);
    reverb->setFreeze(false);
    CHECK_FALSE(reverb->settings().freeze);

    // A snapshot is sanitised the same way and read back whole.
    ReverbSettings hostile;
    hostile.mix = nan;
    hostile.decaySeconds = -4.0f;
    hostile.dampingHz = inf;
    hostile.width = 99.0f;
    REQUIRE(reverb->publishSettings(hostile));
    const ReverbSettings back = reverb->settings();
    CHECK(back.mix == kMixDefault);
    CHECK(back.decaySeconds == kDecayMin);
    CHECK(back.dampingHz == kDampingDefault);
    CHECK(back.width == kWidthMax);
    CHECK(sameSettings(back, hostile.sanitized()));
}

TEST_CASE("A snapshot applies every field in the same control tick", "[reverb][master_reverb]")
{
    const ReverbSettings target = otherSettings();
    ReverbSettings start; // defaults

    auto viaSnapshot = std::make_unique<MasterReverb>();
    REQUIRE(viaSnapshot->publishSettings(target));
    auto viaSetters = std::make_unique<MasterReverb>();
    viaSetters->setMix(target.mix);
    viaSetters->setDecaySeconds(target.decaySeconds);
    viaSetters->setDampingHz(target.dampingHz);
    viaSetters->setLowCutHz(target.lowCutHz);
    viaSetters->setDiffusion(target.diffusion);
    viaSetters->setModDepth(target.modDepth);
    viaSetters->setModRateHz(target.modRateHz);
    viaSetters->setWidth(target.width);

    for (auto *reverb : {viaSnapshot.get(), viaSetters.get()})
    {
        std::array<float, 1> dry{0.0f}, wl{}, wr{};
        reverb->render(dry.data(), wl.data(), wr.data(), 1); // exactly one tick
        const ReverbSettings &now = reverb->appliedSettings();
        // Every field left its default in that one tick, none reached the far value
        // in a single step, and none is still waiting for a later tick.
        CHECK(now.decaySeconds < start.decaySeconds);
        CHECK(now.decaySeconds > target.decaySeconds);
        CHECK(now.dampingHz < start.dampingHz);
        CHECK(now.dampingHz > target.dampingHz);
        CHECK(now.lowCutHz > start.lowCutHz);
        CHECK(now.lowCutHz < target.lowCutHz);
        CHECK(now.diffusion < start.diffusion);
        CHECK(now.diffusion > target.diffusion);
        CHECK(now.modDepth > start.modDepth);
        CHECK(now.modDepth < target.modDepth);
        CHECK(now.modRateHz > start.modRateHz);
        CHECK(now.modRateHz < target.modRateHz);
        CHECK(now.width < start.width);
        CHECK(now.width > target.width);
    }
    // Both routes end in the same bits after the same number of ticks.
    tick(*viaSnapshot, 5);
    tick(*viaSetters, 5);
    CHECK(sameSettings(viaSnapshot->appliedSettings(), viaSetters->appliedSettings()));
}

TEST_CASE("Later single edits beat an earlier snapshot, even a return to the old value", "[reverb][master_reverb]")
{
    {
        // Snapshot says decay 3; the performer then puts decay back to the value it
        // had before. Both land before the audio thread looks: the last edit wins.
        auto reverb = std::make_unique<MasterReverb>();
        ReverbSettings snap = otherSettings();
        REQUIRE(reverb->publishSettings(snap));
        reverb->setDecaySeconds(ReverbParams::kDecayDefault);
        tick(*reverb, 600);
        CHECK(reverb->appliedSettings().decaySeconds == ReverbParams::kDecayDefault);
        CHECK(reverb->appliedSettings().dampingHz == snap.dampingHz); // the rest of the snapshot landed
        CHECK(reverb->appliedSettings().width == snap.width);
    }
    {
        // Single edit first, snapshot after: the snapshot is newer and wins.
        auto reverb = std::make_unique<MasterReverb>();
        reverb->setDecaySeconds(9.0f);
        ReverbSettings snap = otherSettings();
        REQUIRE(reverb->publishSettings(snap));
        tick(*reverb, 600);
        CHECK(reverb->appliedSettings().decaySeconds == snap.decaySeconds);
    }
    {
        // Freeze edits ride the same revision.
        auto reverb = std::make_unique<MasterReverb>();
        ReverbSettings snap = otherSettings();
        snap.freeze = true;
        REQUIRE(reverb->publishSettings(snap));
        reverb->setFreeze(false);
        tick(*reverb, 200);
        CHECK_FALSE(reverb->appliedSettings().freeze);
    }
}

TEST_CASE("A full snapshot ring still converges on the newest values", "[reverb][master_reverb]")
{
    auto reverb = std::make_unique<MasterReverb>();
    ReverbSettings last;
    unsigned accepted = 0;
    for (unsigned i = 0; i < 12; ++i)
    {
        ReverbSettings s = otherSettings();
        s.mix = static_cast<float>(i) / 12.0f;
        s.decaySeconds = 1.0f + static_cast<float>(i);
        accepted += reverb->publishSettings(s) ? 1u : 0u;
        last = s;
    }
    CHECK(accepted == 4); // the ring holds four; the rest arrive through the targets
    CHECK(sameSettings(reverb->settings(), last.sanitized()));
    tick(*reverb, 900);
    CHECK(reverb->appliedSettings().decaySeconds == last.decaySeconds);
    CHECK(reverb->currentMix() == last.mix);
    // The ring drained, so a later snapshot is accepted again.
    CHECK(reverb->publishSettings(last));
}

TEST_CASE("Coefficient controls ease to exact targets and then do no setter work", "[reverb][master_reverb]")
{
    auto reverb = std::make_unique<MasterReverb>();
    const ReverbSettings target = otherSettings();
    reverb->publishSettings(target);
    ReverbSettings previous = reverb->appliedSettings();
    unsigned changingTicks = 0;
    unsigned settledTick = 0;
    for (unsigned t = 1; t <= 1200; ++t)
    {
        tick(*reverb, 1);
        const ReverbSettings &now = reverb->appliedSettings();
        if (!sameSettings(now, previous))
        {
            ++changingTicks;
            settledTick = t;
        }
        previous = now;
    }
    // ~30 ms time constant: a full-range move is done in well under a second and
    // ends bit-exactly on the published values.
    CHECK(settledTick < 700);
    CHECK(changingTicks > 20); // it eased; it did not step
    CHECK(previous.decaySeconds == target.decaySeconds);
    CHECK(previous.dampingHz == target.dampingHz);
    CHECK(previous.lowCutHz == target.lowCutHz);
    CHECK(previous.diffusion == target.diffusion);
    CHECK(previous.modDepth == target.modDepth);
    CHECK(previous.modRateHz == target.modRateHz);
    CHECK(previous.width == target.width);
}

TEST_CASE("Splitting the render into any calls does not change the output", "[reverb][master_reverb]")
{
    // Control ticks are counted in frames, so cut points cannot move them. Events
    // land on frames every split shares.
    constexpr std::size_t kSegment = 48000;
    const auto dry = burst(6 * kSegment, 2 * kSegment, 0.3f);
    ReverbSettings snap = otherSettings();
    snap.mix = 0.7f;
    const auto applyEvent = [&](MasterReverb &reverb, unsigned segment) {
        switch (segment)
        {
        case 1: reverb.setMix(1.0f); reverb.setDecaySeconds(3.0f); reverb.setDampingHz(1200.0f); break;
        case 2: reverb.publishSettings(snap); break;
        case 3: reverb.setFreeze(true); break;
        case 4: reverb.setFreeze(false); reverb.setWidth(0.0f); break;
        default: break;
        }
    };
    const auto run = [&](const std::vector<std::uint32_t> &sizes) {
        auto reverb = std::make_unique<MasterReverb>();
        Wet out;
        for (unsigned segment = 0; segment < 6; ++segment)
        {
            applyEvent(*reverb, segment);
            const std::vector<float> part(dry.begin() + segment * kSegment, dry.begin() + (segment + 1) * kSegment);
            const Wet w = render(*reverb, part, sizes);
            out.left.insert(out.left.end(), w.left.begin(), w.left.end());
            out.right.insert(out.right.end(), w.right.begin(), w.right.end());
        }
        return out;
    };
    const Wet reference = run({64});
    for (const auto &sizes : {std::vector<std::uint32_t>{1}, {3, 5, 7, 11, 13}, {63, 65, 1, 130}, {1000, 3, 257}, {kSegment}})
    {
        const Wet split = run(sizes);
        for (std::size_t n = 0; n < reference.left.size(); ++n)
        {
            requireSameSample(split.left[n], reference.left[n]);
            requireSameSample(split.right[n], reference.right[n]);
        }
    }
}

TEST_CASE("Freeze holds the tail and its transitions do not click", "[reverb][master_reverb][freeze]")
{
    constexpr std::size_t on = 3 * 48000;
    constexpr std::size_t off = 5 * 48000;
    for (const float decay : {0.2f, 5.0f, 60.0f})
        for (const float damping : {800.0f, 3000.0f})
        {
            CAPTURE(decay, damping);
            auto reverb = std::make_unique<MasterReverb>();
            reverb->setMix(1.0f);
            reverb->setDecaySeconds(decay);
            reverb->setDampingHz(damping);
            reverb->prepare(kRate);
            std::vector<float> dry(8 * 48000, 0.0f);
            // Long tails are frozen 1.5 s after the primer so the pre-event floor is the
            // settled tail (a click is only visible against a quiet floor); a 0.2 s
            // decay is frozen while it still rings, so only its hold is checked.
            const std::size_t primerEnd = decay >= 5.0f ? on - 72000 : on - 12000;
            const auto primer = burst(12000, 12000, 0.3f);
            std::copy(primer.begin(), primer.end(), dry.begin() + (primerEnd - 12000));

            Wet wet;
            for (const auto &segment : {std::pair<std::size_t, std::size_t>{0, on}, {on, off}, {off, dry.size()}})
            {
                if (segment.first == on) reverb->setFreeze(true);
                if (segment.first == off) reverb->setFreeze(false);
                const std::vector<float> part(dry.begin() + segment.first, dry.begin() + segment.second);
                const Wet w = render(*reverb, part, {64});
                wet.left.insert(wet.left.end(), w.left.begin(), w.left.end());
            }
            // Measured: <= 1.8x on every case here; the engine's own immediate freeze
            // reaches 5.2x (5 s decay) and 13.5x (60 s decay) at 800 Hz damping.
            if (decay >= 5.0f)
            {
                CHECK(worstTickOverFloor(wet.left, on) <= 3.0);
                CHECK(worstTickOverFloor(wet.left, off) <= 3.0);
            }

            // The tail is held through the freeze (not decaying away) and released
            // to its own decay afterwards.
            const double early = rmsOf(wet.left, on + 24000, on + 28800);
            const double late = rmsOf(wet.left, off - 4800, off);
            CHECK(early > 1.0e-3);
            CHECK(late > 0.5 * early);
            CHECK(late < 2.5 * early); // a frozen loop of unity gain does not run away
            if (decay <= 5.0f)
                CHECK(rmsOf(wet.left, dry.size() - 4800, dry.size()) < 0.05 * late);
        }
}

TEST_CASE("Freeze engages only after its ramp and a rapid toggle never engages", "[reverb][master_reverb][freeze]")
{
    auto reverb = std::make_unique<MasterReverb>();
    reverb->setMix(1.0f);
    reverb->setFreeze(true);
    std::array<float, MasterReverb::kControlQuantum> dry{}, wl{}, wr{};
    unsigned engagedAt = 0;
    for (unsigned t = 1; t <= 200 && !engagedAt; ++t)
    {
        reverb->render(dry.data(), wl.data(), wr.data(), MasterReverb::kControlQuantum);
        if (reverb->appliedSettings().freeze) engagedAt = t;
    }
    // Arming starts on the first tick and engages kFreezeRampTicks ticks later.
    CHECK(engagedAt == MasterReverb::kFreezeRampTicks + 1);
    reverb->setFreeze(false);
    tick(*reverb, 2);
    CHECK_FALSE(reverb->appliedSettings().freeze);

    // Toggling every tick aborts the arming each time: the engine is never frozen
    // and everything stays finite and bounded.
    const auto noisy = burst(64 * 400, 64 * 400, 0.5f);
    float peak = 0.0f;
    for (unsigned t = 0; t < 400; ++t)
    {
        reverb->setFreeze((t & 1u) == 0);
        reverb->render(noisy.data() + t * 64, wl.data(), wr.data(), 64);
        CHECK_FALSE(reverb->appliedSettings().freeze);
        for (unsigned k = 0; k < 64; ++k)
        {
            peak = std::max(peak, std::max(std::fabs(wl[k]), std::fabs(wr[k])));
            REQUIRE(std::isfinite(wl[k]));
            REQUIRE(std::isfinite(wr[k]));
        }
    }
    CHECK(peak < 12.0f);
}

TEST_CASE("Extreme controls and hostile input stay finite and bounded", "[reverb][master_reverb]")
{
    struct Combo
    {
        float decay, damping, lowCut, depth, rate, width, diffusion;
        bool freeze;
    };
    const std::vector<Combo> combos = {
        {0.1f, 100.0f, 10.0f, 0.0f, 0.01f, 0.0f, 0.0f, false},
        {60.0f, 10800.0f, 1000.0f, 1.0f, 5.0f, 2.0f, 1.0f, false},
        {1000.0f, 3000.0f, 40.0f, 1.0f, 5.0f, 1.0f, 0.8f, false},
        {1000.0f, 100.0f, 10.0f, 0.5f, 0.5f, 2.0f, 0.0f, true},
        {20.0f, 3000.0f, 40.0f, 0.5f, 0.5f, 1.0f, 0.8f, true},
    };
    for (const Combo &c : combos)
    {
        CAPTURE(c.decay, c.damping, c.lowCut, c.depth, c.rate, c.width, c.freeze);
        auto reverb = std::make_unique<MasterReverb>();
        ReverbSettings s;
        s.mix = 1.0f;
        s.decaySeconds = c.decay;
        s.dampingHz = c.damping;
        s.lowCutHz = c.lowCut;
        s.modDepth = c.depth;
        s.modRateHz = c.rate;
        s.width = c.width;
        s.diffusion = c.diffusion;
        s.freeze = c.freeze;
        reverb->publishSettings(s);
        reverb->prepare(kRate);

        // DC, sustained bass, full-scale noise, impulses and out-of-range bursts
        // (+/-50, far past full scale), back to back. NaN is deliberately absent:
        // nothing on the master bus (delay, compressor, tank input filters) survives
        // a non-finite sample, so it is not this stage's contract.
        rpdsp::XorShift32 noise(7);
        std::vector<float> dry(3 * 48000, 0.0f);
        for (std::size_t n = 0; n < dry.size(); ++n)
        {
            if (n < 12000) dry[n] = 0.9f;
            else if (n < 60000) dry[n] = 0.9f * std::sin(6.2831853f * 40.0f * static_cast<float>(n) / kRate);
            else if (n < 84000) dry[n] = noise.nextBipolar();
            else if (n % 4800 == 0) dry[n] = 1.0f;
        }
        for (std::size_t n = 100000; n < 100100; ++n) dry[n] = (n & 1u) ? 50.0f : -50.0f;
        std::vector<float> wl(64), wr(64);
        float peak = 0.0f;
        std::size_t nonFinite = 0;
        for (std::size_t at = 0; at < dry.size(); at += 64)
        {
            reverb->render(dry.data() + at, wl.data(), wr.data(), 64);
            for (unsigned k = 0; k < 64; ++k)
            {
                if (!std::isfinite(wl[k]) || !std::isfinite(wr[k])) ++nonFinite;
                // Blend ignores the burst's own +/-50 (that is dry signal, not tank).
                if (std::fabs(dry[at + k]) < 2.0f)
                    peak = std::max(peak, std::max(std::fabs(wl[k]), std::fabs(wr[k])));
            }
        }
        CHECK(nonFinite == 0);
        // The clamp bounds the ring; it is not a limiter, so leave plenty of headroom
        // for the downstream compressor and PCM clamp (which the bus tests cover).
        CHECK(peak < 64.0f);
    }
}

TEST_CASE("Half and Float storage agree at the same capacity", "[reverb][master_reverb][storage]")
{
    using Half = rpdsp::DarkReverb<MasterReverb::kCapacity, rpdsp::DarkReverbStorage::Half>;
    using Float = rpdsp::DarkReverb<MasterReverb::kCapacity, rpdsp::DarkReverbStorage::Float>;
    static_assert(Half::kBufferBytes * 2 == Float::kBufferBytes, "Float costs exactly twice the delay buffer");
    static_assert(Float::kBufferBytes == 65536 && Half::kBufferBytes == 32768, "16384-sample tank");

    struct Case
    {
        const char *name;
        float decay;
    };
    for (const Case &c : {Case{"impulse, short", 0.1f}, Case{"impulse, medium", 6.0f}, Case{"impulse, very long", 1000.0f}})
    {
        CAPTURE(c.name, c.decay);
        const std::size_t frames = 6 * 48000;
        std::vector<float> in(frames, 0.0f), h(frames), f(frames), unused(frames);
        in[0] = 1.0f;
        auto half = std::make_unique<Half>();
        auto flt = std::make_unique<Float>();
        half->prepare(kRate);
        half->setMix(1.0f);
        half->setDecaySeconds(c.decay);
        flt->prepare(kRate);
        flt->setMix(1.0f);
        flt->setDecaySeconds(c.decay);
        half->process(in.data(), in.data(), h.data(), unused.data(), frames);
        flt->process(in.data(), in.data(), f.data(), unused.data(), frames);
        double error = 0.0, reference = 0.0, peakHalf = 0.0, peakFloat = 0.0;
        for (std::size_t n = 0; n < frames; ++n)
        {
            const double d = static_cast<double>(h[n]) - f[n];
            error += d * d;
            reference += static_cast<double>(f[n]) * f[n];
            peakHalf = std::max(peakHalf, static_cast<double>(std::fabs(h[n])));
            peakFloat = std::max(peakFloat, static_cast<double>(std::fabs(f[n])));
        }
        // Measured on GCC 13.3 x86-64: residual -62..-66 dB below the Float output
        // over 6 s, equal peaks within 0.01 dB. Half's rounding is relative, so
        // decay and level agree; only the noise floor differs.
        CHECK(10.0 * std::log10(error / reference) < -55.0);
        CHECK(std::fabs(20.0 * std::log10(peakHalf / peakFloat)) < 0.1);
        if (c.decay >= 6.0f)
        {
            CHECK(std::fabs(10.0 * std::log10(rmsOf(h, 48000, 52800) / rmsOf(f, 48000, 52800))) < 0.2);
        }
    }
}

TEST_CASE("The compiled reverb variant names itself for the serial diagnostics", "[reverb][master_reverb][storage]")
{
    static_assert(!MasterReverb::kBypass, "the main suites test the real reverb; the bypass build has its own target");
    using HalfEngine = rpdsp::DarkReverb<MasterReverb::kCapacity, rpdsp::DarkReverbStorage::Half>;
    constexpr bool isHalf = std::is_same_v<MasterReverb::Engine, HalfEngine>;
    CHECK(MasterReverb::kHalfStorage == isHalf);
    CHECK(std::string(MasterReverb::kVariantName) == (isHalf ? "half16384" : "float16384"));
    // The tank is the bulk of the object; the rest is control state and the two
    // queues (RAM budget: docs/audio-performance.md, "Reverb RAM audit").
    CHECK(sizeof(MasterReverb) >= sizeof(MasterReverb::Engine));
    CHECK(sizeof(MasterReverb) < sizeof(MasterReverb::Engine) + 1024);
}

TEST_CASE("Rendering, control changes and snapshots never allocate", "[reverb][master_reverb][alloc]")
{
    // Everything that can allocate is built outside the window.
    auto reverb = std::make_unique<MasterReverb>();
    std::vector<float> dry = burst(48000, 24000, 0.3f), wl(64), wr(64);
    auto manager = std::make_unique<VoiceManager>(1);
    VoiceConfig config;
    config.oscillatorCount = 1;
    config.oscWaveforms[0] = WAVE_SIN;
    config.hasEnvelope = false;
    config.hasFilter = false;
    const uint8_t id = manager->addVoice(config);
    manager->init(kRate);
    VoiceState note;
    note.noteIndex = 24.0f;
    note.velocityLevel = 1.0f;
    note.isGateHigh = true;
    note.shouldRetrigger = true;
    manager->updateVoiceState(id, note);
    std::vector<float> left(1024), right(1024);
    const ReverbSettings snap = otherSettings();

    std::uint64_t allocations = 0;
    {
        AllocationWindow window;
        for (unsigned call = 0; call < 200; ++call)
        {
            if (call % 7 == 0) reverb->setMix(static_cast<float>(call % 5) / 4.0f);
            if (call % 11 == 0) reverb->setFreeze((call / 11) % 2 == 0);
            if (call % 13 == 0) reverb->publishSettings(snap);
            if (call % 3 == 0) reverb->setDecaySeconds(1.0f + static_cast<float>(call % 17));
            reverb->render(dry.data() + (call % 700) * 64, wl.data(), wr.data(), 64);
            (void)reverb->settings();
        }
        for (unsigned call = 0; call < 200; ++call)
        {
            if (call % 9 == 0) manager->setReverbMix(static_cast<float>(call % 4) / 3.0f);
            if (call % 17 == 0) manager->applyReverbSettings(snap);
            if (call % 5 == 0) manager->setReverbFreeze(call % 2 == 0);
            manager->processStereoBlock(left.data(), right.data(), 1 + (call * 37) % 300);
            manager->processBlock(left.data(), 256);
            manager->processStereoBlock(left.data(), right.data(), 700); // longer than kMaxBlock: chunked
        }
        allocations = window.count();
    }
    CHECK(allocations == 0);
}

TEST_CASE("prepare() clears the tank and keeps the published targets", "[reverb][master_reverb]")
{
    auto reverb = std::make_unique<MasterReverb>();
    reverb->setMix(1.0f);
    reverb->setDecaySeconds(3.0f); // before prepare: must survive it
    reverb->prepare(kRate);
    CHECK(reverb->appliedSettings().decaySeconds == 3.0f);
    CHECK(reverb->currentMix() == 1.0f);

    render(*reverb, burst(24000, 24000, 0.5f), {64});
    const Wet ringing = render(*reverb, std::vector<float>(4800, 0.0f), {64});
    REQUIRE(rmsOf(ringing.left, 0, 4800) > 1.0e-3);
    reverb->prepare(kRate);
    const Wet cleared = render(*reverb, std::vector<float>(24000, 0.0f), {64});
    for (std::size_t n = 0; n < cleared.left.size(); ++n)
    {
        REQUIRE(cleared.left[n] == 0.0f);
        REQUIRE(cleared.right[n] == 0.0f);
    }

    // Other sample rates and bad ones stay finite.
    for (const float rate : {8000.0f, 44100.0f, 96000.0f, 0.0f, -1.0f})
    {
        reverb->prepare(rate);
        const Wet w = render(*reverb, burst(9600, 4800, 0.4f), {64, 33});
        for (std::size_t n = 0; n < w.left.size(); ++n)
            REQUIRE((std::isfinite(w.left[n]) && std::isfinite(w.right[n])));
    }
}
