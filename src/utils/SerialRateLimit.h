#pragma once

// SerialRateLimit.h — keeps diagnostics from flooding the USB serial port.
//
// Serial output on Core 0 is cheap to write and expensive to ignore: a print that
// runs every loop() pass floods the monitor (and can starve the USB stack) while
// the audio is perfectly healthy. Portable logic only, no Arduino includes: callers
// pass millis() in, so every rule below is unit-tested on the host.
//
//   StallWatch  one check per interval, never per poll (the Core 1 stall line).
//   LogBudget   token bucket behind Debug::vlogf: a burst is allowed, the sustained
//               rate is capped, and dropped lines are counted for one summary.
//
// All time math is unsigned subtraction, so millis() wraparound is harmless.

#include <cstdint>

namespace SerialRateLimit
{
// Detects a frozen counter (completed audio buffers) without reacting to how often
// it is polled. poll() may be called every loop() pass; it evaluates a window only
// once `intervalMs` has elapsed since the previous window closed, so "unchanged"
// always means unchanged for the whole interval and at most one report is possible
// per interval.
class StallWatch
{
public:
    explicit constexpr StallWatch(uint32_t intervalMs) noexcept : intervalMs_(intervalMs) {}

    // `live`: the owner claims the counter should be advancing right now (the render
    // loop is running, not booting, parked or failed). True only when a window just
    // closed with the counter unmoved and `live` at both ends of the window.
    bool poll(uint32_t nowMs, bool live, uint32_t count) noexcept
    {
        if (!started_)
        {
            started_ = true;
            windowStartMs_ = nowMs;
            last_ = count;
            liveAtStart_ = live;
            return false;
        }
        if (nowMs - windowStartMs_ < intervalMs_)
            return false;
        const bool stalled = live && liveAtStart_ && count == last_;
        windowStartMs_ = nowMs;
        last_ = count;
        liveAtStart_ = live;
        return stalled;
    }

private:
    uint32_t intervalMs_;
    uint32_t windowStartMs_ = 0;
    uint32_t last_ = 0;
    bool liveAtStart_ = false;
    bool started_ = false;
};

// Token bucket: `burst` lines may go out back to back, then one more per `refillMs`.
// Refusals are counted so the caller can print a single "N lines dropped" summary
// instead of silently losing them.
class LogBudget
{
public:
    constexpr LogBudget(uint8_t burst, uint32_t refillMs) noexcept
        : burst_(burst), refillMs_(refillMs), tokens_(burst)
    {
    }

    bool allow(uint32_t nowMs) noexcept
    {
        refill(nowMs);
        if (tokens_ == 0)
        {
            ++suppressed_;
            return false;
        }
        --tokens_;
        return true;
    }

    // Lines refused since the last call; reading resets the count.
    uint32_t takeSuppressed() noexcept
    {
        const uint32_t n = suppressed_;
        suppressed_ = 0;
        return n;
    }

private:
    void refill(uint32_t nowMs) noexcept
    {
        const uint32_t steps = (nowMs - lastRefillMs_) / refillMs_;
        if (steps == 0)
            return;
        const uint32_t room = static_cast<uint32_t>(burst_ - tokens_);
        if (steps >= room)
        {
            tokens_ = burst_;
            lastRefillMs_ = nowMs; // full bucket: a long silence earns no extra credit
        }
        else
        {
            tokens_ = static_cast<uint8_t>(tokens_ + steps);
            lastRefillMs_ += steps * refillMs_;
        }
    }

    uint8_t burst_;
    uint32_t refillMs_;
    uint8_t tokens_;
    uint32_t lastRefillMs_ = 0;
    uint32_t suppressed_ = 0;
};
} // namespace SerialRateLimit
