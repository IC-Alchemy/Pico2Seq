# Audio crackle investigation

Each 256-frame buffer at 48 kHz represents 5333.33 microseconds of audio.
Rendering must sustain that rate, including control updates and DMA servicing.
The existing gain, sample conversion, filters and envelope behavior are retained.

## Removed work

- The previous ARM build constructed a default `ControlUpdate` before checking
  whether each voice's queue was empty. Disassembly of `Voice::applyControlUpdate_`
  showed a **264-byte memcpy from flash on every sample**, including silent or
  disabled voices. Four voices at 48 kHz copied 50,688,000 bytes per second just
  to initialize an unused temporary. Each voice now keeps one audio-owned
  scratch copy. Only a successful queue pop copies a message, and the queue
  releases its slot before DSP uses that copy. FIFO gate handling is unchanged.
- Velocity-to-amplitude routing is cached when initialization or a queued
  configuration selects a parameter layout. It no longer calls the layout
  resolver on every voice/sample. Custom layouts and hard-sync routing remain
  supported.
- The I2S connection now passes PCM16 stereo buffers directly to DMA. Previously
  each completion interrupt copied 1024 bytes into a separate consumer buffer
  before restarting DMA. Four producer buffers preserve the old effective queue
  depth while removing three net sample buffers (3072 bytes). The four control
  scratch copies use roughly 1 KiB of that saving.
- All four buffers are filled before I2S starts. Audio DMA receives high channel
  priority and its Core 1 interrupt receives highest IRQ priority. DMA channel
  priority controls DMA scheduling; it does not change bus priority. See the
  [Pico SDK hardware API](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html).

Audio startup still waits for Core 0's `voicesReady` publication. Automatic DMA
channel claiming, the watchdog and freeze reports remain in place. No clock
increase is required by these changes; validate at the stable **150 MHz** first.

## Hot audio code in SRAM

Pico builds now place the buffer renderer, voice mixer, `Voice::process()`, its
per-sample helpers and all ten recipe process callbacks in `.time_critical.*`.
`src/utils/AudioRam.h` wraps the Pico SDK's `__not_in_flash_func`; the existing
linker script places these sections in SRAM and startup copies their code from
the flash image. The always-inlined oscillator, envelope and filter stages stay
inside their RAM caller. Host builds use ordinary function placement.

This reduces instruction fetches competing with Core 0 for XIP cache space.
It does not make the entire audio core independent of flash: preset/configuration
handlers, recipe descriptors, other read-only data and board-core math/memory
helpers can still use XIP. In particular, Arduino's wrapped math routines in the
6.1.0 core are still linked in flash; moving a caller does not relocate them.
DSP calculations, control ownership and diagnostic timing boundaries are unchanged.

For a controlled hardware comparison, build both variants from the same source
at the same clock (the helper's default clock remains 300 MHz):

```powershell
./scripts/build_pico2seq.ps1 -CpuMHz 150 -AudioInFlash -BuildDirectory build/audio-xip-150
./scripts/build_pico2seq.ps1 -CpuMHz 150 -BuildDirectory build/audio-ram-150
```

`-AudioInFlash` defines `PICO2SEQ_AUDIO_IN_RAM=0`; normal builds default to `1`.
Keep the ELF/map and `build.options.json` from each build. Check the ARM ELF with
`arm-none-eabi-nm -S -C`: the renderer, mixer, hot voice helpers and
`VoiceRecipes` process callbacks should have `0x200...` execution addresses in
the RAM build, versus `0x100...` in the flash build. Helpers fully inlined into
their callers may have no separate symbol. Compare the linked `.data` sizes to
measure the SRAM cost, including alignment and linker veneers.

The 150 MHz comparison with Arduino RP2040 core 6.1.0 and `-O3 -ffast-math`
verified all 19 emitted hot functions above in SRAM:

| Linked measurement | XIP baseline | RAM build |
|---|---:|---:|
| `Voice::process()` execution address | `0x10016e44` | `0x20002110` |
| `.data` (includes RAM code) | 9,496 bytes | 31,944 bytes |
| `.bss` | 25,864 bytes | 25,864 bytes |

The additional SRAM use is **22,448 bytes (21.9 KiB)**. Both firmware builds
passed, as did all 283 CTest cases. These are placement and regression checks;
no hardware timing or listening result has been recorded for this change.

A same-source rebuild of the flash variant (the first `build/audio-xip-150`
artifact predated the final `processFrequencySlew` annotation) reproduced its
symbol layout exactly, confirming the pair differs only by placement: 17 of the
19 functions are byte-identical between the two ELFs modulo addresses and
literal-pool values, and `Voice::process` (32 bytes shorter) /
`processPitchedEngine_` (54 bytes longer) reorder basic blocks as a side effect
of the placement attribute itself, not of different inputs. In the flash build,
17 of the 19 entry points also begin mid 16-byte XIP cache line (the sections
are only 4-byte aligned), so a first fetch wastes part of a line. The
disassembly confirms the cross-calls that still execute from flash in the RAM
build (each reached through an 8-byte SRAM veneer): `__wrap_tanf`, `__wrap_sinf`,
`__wrap_cosf`, `__wrap_expf`, `memset`, `__wrap_memcpy`, `take_audio_buffer`,
`give_audio_buffer`, `micros`/`millis`, and `Voice::applyConfig_`,
`applyParameters_`, `updatePitchCache_`, `applyStructuralConfig_`, plus in-flash
`tbb`/`tbh` jump tables and the waveguide's read-only pointer tables.

Flash each image in turn and use identical presets, gates, slides, tempo, voice
count and OLED/LED/control activity. After startup, record several `[DIAG C1]`
windows for sustained notes and rapid triggers, especially FM and waveguide
presets. Compare `render_us` and `max_us`, and the increases in `over`,
`underruns` and `txstalls` over equal durations. Repeat the comparison to check
variation. Host tests and emulator timings cannot measure the XIP-cache benefit;
the speedup and listening result require the physical Pico 2.

`scripts/measure_audio_timing.ps1` automates the capture side. It optionally
uploads a build directory first (`-UploadDir`), records `[DIAG C1]` windows for
`-Seconds`, and prints median/mean/min/max `render_us`, peak `max_us` and the
`over`/`underruns`/`txstalls` increases, plus a machine-readable `RESULT` line.
`-RawLog` saves the verbatim serial lines; `-AnalyzeFile` re-summarizes a saved
log without a board. The full comparison is two commands:

```powershell
./scripts/measure_audio_timing.ps1 -UploadDir build/audio-ram-150 -Label ram -Seconds 60 -RawLog build/audio-ram-150/diag.log
./scripts/measure_audio_timing.ps1 -UploadDir build/audio-xip-150 -Label xip -Seconds 60 -RawLog build/audio-xip-150/diag.log
```

Keep the device state identical across both captures. An idle capture (transport
running, gates off) is already comparable — every enabled voice renders each
sample — but FM/waveguide presets with dense gates exercise the relocated code
hardest.

## Reading the serial heartbeat

The existing `[DIAG C1]` report now includes:

| Field | Meaning |
|---|---|
| `render_us` | Mean buffer render time in the last reporting window |
| `max_us` | Maximum buffer render time in that window |
| `budget_us=5333` | Whole-microsecond budget for 256 frames at 48 kHz |
| `over` | Cumulative renders exceeding that budget |
| `underruns` | Cumulative empty-queue events where DMA played a block of silence |
| `txstalls` | Cumulative observations of the PIO TX-stall flag |

Render timing excludes waiting for a free buffer and includes interrupts that
occur during rendering. It is elapsed render time, not an exact CPU utilization
percentage. Only two timer reads are added per buffer. Core 0 handles all serial
formatting; Core 1 sends a bounded snapshot without waiting.

`underruns` and `txstalls` reset when I2S is enabled. `over` counts since the render
loop started. PIO stall flags are sticky, so `txstalls` counts observations, not
individual lost clocks or samples. Only this driver's state-machine flag is
cleared. A PIO stall can occur even when prepared buffers are available if DMA
servicing is late. Conversely, substituting silence can keep PIO running while
still producing an audible discontinuity.

## Hardware check

Build with `scripts/build_pico2seq.ps1 -CpuMHz 150`, flash the resulting UF2, and
listen to one voice followed by all four. Exercise preset changes, fast gates,
slides, controls and OLED/LED updates. Check that `underruns` and `txstalls` stay
at zero and that typical rendering leaves room below 5333 microseconds. An
occasional `over` can be absorbed by queued audio; a rising underrun count proves
the output actually ran out of rendered buffers.

If crackling remains while both delivery counters stay zero, inspect the sample
waveform and gate/parameter transitions next. These optimizations and host tests
do not prove that the physical audio output is clean.
