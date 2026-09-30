#if (defined PICO_RP2350) || (defined PICO_RP2040)
#if 1
/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Pico2Seq local note (2026-09-28): removed dead
 * PICO_BUFFER_USB_ALLOC_HACK USB-DPRAM allocation scaffolding to keep only the
 * active calloc-backed behavior.
 */

#ifdef ARDUINO_ARCH_RP2040
#include "buffer.h"
#endif

#endif
#endif