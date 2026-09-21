/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#if (defined ARDUINO_ARCH_RP2040) || (defined ARDUINO_ARCH_RP2350)

#ifndef _PICO_AUDIO_I2S_H
#define _PICO_AUDIO_I2S_H

#include "audio.h"

/** \file audio_i2s.h
 *  \defgroup pico_audio_i2s pico_audio_i2s
 *  I2S audio output using the PIO
 *
 * Pico2Seq use: Core 1's only sound outlet (48 kHz stereo, GP10-12). The DMA IRQ
 * feeds PIO while loop1() renders ahead; an underrun substitutes silence, which
 * the performer hears as a dropout — hence the pre-fill and heartbeat counters.
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PICO_AUDIO_I2S_DMA_IRQ
#ifdef PICO_AUDIO_DMA_IRQ
#define PICO_AUDIO_I2S_DMA_IRQ PICO_AUDIO_DMA_IRQ
#else
#define PICO_AUDIO_I2S_DMA_IRQ 1
#endif
#endif

#ifndef PICO_AUDIO_I2S_PIO
#ifdef PICO_AUDIO_PIO
#define PICO_AUDIO_I2S_PIO PICO_AUDIO_PIO
#else
#define PICO_AUDIO_I2S_PIO 1
#endif
#endif

#if !(PICO_AUDIO_I2S_DMA_IRQ == 0 || PICO_AUDIO_I2S_DMA_IRQ == 1)
#error PICO_AUDIO_I2S_DMA_IRQ must be 0 or 1
#endif

#if !(PICO_AUDIO_I2S_PIO == 0 || PICO_AUDIO_I2S_PIO == 1)
#error PICO_AUDIO_I2S_PIO ust be 0 or 1
#endif

#ifndef PICO_AUDIO_I2S_MAX_CHANNELS
#ifdef PICO_AUDIO_MAX_CHANNELS
#define PICO_AUDIO_I2S_MAX_CHANNELS PICO_AUDIO_MAX_CHANNELS
#else
#define PICO_AUDIO_I2S_MAX_CHANNELS 2u
#endif
#endif

#ifndef PICO_AUDIO_I2S_BUFFERS_PER_CHANNEL
#ifdef PICO_AUDIO_BUFFERS_PER_CHANNEL
#define PICO_AUDIO_I2S_BUFFERS_PER_CHANNEL PICO_AUDIO_BUFFERS_PER_CHANNEL
#else
#define PICO_AUDIO_I2S_BUFFERS_PER_CHANNEL 3u
#endif
#endif

#ifndef PICO_AUDIO_I2S_BUFFER_SAMPLE_LENGTH
#ifdef PICO_AUDIO_BUFFER_SAMPLE_LENGTH
#define PICO_AUDIO_I2S_BUFFER_SAMPLE_LENGTH PICO_AUDIO_BUFFER_SAMPLE_LENGTH
#else
#define PICO_AUDIO_I2S_BUFFER_SAMPLE_LENGTH 576u
#endif
#endif

#ifndef PICO_AUDIO_I2S_SILENCE_BUFFER_SAMPLE_LENGTH
#ifdef PICO_AUDIO_I2S_SILENCE_BUFFER_SAMPLE_LENGTH
#define PICO_AUDIO_I2S_SILENCE_BUFFER_SAMPLE_LENGTH PICO_AUDIO_SILENCE_BUFFER_SAMPLE_LENGTH
#else
#define PICO_AUDIO_I2S_SILENCE_BUFFER_SAMPLE_LENGTH 256u
#endif
#endif

// Allow use of pico_audio driver without actually doing anything much
#ifndef PICO_AUDIO_I2S_NOOP
#ifdef PICO_AUDIO_NOOP
#define PICO_AUDIO_I2S_NOOP PICO_AUDIO_NOOP
#else
#define PICO_AUDIO_I2S_NOOP 0
#endif
#endif

#ifndef PICO_AUDIO_I2S_MONO_INPUT
#define PICO_AUDIO_I2S_MONO_INPUT 0
#endif
#ifndef PICO_AUDIO_I2S_MONO_OUTPUT
#define PICO_AUDIO_I2S_MONO_OUTPUT 0
#endif





// Base pin = BCLK, base+1 = LRCK (Pico2Seq: GP10/GP11, data GP12). Swapped order
// is supported but unused; keep the default so HardwarePins.h stays true.
#ifndef PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#endif

/** \brief Base configuration for one I2S outlet (pins + claimed DMA/SM)
 * \ingroup pico_audio_i2s
 */
// AUTO claims a free DMA channel/SM at setup so coexisting users never collide.
#define PICO_AUDIO_I2S_DMA_CHANNEL_AUTO UINT8_MAX
#define PICO_AUDIO_I2S_PIO_SM_AUTO UINT8_MAX

typedef struct audio_i2s_config {
    uint8_t data_pin;
    uint8_t clock_pin_base;
    uint8_t dma_channel; // channel number, or PICO_AUDIO_I2S_DMA_CHANNEL_AUTO
    uint8_t pio_sm;
} audio_i2s_config_t;

/** \brief Set up system to output I2S audio
 * \ingroup pico_audio_i2s
 *
 * \param intended_audio_format \todo
 * \param config The configuration to apply.
 */
const audio_format_t *audio_i2s_setup(const audio_format_t *intended_audio_format,
                                               const audio_i2s_config_t *config);


/** \brief Connect a producer pool straight through to I2S (no extra copy)
 * \ingroup pico_audio_i2s
 *
 * Pico2Seq's path: identical PCM16 stereo formats make this the zero-copy route.
 */
bool audio_i2s_connect_thru(audio_buffer_pool_t *producer, audio_connection_t *connection);


/** \brief Connect with the default consumer format (s16 stereo)
 * \ingroup pico_audio_i2s
 */
bool audio_i2s_connect(audio_buffer_pool_t *producer);


/** \brief Connect an 8-bit producer pool to I2S (unused by Pico2Seq)
 * \ingroup pico_audio_i2s
 */
bool audio_i2s_connect_s8(audio_buffer_pool_t *producer);

/** \brief Connect with an explicit consumer pool (buffer_on_give path)
 * \ingroup pico_audio_i2s
 *
 * Pico2Seq passes buffer_on_give=false with zero consumer buffers to select
 * the pass-through connection above.
 */
bool audio_i2s_connect_extra(audio_buffer_pool_t *producer, bool buffer_on_give, uint buffer_count,
                                 uint samples_per_buffer, audio_connection_t *connection);


/** \brief Enable/disable the I2S clocks (call after the pre-fill, not before)
 * \ingroup pico_audio_i2s
 */
void audio_i2s_set_enabled(bool enabled);

// Read from the audio core only; IRQ-owned counters sampled into its heartbeat.
uint32_t audio_i2s_underrun_count(void);
uint32_t audio_i2s_tx_stall_count(void);
// Setup-stage probe for bring-up diagnostics; zero means setup has not entered
// the driver. It is read by Core 0 and written only by Core 1.
uint32_t audio_i2s_setup_stage(void);

#ifdef __cplusplus
}
#endif

#endif //_AUDIO_I2S_H
#endif
