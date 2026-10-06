/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Pico2Seq local note (2026-09-28): removed dead
 * PICO_BUFFER_USB_ALLOC_HACK branches; this repo always uses calloc-backed
 * allocation here.
 */
#if 1
#ifdef ARDUINO_ARCH_RP2040

#ifndef _PICO_UTIL_BUFFER_H
#define _PICO_UTIL_BUFFER_H

#include "pico/types.h"

/** \file buffer.h
 * \defgroup util_buffer buffer
 * \brief Buffer management
 * \ingroup pico_util
 *
 * Pico2Seq use: backing bytes behind each 256-sample I2S buffer. Allocated once
 * at pool creation on Core 1; never in the render loop (no jitter, no dropouts).
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef DEBUG_MALLOC
#include <stdio.h>
#endif

#include <stdlib.h>

/** \struct mem_buffer
 *  \ingroup util_buffer
 *  \brief Wrapper around static or heap sample bytes (size + flags)
 */
typedef struct mem_buffer {
    size_t size;
    uint8_t *bytes;
    uint8_t flags;
} mem_buffer_t;

inline static bool pico_buffer_alloc_in_place(mem_buffer_t *buffer, size_t size) {
    buffer->bytes = (uint8_t *) calloc(1, size);
    if (buffer->bytes) {
        buffer->size = size;
        return true;
    }
    buffer->size = 0;
    return false;
}

inline static mem_buffer_t *pico_buffer_wrap(uint8_t *bytes, size_t size) {
    mem_buffer_t *buffer = (mem_buffer_t *) malloc(sizeof(mem_buffer_t));
    if (buffer) {
        buffer->bytes = bytes;
        buffer->size = size;
    }
    return buffer;
}

inline static mem_buffer_t *pico_buffer_alloc(size_t size) {
    mem_buffer_t *b = (mem_buffer_t *) malloc(sizeof(mem_buffer_t));
    if (b) {
        if (!pico_buffer_alloc_in_place(b, size)) {
            free(b);
            b = NULL;
        }
    }
    return b;
}

#ifdef __cplusplus
}
#endif
#endif
#endif
#endif
