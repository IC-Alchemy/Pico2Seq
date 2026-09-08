#pragma once

#include <atomic>
#include <cstdint>

// Count payload-free ISR events and transfer a batch to the control loop.
// As with any uint32_t counter, fewer than 2^32 ticks may accumulate per batch.
class PendingTickCounter
{
    static_assert(std::atomic<uint32_t>::is_always_lock_free,
                  "ISR tick counting requires lock-free 32-bit atomics");

public:
    // ISR only. No other state is published, so relaxed ordering is sufficient.
    void post() noexcept
    {
        pending_.fetch_add(1, std::memory_order_relaxed);
    }

    // Control loop only. A concurrent post belongs to this batch or the next;
    // there is no separate read/clear window that could erase it.
    [[nodiscard]] uint32_t takeAll() noexcept
    {
        return pending_.exchange(0, std::memory_order_relaxed);
    }

private:
    std::atomic<uint32_t> pending_{0};
};
