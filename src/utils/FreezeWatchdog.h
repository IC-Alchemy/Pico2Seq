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
//   [0] FreezePhase at last feed, or FW_FAULT after a hard fault
//   [1] millis() at last feed          [2] boot counter (every boot)
//   [3] g_processedStepCount at feed   [5] fault: stacked PC   [6] fault: LR
//   [4] is owned by pico-sdk's watchdog_enable marker - do not use.

#include <Arduino.h>
#include "hardware/watchdog.h"
#include "hardware/exception.h"

extern uint32_t g_processedStepCount; // defined in Pico2Seq.ino

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
};

static const char *freezeWatchdogPhaseName(uint32_t phase)
{
    switch (phase)
    {
    case FW_SETUP_BOOTCHECK: return "setup: boot check";
    case FW_SETUP_USB:       return "setup: USB MIDI / serial";
    case FW_SETUP_BUS:       return "setup: Wire/LED matrix";
    case FW_SETUP_SENSORS:   return "setup: VL53L1X + TMAG5273";
    case FW_SETUP_MPR121:    return "setup: MPR121";
    case FW_SETUP_OLED:      return "setup: OLED";
    case FW_SETUP_MATRIX:    return "setup: touch matrix";
    case FW_SETUP_ALCHEMY:   return "setup: Alchemy tiles";
    case FW_SETUP_UCLOCK:    return "setup: uClock start";
    case FW_LOOP_USB_READ:   return "loop: usb_midi.read";
    case FW_LOOP_HELD_BUTTONS: return "loop: pollUIHeldButtons";
    case FW_LOOP_CLOCK_EVENTS: return "loop: processClockEvents";
    case FW_LOOP_PPQN:       return "loop: PPQN drain";
    case FW_LOOP_CONTROL:    return "loop: control slice (I2C sensors/matrix/tiles)";
    case FW_LOOP_DISPLAY:    return "loop: display slice (OLED/LED)";
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
// Capture the stacked PC/LR of the faulting code, then stall and let the armed
// watchdog reboot us. Handles both the plain and the extended (FPU) frame
// layout, since the integer block comes first in both.
static void freezeWatchdogHardFaultHandler()
{
    uint32_t stackedMsp;
    asm volatile("mrs %0, msp" : "=r"(stackedMsp));
    const uint32_t *frame = reinterpret_cast<const uint32_t *>(stackedMsp);
    watchdog_hw->scratch[0] = FW_FAULT;
    watchdog_hw->scratch[5] = frame[6]; // stacked PC
    watchdog_hw->scratch[6] = frame[5]; // stacked LR
    for (;;)
    {
    }
}
#endif

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

// First call in setup(): if the previous run died, say where. Waits up to 3s
// for a serial host so the post-mortem is not dropped.
static inline void freezeWatchdogBootCheck()
{
    // A warm reset WITHOUT the watchdog flag still matters (reset pin, debug
    // reset, a crash-reboot path that never marked FW_FAULT). scratch[2]
    // survives warm resets and is cleared by power-on, so > 1 proves a
    // warm-reset chain. No serial wait - best effort, next boot repeats it.
    if (!watchdog_caused_reboot() && watchdog_hw->scratch[2] > 1)
    {
        Serial.begin(115200);
        Serial.print("[FREEZE] warm reset #");
        Serial.println(watchdog_hw->scratch[2]);
    }

    if (!watchdog_caused_reboot())
    {
        return;
    }

    const uint32_t phase = watchdog_hw->scratch[0];
    const uint32_t frozeAtMs = watchdog_hw->scratch[1];
    const uint32_t boots = watchdog_hw->scratch[2];
    const uint32_t steps = watchdog_hw->scratch[3];

    Serial.begin(115200);
    const uint32_t waitStart = millis();
    while (!Serial && (millis() - waitStart < 3000))
    {
    }

    Serial.println("=================================================");
    Serial.println("[FREEZE] POST-MORTEM (previous run died)");
    Serial.print("[FREEZE] boot #");
    Serial.println(boots);
    Serial.print("[FREEZE] control core was in: ");
    Serial.print(freezeWatchdogPhaseName(phase));
    Serial.print(" (0x");
    Serial.print(phase, HEX);
    Serial.println(")");
    Serial.print("[FREEZE] froze at ~");
    Serial.print(frozeAtMs);
    Serial.print(" ms after boot, after ");
    Serial.print(steps);
    Serial.println(" processed 16th-note steps");
    if (phase == FW_FAULT)
    {
        Serial.print("[FREEZE] fault PC=0x");
        Serial.print(watchdog_hw->scratch[5], HEX);
        Serial.print(" LR=0x");
        Serial.println(watchdog_hw->scratch[6], HEX);
    }
    Serial.println("=================================================");

    // Consume the evidence so the next normal reboot stays quiet.
    watchdog_hw->scratch[0] = FW_NONE;
    watchdog_hw->scratch[5] = 0;
    watchdog_hw->scratch[6] = 0;
    freezeWatchdogMark(FW_SETUP_BOOTCHECK);
}
