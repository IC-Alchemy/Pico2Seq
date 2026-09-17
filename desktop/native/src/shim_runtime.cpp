// Definitions backing the desktop shims: host clock, Serial sink, the
// global ISR-equivalent critical section, and the fake MPR121 touch state.

// Windows SDK before <Arduino.h>: winuser.h's INPUT typedef must not meet
// the Arduino INPUT macro.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "Arduino.h"
#include "hardware/sync.h"
#include "Adafruit_MPR121.h"
#include "Adafruit_VL53L1X.h"
#include "FastLED.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace
{
std::chrono::steady_clock::time_point g_epoch = std::chrono::steady_clock::now();

// One recursive mutex plays the role of "interrupts disabled": the control
// thread takes it inside save_and_disable_interrupts()/noInterrupts(), and
// the HostClock tick runner holds it while firing clock callbacks, which is
// what the uClock timer ISR's interrupt context guaranteed on the firmware.
std::recursive_mutex g_interruptMutex;

std::atomic<uint16_t> g_mpr121TouchBits{0};

bool serialLogEnabled()
{
    static const bool enabled = std::getenv("P2S_SERIAL_LOG") != nullptr;
    return enabled;
}
} // namespace

namespace p2s
{
namespace host
{
namespace
{
constexpr size_t kMaxInterrupts = 8;
InterruptFn g_interrupts[kMaxInterrupts] = {};
uint8_t g_interruptPins[kMaxInterrupts] = {};

void fireAllRegisteredInterrupts()
{
    for (InterruptFn fn : g_interrupts)
        if (fn)
            fn();
}
} // namespace

void registerInterrupt(uint8_t pin, InterruptFn fn)
{
    for (size_t i = 0; i < kMaxInterrupts; ++i)
    {
        if (g_interruptPins[i] == pin || g_interrupts[i] == nullptr)
        {
            g_interruptPins[i] = pin;
            g_interrupts[i] = fn;
            return;
        }
    }
}

uint32_t clockMillis()
{
    const auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - g_epoch).count());
}

uint32_t clockMicros()
{
    const auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now - g_epoch).count());
}

void serialWrite(const char *text)
{
    // Always mirror to the debugger: winapp run --debug-output captures this,
    // which is how firmware Serial lines reach the desktop developer.
#if defined(_WIN32)
    OutputDebugStringA(text);
#endif
    if (serialLogEnabled())
        std::fputs(text, stdout);
}

bool serialEnabled() { return serialLogEnabled(); }

void setMpr121TouchBits(uint16_t bits)
{
    static std::atomic<uint16_t> s_lastBits{0};
    const uint16_t previous = s_lastBits.exchange(bits, std::memory_order_relaxed);
    if (previous != bits)
        fireAllRegisteredInterrupts(); // stand-in for the MPR121 IRQ edge
    g_mpr121TouchBits.store(bits, std::memory_order_relaxed);
}
uint16_t mpr121TouchBits() { return g_mpr121TouchBits.load(std::memory_order_relaxed); }

void setLidarSampleMm(int32_t distanceMm)
{
    Adafruit_VL53L1X::distanceMm = distanceMm < 0 ? -1 : distanceMm;
    Adafruit_VL53L1X::rangeStatus = distanceMm < 0 ? 2 /* signal fail: rejected */ : 0;
    Adafruit_VL53L1X::pending.store(true, std::memory_order_relaxed);
}
} // namespace host
} // namespace p2s

// Fake VL53L1X ranging data (statics owned by the shim class).
std::atomic<bool> Adafruit_VL53L1X::pending{false};
int32_t Adafruit_VL53L1X::distanceMm = -1;
uint8_t Adafruit_VL53L1X::rangeStatus = 255;

void delay(unsigned long ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void noInterrupts() { g_interruptMutex.lock(); }
void interrupts() { g_interruptMutex.unlock(); }

uint32_t save_and_disable_interrupts()
{
    g_interruptMutex.lock();
    return 0;
}

void restore_interrupts(uint32_t) { g_interruptMutex.unlock(); }

void HostSerial::printf(const char *fmt, ...)
{
    if (!serialLogEnabled())
        return;
    va_list args;
    va_start(args, fmt);
    char buffer[512];
    const int written = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (written > 0)
        p2s::host::serialWrite(buffer);
}

HostSerial Serial;

// HSV -> RGB used by CRGB(CHSV). Standard FastLED approximation-free
// conversion (hue 0-255, sat/val 0-255).
CRGB::CRGB(const CHSV &hsv)
{
    const uint8_t region = hsv.h / 43;
    const uint8_t remainder = (hsv.h - (region * 43)) * 6;
    const uint8_t p = static_cast<uint8_t>((hsv.v * (255 - hsv.s)) >> 8);
    const uint8_t q = static_cast<uint8_t>((hsv.v * (255 - ((hsv.s * remainder) >> 8))) >> 8);
    const uint8_t t = static_cast<uint8_t>((hsv.v * (255 - ((hsv.s * (255 - remainder)) >> 8))) >> 8);
    switch (region)
    {
    case 0: r = hsv.v; g = t; b = p; break;
    case 1: r = q; g = hsv.v; b = p; break;
    case 2: r = p; g = hsv.v; b = t; break;
    case 3: r = p; g = q; b = hsv.v; break;
    case 4: r = t; g = p; b = hsv.v; break;
    default: r = hsv.v; g = p; b = q; break;
    }
}
