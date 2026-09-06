#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// One producer and one consumer, each called from one thread only.
// A published slot belongs to the consumer until its copy has finished.
template <typename T, size_t Capacity>
class SpscQueue
{
    static_assert(Capacity > 0 && Capacity < UINT32_MAX, "Invalid queue capacity");
    static_assert(std::is_trivially_copyable<T>::value, "Queue payload must be plain data");
    static_assert(std::atomic<uint32_t>::is_always_lock_free,
                  "Audio queues require lock-free 32-bit atomics");

public:
    // Producer only. Full queues leave both the input and occupied slots intact.
    bool tryPush(const T &value) noexcept
    {
        const uint32_t write = writeIndex_.load(std::memory_order_relaxed);
        const uint32_t next = advance(write);
        if (next == readIndex_.load(std::memory_order_acquire))
            return false;
        slots_[write] = value;
        writeIndex_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer only. Return the slot only AFTER copying its complete payload.
    bool tryPop(T &value) noexcept
    {
        const uint32_t read = readIndex_.load(std::memory_order_relaxed);
        if (read == writeIndex_.load(std::memory_order_acquire))
            return false;
        value = slots_[read];
        readIndex_.store(advance(read), std::memory_order_release);
        return true;
    }

private:
    static uint32_t advance(uint32_t index) noexcept
    {
        return index == Capacity ? 0u : index + 1u;
    }

    // One sentinel slot distinguishes full from empty; Capacity entries are usable.
    std::array<T, Capacity + 1> slots_{};
    std::atomic<uint32_t> writeIndex_{0}; // producer publishes completed writes
    std::atomic<uint32_t> readIndex_{0};  // consumer publishes completed reads
};
