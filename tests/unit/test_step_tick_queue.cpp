#include <catch2/catch_test_macros.hpp>

#include <limits>

#define private public
#include "sequencer/StepTickQueue.h"
#undef private

TEST_CASE("StepTickQueue starts empty", "[steptick]")
{
    StepTickQueue q;
    REQUIRE(q.empty());
    REQUIRE(q.size() == 0);

    uint32_t step = 0;
    REQUIRE_FALSE(q.pop(step));
}

TEST_CASE("StepTickQueue preserves FIFO order", "[steptick]")
{
    StepTickQueue q;
    REQUIRE(q.push(10));
    REQUIRE(q.push(11));
    REQUIRE(q.push(12));

    uint32_t step = 0;
    REQUIRE(q.pop(step));
    REQUIRE(step == 10);
    REQUIRE(q.pop(step));
    REQUIRE(step == 11);
    REQUIRE(q.pop(step));
    REQUIRE(step == 12);
    REQUIRE(q.empty());
}

TEST_CASE("StepTickQueue wraps cleanly past capacity", "[steptick]")
{
    StepTickQueue q;

    // Push/pop past capacity repeatedly so the free-running indices wrap the
    // physical slot array many times over.
    for (uint32_t round = 0; round < 100; ++round)
    {
        for (uint32_t i = 0; i < StepTickQueue::kCapacity; ++i)
        {
            REQUIRE(q.push(round * StepTickQueue::kCapacity + i));
        }
        REQUIRE_FALSE(q.empty());
        REQUIRE(q.size() == StepTickQueue::kCapacity);

        for (uint32_t i = 0; i < StepTickQueue::kCapacity; ++i)
        {
            uint32_t step = 0;
            REQUIRE(q.pop(step));
            REQUIRE(step == round * StepTickQueue::kCapacity + i);
        }
        REQUIRE(q.empty());
    }
}

TEST_CASE("StepTickQueue handles uint32_t index wraparound", "[steptick]")
{
    StepTickQueue q;
    q.head_ = std::numeric_limits<uint32_t>::max() - 1;
    q.tail_ = q.head_;

    REQUIRE(q.empty());
    REQUIRE(q.push(123));
    REQUIRE(q.push(456));
    REQUIRE(q.size() == 2);

    uint32_t step = 0;
    REQUIRE(q.pop(step));
    REQUIRE(step == 123);
    REQUIRE(q.size() == 1);
    REQUIRE(q.pop(step));
    REQUIRE(step == 456);
    REQUIRE(q.empty());
    REQUIRE(q.size() == 0);
}

TEST_CASE("StepTickQueue drops new ticks when full", "[steptick]")
{
    StepTickQueue q;
    for (uint32_t i = 0; i < StepTickQueue::kCapacity; ++i)
    {
        REQUIRE(q.push(i));
    }
    REQUIRE(q.size() == StepTickQueue::kCapacity);
    REQUIRE_FALSE(q.push(999)); // full: newest tick dropped

    uint32_t step = 0;
    for (uint32_t i = 0; i < StepTickQueue::kCapacity; ++i)
    {
        REQUIRE(q.pop(step));
        REQUIRE(step == i);
    }
    REQUIRE(q.empty());

    // Queue works normally again after draining.
    REQUIRE(q.push(1000));
    REQUIRE(q.pop(step));
    REQUIRE(step == 1000);
}

TEST_CASE("StepTickQueue supports interleaved push/pop below capacity", "[steptick]")
{
    StepTickQueue q;

    for (uint32_t i = 0; i < StepTickQueue::kCapacity - 1; ++i)
    {
        REQUIRE(q.push(i));
    }
    uint32_t step = 0;
    REQUIRE(q.pop(step));
    REQUIRE(step == 0);
    REQUIRE(q.push(100));
    REQUIRE(q.size() == StepTickQueue::kCapacity - 1);

    // The freed slot must be reusable and order intact across the wrap.
    for (uint32_t i = 1; i < StepTickQueue::kCapacity - 1; ++i)
    {
        REQUIRE(q.pop(step));
        REQUIRE(step == i);
    }
    REQUIRE(q.pop(step));
    REQUIRE(step == 100);
    REQUIRE(q.empty());
}
