#pragma once
// Desktop replacement for the Pico SDK's hardware/sync.h.
//
// The firmware uses save_and_disable_interrupts()/restore_interrupts() to
// make queue-drain sections atomic against the uClock ISR. The desktop
// equivalent is one global recursive mutex that the HostClock tick runner
// also holds while firing clock callbacks — reproducing ISR semantics
// between the control thread and the audio thread.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t save_and_disable_interrupts(void);
void restore_interrupts(uint32_t state);

#ifdef __cplusplus
}
#endif
