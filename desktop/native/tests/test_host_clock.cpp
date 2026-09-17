// Golden-timing tests for the HostClock (desktop uClock port).
//
// Expected values derive from the real uClock 2.2.1 semantics at 480 PPQN:
//   tick interval = 60,000,000 us / (480 * bpm)
//   16th-note step = 120 ticks; shuffle template offsets delay (positive)
//   or advance (negative) individual 16ths by whole ticks.

#include <catch2/catch_test_macros.hpp>

#include "../src/HostClock.h"

#include <uClock.h>

#include <cstdint>
#include <vector>

namespace
{
struct ClockTrace
{
    std::vector<uint32_t> ppqnTicks; // callback argument per tick
    std::vector<uint64_t> stepUs;    // pumped micros at each onStep callback
};

ClockTrace *g_trace = nullptr;
uint64_t g_pumpUs = 0;

void onPpqnTick(uint32_t tick)
{
    if (g_trace)
        g_trace->ppqnTicks.push_back(tick);
}

void onStep(uint32_t)
{
    if (g_trace)
        g_trace->stepUs.push_back(g_pumpUs);
}

ClockTrace &arm()
{
    static ClockTrace trace; // tests run sequentially; reset in arm()
    p2s::host::resetHostClockForTest();
    trace = ClockTrace{};
    g_trace = &trace;
    g_pumpUs = 0;
    uClock.setOutputPPQN(uClock.PPQN_480);
    uClock.setOnOutputPPQN(&onPpqnTick);
    uClock.setOnStep(&onStep);
    return trace;
}

// Advance the clock to `us`, recording the pump time for step callbacks.
void pumpTo(uint64_t us)
{
    g_pumpUs = us;
    p2s::host::advanceClockNs(us * 1000ULL);
}

void pumpRange(uint64_t fromUs, uint64_t toUs)
{
    for (uint64_t us = fromUs; us <= toUs; us += 250)
        pumpTo(us);
}
} // namespace

TEST_CASE("HostClock straight 90 BPM fires 480 PPQN and 16th steps", "[desktop][clock]")
{
    ClockTrace &trace = arm();
    uClock.setTempo(90.0f);
    uClock.setShuffle(false);
    uClock.start();

    pumpRange(250, 2'000'250); // exactly 2 s after the arm point (first pump)

    // 2 s * 90 BPM * 480 PPQN / 60 = 1440 output ticks, numbered 0..1439.
    // (The clock arms on the first pump: the first tick fires one interval
    // after arm, matching uClock's already-running hardware timer.)
    REQUIRE(trace.ppqnTicks.size() == 1440);
    REQUIRE(trace.ppqnTicks.front() == 0);
    REQUIRE(trace.ppqnTicks.back() == 1439);

    // 2 s * 90 BPM * 4 steps / 60 = 12 sixteenth steps.
    REQUIRE(trace.stepUs.size() == 12);

    // Straight feel: every step exactly 120 ticks (166,666.67 us) apart.
    // Step times quantize to the 250 us pump grid, so gaps carry up to
    // 2 * 250 us of quantization.
    const uint64_t nominal = 1'000'000ULL * 60 / (90 * 4);
    for (size_t i = 1; i < trace.stepUs.size(); ++i)
    {
        const uint64_t gap = trace.stepUs[i] - trace.stepUs[i - 1];
        REQUIRE(gap >= nominal - 600);
        REQUIRE(gap <= nominal + 600);
    }
}

TEST_CASE("HostClock Phatty Swang delays odd 16ths by 40 ticks", "[desktop][clock]")
{
    ClockTrace &trace = arm();
    uClock.setTempo(90.0f);
    int8_t phatty[16] = {0, 40, 0, 40, 0, 40, 0, 40, 0, 40, 0, 40, 0, 40, 0, 40};
    uClock.setShuffleTemplate(phatty, 16);
    uClock.setShuffle(true);
    uClock.start();

    pumpRange(250, 1'000'000);

    REQUIRE(trace.stepUs.size() == 6);
    const double tick = 1'000'000.0 * 60 / (90.0 * 480); // 1388.89 us
    const uint64_t longGap = static_cast<uint64_t>(160 * tick); // step 0 -> 1 (120 + 40)
    const uint64_t shortGap = static_cast<uint64_t>(80 * tick); // step 1 -> 2 (120 - 40)
    const uint64_t gotLong = trace.stepUs[1] - trace.stepUs[0];
    const uint64_t gotShort = trace.stepUs[2] - trace.stepUs[1];
    // 250 us pump quantization on both endpoints of each gap.
    CHECK(gotLong >= longGap - 600);
    CHECK(gotLong <= longGap + 600);
    CHECK(gotShort >= shortGap - 600);
    CHECK(gotShort <= shortGap + 600);
}

TEST_CASE("HostClock stop halts callbacks and start resets step numbers", "[desktop][clock]")
{
    ClockTrace &trace = arm();
    uClock.setTempo(120.0f);
    uClock.setShuffle(false);
    uClock.start();

    pumpRange(250, 500'000);
    const size_t stepsBefore = trace.stepUs.size();
    REQUIRE(stepsBefore == 4); // 0.5 s @ 120 BPM = 4 steps

    uClock.stop();
    const size_t ticksAtStop = trace.ppqnTicks.size();
    pumpRange(500'250, 1'000'000);
    REQUIRE(trace.ppqnTicks.size() == ticksAtStop); // nothing fires while paused
    REQUIRE(trace.stepUs.size() == stepsBefore);

    uClock.start();
    pumpRange(1'000'250, 1'500'000);
    // The fresh run fires 4 more steps (step numbers restarted from 0).
    REQUIRE(trace.stepUs.size() == stepsBefore + 4);
}

TEST_CASE("HostClock tempo change takes effect for future ticks", "[desktop][clock]")
{
    ClockTrace &trace = arm();
    uClock.setTempo(90.0f);
    uClock.setShuffle(false);
    uClock.start();

    pumpRange(250, 1'000'000);
    REQUIRE(trace.stepUs.size() == 6); // 1 s @ 90 BPM

    uClock.setTempo(180.0f); // doubles the rate from the next tick gap on
    pumpRange(1'000'250, 2'000'000);
    REQUIRE(trace.stepUs.size() == 6 + 12); // 1 s @ 180 BPM
}
