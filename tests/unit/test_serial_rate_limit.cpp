#include <catch2/catch_test_macros.hpp>
#include "utils/SerialRateLimit.h"

#include <cstdint>

// Guards against serial floods. The STALLED line once printed hundreds of times a
// second because the check ran every loop() pass while audio buffers complete only
// every ~5.3 ms, so "count unchanged since the last pass" was almost always true.

using namespace SerialRateLimit;

namespace
{
constexpr uint32_t kInterval = 2000;
} // namespace

TEST_CASE("StallWatch stays quiet while buffers keep completing, however fast it is polled", "[serial_rate]")
{
    StallWatch watch(kInterval);
    uint32_t reports = 0;
    uint32_t bufs = 45000;
    // loop() polled every 1 ms; a buffer completes every 5 ms; healthy for 10 s.
    for (uint32_t ms = 0; ms < 10000; ++ms)
    {
        if (ms % 5 == 0)
            ++bufs;
        if (watch.poll(ms, true, bufs))
            ++reports;
    }
    REQUIRE(reports == 0);
}

TEST_CASE("StallWatch reports a frozen count once per interval, not once per poll", "[serial_rate]")
{
    StallWatch watch(kInterval);
    uint32_t reports = 0;
    for (uint32_t ms = 0; ms < 10000; ++ms)
        if (watch.poll(ms, true, 777))
            ++reports;
    // One report per closed window: 2000, 4000, 6000, 8000 (not ~10000 polls).
    REQUIRE(reports == 4);
}

TEST_CASE("StallWatch ignores phases that do not claim the render loop is live", "[serial_rate]")
{
    StallWatch watch(kInterval);
    for (uint32_t ms = 0; ms < 10000; ++ms)
        REQUIRE_FALSE(watch.poll(ms, false, 5));
}

TEST_CASE("StallWatch needs a live baseline before it can accuse", "[serial_rate]")
{
    StallWatch watch(kInterval);
    // Not live for the first window, live (and unchanged) only in the second.
    for (uint32_t ms = 0; ms < 2000; ++ms)
        REQUIRE_FALSE(watch.poll(ms, false, 9));
    REQUIRE_FALSE(watch.poll(2000, true, 9)); // closes window 1: baseline was not live
    bool sawStall = false;
    for (uint32_t ms = 2001; ms <= 4000; ++ms)
        sawStall = watch.poll(ms, true, 9) || sawStall;
    REQUIRE(sawStall); // window 2 is live on both ends and unchanged
}

TEST_CASE("StallWatch survives millis() wraparound", "[serial_rate]")
{
    StallWatch watch(kInterval);
    const uint32_t start = 0xFFFFFF00u;
    uint32_t reports = 0;
    for (uint32_t i = 0; i < 6000; ++i)
        if (watch.poll(start + i, true, 1)) // wraps through 0 partway
            ++reports;
    REQUIRE(reports == 2);
}

TEST_CASE("LogBudget allows a burst then caps the sustained rate", "[serial_rate]")
{
    LogBudget budget(/*burst=*/10, /*refillMs=*/50);
    uint32_t allowed = 0;
    // A call site hammering every millisecond for one second.
    for (uint32_t ms = 0; ms < 1000; ++ms)
        if (budget.allow(ms))
            ++allowed;
    // Burst of 10 plus one refill per 50 ms: about 10 + 20 lines in 1 s, never 1000.
    REQUIRE(allowed >= 20);
    REQUIRE(allowed <= 31);
}

TEST_CASE("LogBudget counts what it dropped and reports it once", "[serial_rate]")
{
    LogBudget budget(3, 100);
    for (int i = 0; i < 10; ++i)
        budget.allow(0);
    REQUIRE(budget.takeSuppressed() == 7);
    REQUIRE(budget.takeSuppressed() == 0);
}

TEST_CASE("LogBudget refills after a quiet period but never beyond the burst", "[serial_rate]")
{
    LogBudget budget(4, 50);
    for (int i = 0; i < 4; ++i)
        REQUIRE(budget.allow(0));
    REQUIRE_FALSE(budget.allow(0));
    uint32_t allowed = 0;
    for (int i = 0; i < 20; ++i) // long silence, then a same-instant burst
        if (budget.allow(60000))
            ++allowed;
    REQUIRE(allowed == 4);
}

TEST_CASE("LogBudget survives millis() wraparound", "[serial_rate]")
{
    LogBudget budget(2, 100);
    REQUIRE(budget.allow(0xFFFFFFF0u));
    REQUIRE(budget.allow(0xFFFFFFF0u));
    REQUIRE_FALSE(budget.allow(0xFFFFFFF0u));
    REQUIRE(budget.allow(0xFFFFFFF0u + 100)); // wrapped to 84, one refill elapsed
}
