# AGENTS.md

This file provides guidance to agents when working with code in this repository.

Build / run
- Open [`Pico2Seq.ino`](Pico2Seq.ino:1) in Arduino IDE, select "Raspberry Pi Pico2"/"RP2350", install libs listed in [`README.md`](README.md:71). No PlatformIO/CMake present.

Hardware & wiring gotchas
- I2S: GP15=data, GP16=BCK, GP17=LRCK; touch matrix rows GP0-3, cols GP4-11; LED data GP1. (see [`README.md`](README.md:105))
- I2C: SDA=GP12, SCL=GP13. Addresses: OLED=0x3C, AS5600=0x36, VL53L1X=0x29. (see [`README.md`](README.md:114))

Runtime constraints & audio
- Audio runs on Core0, UI on Core1 — avoid unsynchronized audio-state changes. ([`README.md`](README.md:198))
- DSP is vendored in [`src/dsp/`](src/dsp/dsp.h:1); don't replace with external Daisy without checking API.
- Audio path uses preallocated buffers (SAMPLES_PER_BUFFER=256, NUM_AUDIO_BUFFERS=3) and avoids heap; use audio_buffer_pool_t (`Pico2Seq.ino`:31).

Project-specific rules (non-obvious)
- Note edits are ignored if a step's Gate is LOW — see updateParametersForStep (`Pico2Seq.ino`:318).
- UI voice numbers are 1..4; internal ids are 0-based — conversions used (applyVoicePreset at `Pico2Seq.ino`:253).

Testing & CI
- No automated tests or test framework found in repo.

