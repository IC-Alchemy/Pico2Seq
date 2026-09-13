#pragma once

// Freeze forensics for the control core (Core 0): a hardware watchdog plus a
// phase breadcrumb trail.
//
// How it works
//   - freezeWatchdogArm() arms the RP2350 hardware watchdog (2s) and installs a
//     hard-fault handler. From that point any hang or fault on Core 0 reboots
//     the board within ~2 seconds.
//   - Every step of setup() and every slice of loop() calls
//     freezeWatchdogFeed(phase) *before* the work runs, recording the phase
//     (plus millis and the processed-step count) in watchdog scratch registers.
//   - After the reboot, freezeWatchdogBootCheck() prints a post-mortem on
//     Serial: what the control core was doing when it stopped, and PC/LR if a
//     hard fault was captured.
//
// Scratch register map (survive a watchdog/warm reset, cleared by power-on):
//   [0] FreezePhase at last feed, or FW_FAULT / FW_C1_FAULT after a hard fault
//   [1] millis() at last feed          [2] boot counter (every boot)
//   [3] g_processedStepCount at feed   [5] fault: stacked PC   [6] fault: LR
//   [7] fault: CFSR (fault cause bits)
//   [4] is owned by pico-sdk's watchdog_enable marker - do not use.

#include <Arduino.h>
#include "hardware/watchdog.h"
#include "hardware/exception.h"

extern uint32_t g_processedStepCount; // defined in src/app/ClockService.cpp

enum FreezePhase : uint32_t
{
    FW_NONE = 0,
    FW_SETUP_BOOTCHECK,
    FW_SETUP_USB,
    FW_SETUP_BUS,
    FW_SETUP_SENSORS,
    FW_SETUP_MPR121,
    FW_SETUP_OLED,
    FW_SETUP_MATRIX,
    FW_SETUP_ALCHEMY,
    FW_SETUP_UCLOCK,
    FW_LOOP_USB_READ,
    FW_LOOP_HELD_BUTTONS,
    FW_LOOP_CLOCK_EVENTS,
    FW_LOOP_PPQN,
    FW_LOOP_CONTROL,
    FW_LOOP_DISPLAY,
    FW_FAULT = 0xDEADF00D,
    FW_C1_FAULT = 0xDEADC0DE,
};

static const char *freezeWatchdogPhaseName(uint32_t phase)
{
    switch (phase)
    {
    case FW_SETUP_BOOTCHECK: return "setup: boot check";
    case FW_SETUP_USB:       return "setup: USB CDC serial";
    case FW_SETUP_BUS:       return "setup: Wire/LED matrix";
    case FW_SETUP_SENSORS:   return "setup: VL53L1X + TMAG5273";
    case FW_SETUP_MPR121:    return "setup: MPR121";
    case FW_SETUP_OLED:      return "setup: OLED";
    case FW_SETUP_MATRIX:    return "setup: touch matrix";
    case FW_SETUP_ALCHEMY:   return "setup: Alchemy tiles";
    case FW_SETUP_UCLOCK:    return "setup: uClock start";
    case FW_LOOP_USB_READ:   return "loop: housekeeping (midi read removed)";
    case FW_LOOP_HELD_BUTTONS: return "loop: pollUIHeldButtons";
    case FW_LOOP_CLOCK_EVENTS: return "loop: processClockEvents";
    case FW_LOOP_PPQN:       return "loop: PPQN drain";
    case FW_LOOP_CONTROL:    return "loop: control slice (I2C sensors/matrix/tiles)";
    case FW_LOOP_DISPLAY:    return "loop: display slice (OLED/LED)";
    case FW_FAULT:           return "HARD FAULT";
    case FW_C1_FAULT:        return "HARD FAULT (audio core)";
    default:                 return "unknown";
    }
}

// Breadcrumb only - no watchdog reload, safe before freezeWatchdogArm().
static inline void freezeWatchdogMark(uint32_t phase)
{
    watchdog_hw->scratch[0] = phase;
    watchdog_hw->scratch[1] = millis();
    watchdog_hw->scratch[3] = g_processedStepCount;
}

// Call between setup() stages and at loop() phase boundaries (phase = work that
// is about to run, so a hang names the phase that never finished).
static inline void freezeWatchdogFeed(uint32_t phase)
{
    freezeWatchdogMark(phase);
    watchdog_update();
}

#if defined(__arm__)
// Live fault report: written by the fault handler on whichever core faulted,
// drained (and printed) by Core 0's update() loop. Inline variables (C++17)
// so every TU including this header shares one instance.
inline volatile uint32_t freezeFaultPending = 0;
inline volatile uint32_t freezeFaultCore = 0;
inline volatile uint32_t freezeFaultPc = 0;
inline volatile uint32_t freezeFaultLr = 0;
inline volatile uint32_t freezeFaultCfsr = 0;
inline volatile uint32_t freezeFaultBfar = 0;

// Capture the stacked PC/LR of the faulting code plus CFSR/BFAR and the
// faulting core, then force an immediate watchdog reboot. Handles both the
// plain and the extended (FPU) frame layout, since the integer block comes
// first in both. The forced reboot matters for audio-core faults: Core 0 keeps
// feeding the watchdog, so waiting for it to starve would hang forever with
// the evidence sitting in scratch registers that a power cycle wipes. Usage/
// Bus/MemManage faults escalate into this handler (they stay disabled in
// SHCSR), and their cause bits are readable from CFSR here.
static void freezeWatchdogHardFaultHandler()
{
    uint32_t stackedMsp;
    asm volatile("mrs %0, msp" : "=r"(stackedMsp));
    const uint32_t *frame = reinterpret_cast<const uint32_t *>(stackedMsp);
    uint32_t cfsr = 0;
    uint32_t bfar = 0;
#if defined(PICO_RP2350) && PICO_RP2350
    // SCB fault-status registers (gas has no MRS names for these; CMSIS
    // addresses per ARMv8-M): CFSR 0xE000ED28, BFAR 0xE000ED38.
    cfsr = *(volatile uint32_t *)0xE000ED28;
    bfar = *(volatile uint32_t *)0xE000ED38;
#endif
    const uint32_t core = get_core_num();

    freezeFaultPc = frame[6]; // stacked PC
    freezeFaultLr = frame[5]; // stacked LR
    freezeFaultCfsr = cfsr;
    freezeFaultBfar = bfar;
    freezeFaultCore = core;

    watchdog_hw->scratch[0] = (core == 1) ? FW_C1_FAULT : FW_FAULT;
    watchdog_hw->scratch[5] = frame[6];
    watchdog_hw->scratch[6] = frame[5];
    watchdog_hw->scratch[7] = cfsr;

    asm volatile("dmb sy" ::: "memory");
    freezeFaultPending = 1; // Core 0 prints this if it gets the chance.

    watchdog_reboot(0, 0, 0); // TRIGGER now; scratch survives the reset.
    for (;;)
    {
    }
}
#endif

// Post-mortem staged in RAM by freezeWatchdogBootCheck(), printed by Core 0's
// diagnostics loop once a serial host attaches (inline variables, C++17, so
// every TU including this header shares one instance).
inline volatile bool freezePostMortemPending = false;
inline volatile uint32_t freezePmWatchdog = 0;
inline volatile uint32_t freezePmPhase = 0;
inline volatile uint32_t freezePmBoots = 0;
inline volatile uint32_t freezePmSteps = 0;
inline volatile uint32_t freezePmAtMs = 0;
inline volatile uint32_t freezePmPc = 0;
inline volatile uint32_t freezePmLr = 0;
inline volatile uint32_t freezePmCfsr = 0;

// Call once, after Wire.begin() in setup(): from here on, any hang reboots.
static inline void freezeWatchdogArm()
{
    watchdog_hw->scratch[2] = watchdog_hw->scratch[2] + 1; // boot counter
#if defined(__arm__)
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION, freezeWatchdogHardFaultHandler);
#endif
    freezeWatchdogMark(FW_NONE);
    watchdog_enable(2000, true); // 2s budget; worst loop iteration is ~0.5s
}

// First call in setup(): stages the previous run's post-mortem into RAM (it is
// printed by Core 0's diagnostics loop once a serial host attaches) and clears
// the scratch evidence. Printing here with a bounded wait for the host raced
// USB re-enumeration and lost the post-mortem on every reboot; now no evidence
// is dropped no matter how late the monitor reconnects.
static inline void freezeWatchdogBootCheck()
{
    const uint32_t phase = watchdog_hw->scratch[0];
    const uint32_t boots = watchdog_hw->scratch[2];
    const uint32_t steps = watchdog_hw->scratch[3];

    const bool watchdogReset = watchdog_caused_reboot();
    // A warm reset WITHOUT the watchdog flag still matters (reset pin, debug
    // reset, a crash-reboot path that never marked FW_FAULT).
    if (!watchdogReset && boots <= 1)
    {
        return;
    }

    freezePmWatchdog = watchdogReset;
    freezePmPhase = phase;
    freezePmBoots = boots;
    freezePmSteps = steps;
    freezePmAtMs = watchdog_hw->scratch[1];
    freezePmPc = watchdog_hw->scratch[5];
    freezePmLr = watchdog_hw->scratch[6];
    freezePmCfsr = watchdog_hw->scratch[7];

    // Consume the evidence so the next normal reboot stays quiet.
    watchdog_hw->scratch[0] = FW_NONE;
    watchdog_hw->scratch[5] = 0;
    watchdog_hw->scratch[6] = 0;
    watchdog_hw->scratch[7] = 0;
    freezeWatchdogMark(FW_SETUP_BOOTCHECK);

    asm volatile("dmb sy" ::: "memory");
    freezePostMortemPending = true;
}

// Called from Core 0's diagnostics loop while a serial host is attached.
static inline void freezeWatchdogPrintPostMortem()
{
    if (!freezePostMortemPending)
    {
        return;
    }
    freezePostMortemPending = false;

    Serial.println("=================================================");
    Serial.print("[FREEZE] POST-MORTEM (previous run ");
    if (freezePmWatchdog)
    {
        Serial.print("watchdog-rebooted");
    }
    else
    {
        Serial.print("warm-reset");
    }
    Serial.println(")");
    Serial.print("[FREEZE] boot #");
    Serial.println(freezePmBoots);
    Serial.print("[FREEZE] control core was in: ");
    Serial.print(freezeWatchdogPhaseName(freezePmPhase));
    Serial.print(" (0x");
    Serial.print(freezePmPhase, HEX);
    Serial.println(")");
    Serial.print("[FREEZE] froze at ~");
    Serial.print(freezePmAtMs);
    Serial.print(" ms after boot, after ");
    Serial.print(freezePmSteps);
    Serial.println(" processed 16th-note steps");
    if (freezePmPhase == FW_FAULT || freezePmPhase == FW_C1_FAULT)
    {
        Serial.print("[FREEZE] fault PC=0x");
        Serial.print(freezePmPc, HEX);
        Serial.print(" LR=0x");
        Serial.println(freezePmLr, HEX);
        Serial.print("[FREEZE] CFSR=0x");
        Serial.println(freezePmCfsr, HEX);
    }
    Serial.println("=================================================");
}
