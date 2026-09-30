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
// rpdsp functions that cannot be named here (DarkReverb's process() overloads are
// class-template members) take their placement from this hook instead: one shared
// .time_critical.* section, which the same linker rule places in SRAM. It must be
// defined before the first rpdsp header of a translation unit that instantiates
// them, so MasterReverb.h includes this file first. Without it the linked ELF has
// them in flash, reached from the RAM render loop through a veneer.
// Many translation units include an rpdsp header (whose config.h defaults the hook to
// nothing) before this file; only MasterReverb.h's own include order matters, and the
// #undef keeps the later definition from being reported as a redefinition.
#ifdef RPDSP_HOT_FUNCTION
#undef RPDSP_HOT_FUNCTION
#endif
#define RPDSP_HOT_FUNCTION __attribute__((section(".time_critical.rpdsp")))
#else
// Host tests and the comparison build retain ordinary function placement.
#define PICO2SEQ_AUDIO_FUNC(name) name
#endif
