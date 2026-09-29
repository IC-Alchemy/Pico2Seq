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
//   - After the reboot, freezeWatchdogBootCheck() saves the post-mortem in RAM.
//     Core 0 repeats it with diagnostics, including after a late USB reconnect.
//
// Scratch register map (survive a watchdog/warm reset, cleared by power-on):
//   [0] FreezePhase at last feed, or FW_FAULT after a hard fault
//   [1] millis() at last feed          [2] boot counter (every boot)
//   [3] g_processedStepCount at feed   [5] fault: stacked PC   [6] fault: LR
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
    FW_LOOP_DISPLAY, // retired: the OLED and LEDs now feed FW_LOOP_OLED / FW_LOOP_LEDS
    // Append phases so scratch evidence from older firmware keeps its meaning.
    FW_SETUP_VOICES,
    FW_LOOP_MATRIX,
    FW_LOOP_TILES,
    FW_LOOP_ENCODER,
    FW_LOOP_DISTANCE,
    FW_LOOP_RECORD,
    FW_LOOP_OLED,
    FW_LOOP_LEDS,
    FW_LOOP_DIAGNOSTICS,
    FW_SETUP_STORAGE,
    FW_FAULT = 0xDEADF00D,
};

// Full Cortex-M fault record retained in NOINIT RAM across a watchdog reset.
// The watchdog scratch registers retain the small, always-available summary;
// this record preserves the fault-status registers for the next boot report.
struct FreezeFaultRecord
{
    uint32_t magic;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t pc;
    uint32_t lr;
    uint32_t excReturn;
    uint32_t core;
    uint32_t frameValid;
};

static constexpr uint32_t FREEZE_FAULT_RECORD_MAGIC = 0x46525554u; // "FRUT"

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
    case FW_SETUP_VOICES:    return "setup: voices";
    case FW_LOOP_MATRIX:     return "loop: MPR121 scan";
    case FW_LOOP_TILES:      return "loop: Alchemy tile scan";
    case FW_LOOP_ENCODER:    return "loop: magnetic encoder";
    case FW_LOOP_DISTANCE:   return "loop: distance sensor";
    case FW_LOOP_RECORD:     return "loop: parameter recording";
    case FW_LOOP_OLED:       return "loop: OLED update";
    case FW_LOOP_LEDS:       return "loop: LED render/transfer";
    case FW_LOOP_DIAGNOSTICS:return "loop: serial diagnostics";
    case FW_SETUP_STORAGE:   return "setup: session storage";
    case FW_FAULT:           return "HARD FAULT";
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
// Defined in FreezeWatchdog.cpp. The handler must be a naked assembly entry
// point so it can read the original exception frame before a C++ prologue moves
// MSP or changes the selected stack.
extern "C" void freezeWatchdogHardFaultHandler();
extern volatile FreezeFaultRecord g_freezeFaultRecord;
#endif

// Call once, after Wire.begin() in setup(): from here on, any hang reboots.
static inline void freezeWatchdogArm()
{
    watchdog_hw->scratch[2] = watchdog_hw->scratch[2] + 1; // boot counter
#if defined(__arm__)
    if (exception_get_vtable_handler(HARDFAULT_EXCEPTION) != freezeWatchdogHardFaultHandler)
    {
        exception_set_exclusive_handler(HARDFAULT_EXCEPTION, freezeWatchdogHardFaultHandler);
    }
#endif
    freezeWatchdogMark(FW_NONE);
    watchdog_enable(2000, true); // 2s budget; worst loop iteration is ~0.5s
}

// One shared snapshot across Application.cpp and ControlIO.cpp. Capture it
// before setup overwrites scratch, then retain it for a late USB reconnect.
struct FreezeWatchdogReport
{
    bool watchdogReset = false;
    bool faultRecordValid = false;
    uint32_t phase = FW_NONE;
    uint32_t frozeAtMs = 0;
    uint32_t boots = 0;
    uint32_t steps = 0;
    uint32_t pc = 0;
    uint32_t lr = 0;
    uint32_t cfsr = 0;
    uint32_t hfsr = 0;
    uint32_t mmfar = 0;
    uint32_t bfar = 0;
    uint32_t excReturn = 0;
    uint32_t core = 0;
    bool frameValid = false;
};
inline FreezeWatchdogReport previousFreeze;

// Called at the diagnostic interval on Core 0. Never wait for a serial host;
// an unopened port must not consume the report or delay firmware startup.
static inline void freezeWatchdogPrintPreviousRun()
{
    if (!Serial || !previousFreeze.watchdogReset)
        return;
    Serial.printf("[FREEZE] previous watchdog reset: boot=%lu phase=%s (0x%lx) ms=%lu steps=%lu\n",
                  (unsigned long)previousFreeze.boots,
                  freezeWatchdogPhaseName(previousFreeze.phase),
                  (unsigned long)previousFreeze.phase,
                  (unsigned long)previousFreeze.frozeAtMs,
                  (unsigned long)previousFreeze.steps);
    if (previousFreeze.phase == FW_FAULT || previousFreeze.faultRecordValid)
    {
        const char *frameState = !previousFreeze.faultRecordValid
                                     ? "unknown"
                                     : (previousFreeze.frameValid ? "valid" : "unavailable");
        Serial.printf("[FREEZE] captured fault frame=%s PC=0x%lx LR=0x%lx\n",
                      frameState,
                      (unsigned long)previousFreeze.pc, (unsigned long)previousFreeze.lr);
        if (previousFreeze.faultRecordValid)
        {
            const bool bfarValid = (previousFreeze.cfsr & (1u << 15)) != 0;
            const bool mmfarValid = (previousFreeze.cfsr & (1u << 7)) != 0;
            Serial.printf("[FREEZE] fault core=%lu EXC_RETURN=0x%lx CFSR=0x%lx HFSR=0x%lx BFAR=0x%lx(valid=%u) MMFAR=0x%lx(valid=%u)\n",
                          (unsigned long)previousFreeze.core,
                          (unsigned long)previousFreeze.excReturn,
                          (unsigned long)previousFreeze.cfsr,
                          (unsigned long)previousFreeze.hfsr,
                          (unsigned long)previousFreeze.bfar,
                          static_cast<unsigned>(bfarValid),
                          (unsigned long)previousFreeze.mmfar,
                          static_cast<unsigned>(mmfarValid));
        }
    }
}

// First call in setup(): save evidence before any feed/arm can overwrite it.
static inline void freezeWatchdogBootCheck()
{
    // The enable marker distinguishes our timeout from USB upload/reboot
    // requests, which also use the hardware watchdog to reset the chip.
    previousFreeze = {watchdog_enable_caused_reboot(), false, watchdog_hw->scratch[0],
                      watchdog_hw->scratch[1], watchdog_hw->scratch[2],
                      watchdog_hw->scratch[3], watchdog_hw->scratch[5],
                      watchdog_hw->scratch[6]};
#if defined(__arm__)
    if (previousFreeze.watchdogReset && g_freezeFaultRecord.magic == FREEZE_FAULT_RECORD_MAGIC)
    {
        previousFreeze.faultRecordValid = true;
        // Keep the compact scratch PC/LR when the stacked frame was not
        // trustworthy (for example, a stacking error). The status record is
        // still valid in that case.
        if (g_freezeFaultRecord.frameValid)
        {
            previousFreeze.pc = g_freezeFaultRecord.pc;
            previousFreeze.lr = g_freezeFaultRecord.lr;
        }
        previousFreeze.cfsr = g_freezeFaultRecord.cfsr;
        previousFreeze.hfsr = g_freezeFaultRecord.hfsr;
        previousFreeze.mmfar = g_freezeFaultRecord.mmfar;
        previousFreeze.bfar = g_freezeFaultRecord.bfar;
        previousFreeze.excReturn = g_freezeFaultRecord.excReturn;
        previousFreeze.core = g_freezeFaultRecord.core;
        previousFreeze.frameValid = g_freezeFaultRecord.frameValid != 0;
    }
#endif
#if defined(__arm__)
    // Consume the NOINIT record once; a later watchdog timeout without a
    // HardFault must not inherit the previous fault's status-register report.
    g_freezeFaultRecord.magic = 0;
#endif
    watchdog_hw->scratch[0] = FW_NONE;
    watchdog_hw->scratch[5] = 0;
    watchdog_hw->scratch[6] = 0;
}
