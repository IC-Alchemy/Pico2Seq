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

#ifndef PICO2SEQ_I2C_FASTMODE
// Main I2C bus (Wire: OLED, MPR121, TMAG5273, VL53L1X) at 400 kHz fast mode.
// Default OFF: the 2026-09-07 bench run showed constant OLED glitches and
// freezes with fast mode on this rig, matching the earlier Wire1 tile-bank
// finding (400 kHz stalls transfers, commit 492bd3c). The OLED library still
// runs its own frame pushes at 400 kHz (its preclk default) as it always has;
// this switch only extends fast mode to the idle/sensor traffic between
// frames, which is what the ~4x sensor-transaction speedup buys.
#define PICO2SEQ_I2C_FASTMODE 0
#endif
