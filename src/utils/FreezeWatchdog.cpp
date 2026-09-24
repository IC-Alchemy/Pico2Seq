#include "FreezeWatchdog.h"

#if defined(__arm__)
#include "hardware/structs/scb.h"
#include "pico/platform.h"

// This record is intentionally not in .bss: the SDK startup code does not clear
// NOINIT RAM on a watchdog/warm reset. The magic is written last by the handler;
// bootCheck accepts it only when the watchdog reset marker is also present.
volatile FreezeFaultRecord __uninitialized_ram(g_freezeFaultRecord);

// A normal C++ function is not safe as the first instruction of an exception
// handler: its prologue may push registers and move the active stack before the
// stacked frame can be inspected. Select the stack from EXC_RETURN and pass both
// the frame pointer and the original EXC_RETURN to a normal helper.
extern "C" __attribute__((naked)) void freezeWatchdogHardFaultHandler()
{
    __asm__ volatile(
        "tst lr, #4\n"
        "bne 1f\n"
        "mrs r0, msp\n"
        "b 2f\n"
        "1: mrs r0, psp\n"
        "2: mov r1, lr\n"
        "b freezeWatchdogCaptureHardFault\n");
}

extern "C" __attribute__((noinline, noreturn))
void freezeWatchdogCaptureHardFault(const uint32_t *frame, uint32_t excReturn)
{
    // Do not clear W1C fault registers or call the watchdog from the handler.
    // Invalidate first, then publish the complete record before setting the
    // validity marker. A reset during capture must not look like a valid fault.
    g_freezeFaultRecord.magic = 0;
    const uint32_t mmfar = scb_hw->mmfar;
    const uint32_t bfar = scb_hw->bfar;
    const uint32_t cfsr = scb_hw->cfsr;
    const uint32_t hfsr = scb_hw->hfsr;
    g_freezeFaultRecord.cfsr = cfsr;
    g_freezeFaultRecord.hfsr = hfsr;
    g_freezeFaultRecord.mmfar = mmfar;
    g_freezeFaultRecord.bfar = bfar;
    g_freezeFaultRecord.pc = 0;
    g_freezeFaultRecord.lr = 0;
    g_freezeFaultRecord.frameValid = 0;
    g_freezeFaultRecord.excReturn = excReturn;
    // The handler is installed and fed on Core 0 only; avoid an extra
    // peripheral read in the fault path while still recording the owner.
    g_freezeFaultRecord.core = 0;
    __asm__ volatile("dmb 0xF" ::: "memory");
    // Commit status-register evidence before touching the stacked frame. For
    // stacking errors the frame may be invalid, but CFSR/HFSR remain useful.
    g_freezeFaultRecord.magic = FREEZE_FAULT_RECORD_MAGIC;
    __asm__ volatile("dsb 0xF" ::: "memory");

    // Best-effort frame details follow the committed status record. Stacking
    // errors mean the frame itself may be invalid; never dereference it then.
    watchdog_hw->scratch[0] = FW_FAULT;
    watchdog_hw->scratch[5] = 0;
    watchdog_hw->scratch[6] = 0;
    const bool stackingError = (cfsr & ((1u << 4) | (1u << 3))) != 0;
    if (!stackingError)
    {
        g_freezeFaultRecord.pc = frame[6];
        g_freezeFaultRecord.lr = frame[5];
        g_freezeFaultRecord.frameValid = 1;
        watchdog_hw->scratch[5] = frame[6];
        watchdog_hw->scratch[6] = frame[5];
    }
    __asm__ volatile("dmb 0xF" ::: "memory");

    // Preserve the compact scratch summary for the existing recovery report.
    for (;;)
    {
    }
}
#endif
