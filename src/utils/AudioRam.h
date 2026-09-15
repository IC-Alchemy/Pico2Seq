#pragma once

// Set to 0 for a same-clock XIP/RAM hardware timing comparison.
#ifndef PICO2SEQ_AUDIO_IN_RAM
#define PICO2SEQ_AUDIO_IN_RAM 1
#endif

#if defined(ARDUINO_ARCH_RP2040) && PICO2SEQ_AUDIO_IN_RAM
#include <pico.h>
// The Pico linker puts .time_critical.* in SRAM and startup copies it from
// flash. Allow existing inlining inside the RAM render path; do not annotate
// the always_inline Voice stages separately from their caller.
#define PICO2SEQ_AUDIO_FUNC(name) __not_in_flash_func(name)
#else
// Host tests and the comparison build retain ordinary function placement.
#define PICO2SEQ_AUDIO_FUNC(name) name
#endif
