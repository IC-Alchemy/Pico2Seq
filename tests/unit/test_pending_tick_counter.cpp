#include <catch2/catch_test_macros.hpp>
#include "utils/PendingTickCounter.h"
#include <atomic>
#include <thread>

TEST_CASE("PPQN empty and single-tick batches are consumed exactly once", "[ppqn]")
{
    PendingTickCounter ticks;
    REQUIRE(ticks.takeAll() == 0);
    ticks.post();
    REQUIRE(ticks.takeAll() == 1);
    REQUIRE(ticks.takeAll() == 0);
    ticks.post();
    REQUIRE(ticks.takeAll() == 1);
    REQUIRE(ticks.takeAll() == 0);
}

TEST_CASE("PPQN retains a backlog larger than a 16-bit counter", "[ppqn]")
{
    PendingTickCounter ticks;
    constexpr uint32_t backlog = 70000;
    for (uint32_t i = 0; i < backlog; ++i)
        ticks.post();

    REQUIRE(ticks.takeAll() == backlog);
    REQUIRE(ticks.takeAll() == 0);
}

TEST_CASE("PPQN arrivals during a drain remain in the next batch", "[ppqn]")
{
    PendingTickCounter ticks;
    for (unsigned i = 0; i < 3; ++i)
        ticks.post();

    uint32_t remaining = ticks.takeAll();
    REQUIRE(remaining == 3);
    uint32_t processed = 0;
    while (remaining > 0)
    {
        // Simulate an ISR preempting each iteration, including the last tick.
        ticks.post();
        --remaining;
        ++processed;
    }

    REQUIRE(processed == 3);
    REQUIRE(ticks.takeAll() == 3);
    REQUIRE(ticks.takeAll() == 0);
}

TEST_CASE("PPQN concurrent posting and draining neither loses nor duplicates ticks",
          "[ppqn][concurrency]")
{
    PendingTickCounter ticks;
    constexpr uint32_t totalTicks = 1000000;
    std::atomic<bool> start{false};
    std::atomic<bool> done{false};
    std::thread producer([&] {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        for (uint32_t i = 0; i < totalTicks; ++i)
        {
            ticks.post();
            if ((i % 256) == 0)
                std::this_thread::yield();
        }
        done.store(true, std::memory_order_release);
    });

    uint64_t processed = 0;
    start.store(true, std::memory_order_release);
    while (!done.load(std::memory_order_acquire))
        processed += ticks.takeAll();
    producer.join();
    processed += ticks.takeAll();

    REQUIRE(processed == totalTicks);
    REQUIRE(ticks.takeAll() == 0);
}
