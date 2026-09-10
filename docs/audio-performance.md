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
