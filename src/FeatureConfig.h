#pragma once

// Firmware feature switches.
//
// Each switch gates ALL of a feature — its storage, setup, processing, and
// the controls that drive it — so a disabled feature leaves nothing behind:
// no RAM, no CPU, no dead controls. Flip a switch to 1 to compile the whole
// feature back in. Override per build with -D if needed (#ifndef guard).

#ifndef PICO2SEQ_ENABLE_DELAY_EFFECT
// Global delay effect: one rpdsp::DelayLine shared by all voices, with an
// SVF-filtered feedback loop. The delay line alone reserves ~338 KiB of
// static RAM (48000 Hz x 1.8 s x 4 bytes), so it ships disabled until it is
// actually wanted.
#define PICO2SEQ_ENABLE_DELAY_EFFECT 0
#endif
