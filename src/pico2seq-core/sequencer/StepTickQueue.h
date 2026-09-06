#pragma once

#include <cstdint>

// Fixed-capacity single-producer/single-consumer ring for handing uClock step
// numbers from the clock ISR to the Core 1 main loop (the same handoff
// pattern as the ppqnTicksPending counter, but carrying a payload). The ISR
// is the only producer (push); loop1() is the only consumer (pop). Both run
// on core 1, so plain volatile index reads/writes are the whole
// synchronization story — no critical sections needed.
//
// Indices are free-running uint32 with a power-of-two mask, so producer and
// consumer never write the same word and wrap-around is exact for 2^32
// entries. When the ring is full, push() drops the *new* tick and returns
// false: a full ring means loop1() has been stalled for kCapacity steps
// (~1.3 s at 90 BPM), where catching up by bursting stale steps would be
// worse than dropping them.
class StepTickQueue
{
public:
    static constexpr uint32_t kCapacity = 8; // power of two

    // ISR side. Returns false (tick dropped) when the ring is full.
    bool push(uint32_t step)
    {
        const uint32_t head = head_;
        if (head - tail_ >= kCapacity)
        {
            return false;
        }
        slots_[head & kIndexMask] = step;
        head_ = head + 1; // published last: pop never sees an unwritten slot
        return true;
    }

    // Consumer side (loop1). Returns false when the ring is empty.
    bool pop(uint32_t &step)
    {
        const uint32_t tail = tail_;
        if (tail == head_)
        {
            return false;
        }
        step = slots_[tail & kIndexMask];
        tail_ = tail + 1;
        return true;
    }

    bool empty() const { return tail_ == head_; }

    uint32_t size() const { return head_ - tail_; }

private:
    static constexpr uint32_t kIndexMask = kCapacity - 1;

    uint32_t slots_[kCapacity] = {0};
    volatile uint32_t head_ = 0;
    volatile uint32_t tail_ = 0;
};
