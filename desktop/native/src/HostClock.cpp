// Desktop uClock implementation.
//
// Faithful port of the internal-clock timing core of uClock 2.2.1
// (midilab/uClock, MIT license, (c) 2024 Romulo Silva): handleTimerInt() and
// processShuffle() below reproduce the library's counter and shuffle
// semantics one-for-one so sequencer feel matches the hardware exactly.
// Differences from the library, all external-clock related and unused by
// the firmware: no external sync/PLL path, and ticks are scheduled by an
// absolute nanosecond deadline advanced from the audio render callback
// instead of a re-armed hardware timer.

#include "HostClock.h"
#include "uClock.h"
#include "hardware/sync.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace
{
constexpr uint8_t kMaxShuffleTemplateSize = 16;
constexpr float kMinBpm = 1.0f;
constexpr float kMaxBpm = 400.0f;

struct IrqLock
{
    uint32_t state;
    IrqLock() : state(save_and_disable_interrupts()) {}
    ~IrqLock() { restore_interrupts(state); }
};

// ---- uClockClass state (guarded by the global interrupt mutex) ----------
struct ClockState
{
    float tempo = 120.0f;
    uint16_t outputPpqn = 480;
    uint16_t inputPpqn = 480;
    uint16_t modStepRef = 480 / 4; // setOutputPPQN keeps this in sync
    bool started = false;
    bool startArmed = false; // start() called; next advance() begins firing

    // Shuffle template (uClock::Shuffle).
    bool shuffleActive = false;
    uint8_t shuffleSize = kMaxShuffleTemplateSize;
    int8_t shuffleStep[kMaxShuffleTemplateSize] = {0};
    bool shuffleShootCtrl = false;
    int8_t lastShff = 0;
    int8_t shuffleLengthCtrl = 0;

    // Tick counters (touched only inside fired ticks or reset).
    uint32_t tick = 0;
    uint32_t stepCounter = 0;
    uint16_t modStepCounter = 0;
    uint16_t modClockCounter = 0;

    // Absolute ns deadline for the next tick plus the fractional carry, so
    // fractional tick periods (e.g. 1388888.888 ns at 90 BPM) never drift.
    uint64_t nextFireNs = 0;
    double fracNs = 0;

    uClockVoidCallback onClockStart = nullptr;
    uClockVoidCallback onClockStop = nullptr;
    uClockTickCallback onStep = nullptr;
    uClockTickCallback onOutputPpqn = nullptr;
};

ClockState g_clock;

double tickGapNs()
{
    return 60000000000.0 / (static_cast<double>(g_clock.outputPpqn) * g_clock.tempo);
}

void scheduleNextFire()
{
    const double gap = tickGapNs();
    uint64_t whole = static_cast<uint64_t>(gap);
    g_clock.fracNs += gap - static_cast<double>(whole);
    if (g_clock.fracNs >= 1.0)
    {
        const uint64_t extra = static_cast<uint64_t>(g_clock.fracNs);
        whole += extra;
        g_clock.fracNs -= static_cast<double>(extra);
    }
    g_clock.nextFireNs += whole;
}

void resetCounters()
{
    g_clock.tick = 0;
    g_clock.stepCounter = 0;
    g_clock.modStepCounter = 0;
    g_clock.modClockCounter = 0;
    g_clock.shuffleShootCtrl = false;
    g_clock.lastShff = 0;
    g_clock.shuffleLengthCtrl = 0;
}

// Port of uClockClass::processShuffle() — do not reformat; the shape mirrors
// the upstream library so future library diffs stay readable.
bool processShuffle()
{
    if (!g_clock.shuffleActive)
    {
        return g_clock.modStepCounter == 0;
    }

    int16_t mod_shuffle = 0;

    // check shuffle template of current
    int8_t shff = g_clock.shuffleStep[g_clock.stepCounter % g_clock.shuffleSize];

    if (g_clock.shuffleShootCtrl == false && g_clock.modStepCounter == 0)
        g_clock.shuffleShootCtrl = true;

    if (shff >= 0)
    {
        mod_shuffle = g_clock.modStepCounter - shff;
        // any late shuffle? we should skip next mod_step_counter == 0
        if (g_clock.lastShff < 0 && g_clock.modStepCounter != 1)
            return false;
    }
    else if (shff < 0)
    {
        mod_shuffle = g_clock.modStepCounter - (g_clock.modStepRef + shff);
        g_clock.shuffleShootCtrl = true;
    }

    g_clock.lastShff = shff;

    // shuffle_shoot_ctrl helps keep track if we have shoot or not a note for
    // the step space of output_ppqn/4 pulses
    if (mod_shuffle == 0 && g_clock.shuffleShootCtrl == true)
    {
        // keep track of next note shuffle for current note lenght control
        g_clock.shuffleLengthCtrl = g_clock.shuffleStep[(g_clock.stepCounter + 1) % g_clock.shuffleSize];
        if (shff > 0)
            g_clock.shuffleLengthCtrl -= shff;
        if (shff < 0)
            g_clock.shuffleLengthCtrl += shff;
        g_clock.shuffleShootCtrl = false;
        return true;
    }

    return false;
}

// Port of uClockClass::handleTimerInt() — internal-clock mode only (the
// firmware never uses external sync). Sync callbacks are omitted because
// the firmware registers none.
void fireTick()
{
    // (uClock's mod_clock_counter / int_clock_tick bookkeeping exists only
    // for external-clock resync; internal-clock mode drops it.)

    // main PPQNCallback
    if (g_clock.onOutputPpqn)
    {
        g_clock.onOutputPpqn(g_clock.tick);
        ++g_clock.tick;
    }

    // step callback to support 16th old school style sequencers
    // with builtin shuffle for this callback only
    if (g_clock.onStep)
    {
        if (g_clock.modStepCounter == g_clock.modStepRef)
            g_clock.modStepCounter = 0;
        if (processShuffle())
        {
            g_clock.onStep(g_clock.stepCounter);
            ++g_clock.stepCounter;
        }
        ++g_clock.modStepCounter;
    }
}
} // namespace

void p2s::host::advanceClockNs(uint64_t nowNs)
{
    if (g_clock.startArmed)
    {
        g_clock.startArmed = false;
        g_clock.nextFireNs = nowNs; // first tick one full interval after start
        scheduleNextFire();
    }
    if (!g_clock.started)
        return;

    // Fires every tick whose deadline has passed. The lock makes the whole
    // tick (counters + user callbacks) atomic against the control thread's
    // save_and_disable_interrupts() sections, matching ISR semantics.
    while (g_clock.nextFireNs <= nowNs)
    {
        {
            IrqLock lock;
            fireTick();
        }
        scheduleNextFire();
    }
}

void p2s::host::advanceClockBySamples(uint64_t sampleIndex)
{
    // 1e9 ns / 48000 Hz: keep the multiply in 64-bit (exact until ~10^13 samples).
    advanceClockNs((sampleIndex * 1000000ULL) / 48ULL);
}

void p2s::host::resetHostClockForTest()
{
    IrqLock lock;
    g_clock = ClockState{};
}

// ---- uClockClass API ------------------------------------------------------

void uClockClass::init()
{
    // No hardware timer to program on the desktop; defaults already set.
}

void uClockClass::start()
{
    IrqLock lock;
    resetCounters();
    if (g_clock.onClockStart) // runs under lock, like the firmware's loop context
        g_clock.onClockStart();
    g_clock.started = true;
    g_clock.startArmed = true;
}

void uClockClass::stop()
{
    IrqLock lock;
    g_clock.started = false;
    g_clock.startArmed = false;
    resetCounters();
    if (g_clock.onClockStop)
        g_clock.onClockStop();
}

void uClockClass::pause()
{
    if (g_clock.started)
        stop();
    else
        start();
}

void uClockClass::setTempo(float bpm)
{
    if (bpm < kMinBpm || bpm > kMaxBpm)
        return;
    IrqLock lock;
    g_clock.tempo = bpm;
}

float uClockClass::getTempo()
{
    IrqLock lock;
    return g_clock.tempo;
}

void uClockClass::setOutputPPQN(PPQNResolution resolution)
{
    if (resolution < PPQN_4)
        return;
    IrqLock lock;
    g_clock.outputPpqn = static_cast<uint16_t>(resolution);
    g_clock.modStepRef = g_clock.outputPpqn / 4;
}

void uClockClass::setInputPPQN(PPQNResolution resolution)
{
    IrqLock lock;
    g_clock.inputPpqn = static_cast<uint16_t>(resolution);
}

void uClockClass::setClockMode(ClockMode) {}
uClockClass::ClockMode uClockClass::getClockMode() { return INTERNAL_CLOCK; }

uint32_t uClockClass::bpmToMicroSeconds(float bpm)
{
    return static_cast<uint32_t>(60000000.0f / (static_cast<float>(g_clock.outputPpqn) * bpm));
}

void uClockClass::setShuffle(bool active)
{
    IrqLock lock;
    g_clock.shuffleActive = active;
}

bool uClockClass::isShuffled() { return g_clock.shuffleActive; }

void uClockClass::setShuffleSize(uint8_t size)
{
    IrqLock lock;
    g_clock.shuffleSize = std::min<uint8_t>(size, kMaxShuffleTemplateSize);
}

void uClockClass::setShuffleData(uint8_t step, int8_t tick)
{
    if (step >= kMaxShuffleTemplateSize)
        return;
    IrqLock lock;
    g_clock.shuffleStep[step] = tick;
}

void uClockClass::setShuffleTemplate(int8_t *shuff, uint8_t size)
{
    IrqLock lock;
    g_clock.shuffleSize = std::min<uint8_t>(size, kMaxShuffleTemplateSize);
    for (uint8_t i = 0; i < g_clock.shuffleSize; ++i)
        g_clock.shuffleStep[i] = shuff[i];
}

int8_t uClockClass::getShuffleLength() { return g_clock.shuffleLengthCtrl; }

void uClockClass::setOnStep(uClockTickCallback callback)
{
    IrqLock lock;
    g_clock.onStep = callback;
}
void uClockClass::setOnOutputPPQN(uClockTickCallback callback)
{
    IrqLock lock;
    g_clock.onOutputPpqn = callback;
}
void uClockClass::setOnClockStart(uClockVoidCallback callback)
{
    IrqLock lock;
    g_clock.onClockStart = callback;
}
void uClockClass::setOnClockStop(uClockVoidCallback callback)
{
    IrqLock lock;
    g_clock.onClockStop = callback;
}

uClockClass uClock;
