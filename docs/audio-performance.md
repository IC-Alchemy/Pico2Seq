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

Pico builds place the buffer renderer, block mixer, voice span stages,
oscillator span loops and all ten recipe process callbacks in `.time_critical.*`.
`src/utils/AudioRam.h` wraps the Pico SDK's `__not_in_flash_func`; the existing
linker script places these sections in SRAM and startup copies their code from
the flash image. Envelope and filter kernels inline into their RAM callers.
Oscillator span loops use explicit RAM helpers so an out-of-line `std::visit`
dispatcher cannot move their sample processing into flash. Host builds use
ordinary function placement.

Class-template members that cannot be named at the definition (`rpdsp::DarkReverb::process`)
take the same placement from the `RPDSP_HOT_FUNCTION` hook defined in the same header; see the
[master reverb audit](#master-reverb-ram-stack-and-sram-audit) for why annotating their caller
was not enough.

This reduces instruction fetches competing with Core 0 for XIP cache space.
It does not make the entire audio core independent of flash: preset/configuration
handlers, recipe descriptors, other read-only data and board-core math/memory
helpers can still use XIP. In particular, Arduino's wrapped math routines in the
6.1.0 core are still linked in flash; moving a caller does not relocate them.
DSP calculations, control ownership and diagnostic timing boundaries are unchanged.

For a controlled hardware comparison, build both variants from the same source
at the same clock (the helper's default clock is 150 MHz):

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

## Block rendering and silent-voice skip

The block renderer starts from `DeCluttered` at `8b09f71`. That commit pins
`rpdsp` to `c8369de`, which lacks the coefficient APIs already used by its
recipe sources. Prerequisite commit `684d56c` restores the matching `25b3549`
revision. All comparisons use that repaired baseline, with 315 existing host
checks passing. No DSP implementation was changed for block rendering.

Core 1 mixes 256-frame blocks; each voice checks its queue between spans of
at most 32 samples. Queued controls still get one sample each. A concurrently
published update can wait up to 0.67 ms for the next span. Master/mix/delay/macro
targets are read per block; master gain, compressor macro and the delay's mix/time eases still
run every sample. Member/static scratch
keeps sample arrays off Core 1's 2 KiB stack.

Measurements use the supplied Unicorn 2.1.4 Cortex-M33 harness, pqt-gcc
5.0.0-9576866 (GCC 16.1), `-O3 -ffast-math`, softfp, and Arduino-Pico 6.1.0's
wrapped float math. These are executed instructions, not cycles or hardware
utilization. The scripted null test covers all 29 presets in eight groups,
214,272 mono samples per group, including slides, queued gates, preset swaps,
releases, and master-volume changes.

Stage 1 (block plumbing) is PCM16 bit-identical in every group. Its instruction
counts are 1,134.17/684.75 for default voices playing/released and
1,937.23/932.50 for heavy voices, versus baseline 1,187.96/732.05 and
1,990.52/979.80 respectively. The repaired recipe implementation makes the heavy
baseline slightly cheaper than the older plan's 2,029.52 figure.

Final stage results (instructions per output sample, four voices):

| Scenario | Baseline | Stage 2, skip off | Final block, skip on | Final reduction |
|---|---:|---:|---:|---:|
| Default playing (4, 2, 1, 6) | 1,187.96 | 744.86 | 711.93 | 40.1% |
| Heavy playing (13, 28, 3, 0) | 1,990.52 | 1,539.38 | 1,517.39 | 23.8% |
| Default released | 732.05 | 415.70 | 142.14 | 80.6% |
| Heavy released | 979.80 | 612.04 | 123.51 | 87.4% |

Both playing sets exceed the required 15% reduction and both released sets
exceed 50%. Default playing also exceeds the 30% target; heavy playing does
not. Optional engine rewriting was omitted: the remaining heavy cost is
mostly existing DSP kernels rather than engine dispatch.

| Preset group | Skip off: differing samples / null dB | Skip on: differing samples / null dB |
|---|---:|---:|
| 0, 1, 2, 3 | 12 / -111.0 | 23 / -108.2 |
| 4, 5, 6, 7 | 0 / exact | 2 / -102.6 |
| 8, 9, 10, 11 | 0 / exact | 10 / -111.5 |
| 12, 13, 14, 15 | 0 / exact | 26 / -107.4 |
| 16, 17, 18, 19 | 0 / exact | 44 / -109.2 |
| 20, 21, 22, 23 | 0 / exact | 18 / -112.4 |
| 24, 25, 26, 27 | 0 / exact | 17 / -114.4 |
| 28, 0, 1, 2 | 16 / -110.8 | 42 / -106.6 |

Every nonzero PCM difference is exactly 1 LSB. Null dB is residual RMS relative
to reference RMS, using the original scripted comparison. All six non-ladder
groups are bit-identical with skip off. An instrumented float capture reproduced
the same PCM mismatches in the two ladder groups: maximum float differences
were `8.9407e-8` and `5.9605e-8`, with float nulls of -144.16 and -142.59 dB.

Rounding fixes preserve the original oscillator dispatch boundary using GCC's
`__builtin_assoc_barrier` (a volatile float fallback on other compilers), and
process each stored oscillator directly. Isolated probes showed that copied
B-spline state changed fast-math contraction inside the oscillator loop.
Reverting envelope/SVF/HPF copies did not fix those mismatches; restoring the
scalar source loop did. The final oscillator loop retains one dispatch per
span and restores exact non-ladder PCM. The ladder's block API and a local-copy
loop of scalar `process()` both retained small contraction differences; the
final code uses the scalar local-copy loop and meets the plan's ladder allowance.

Final placement inspection found the compiler had emitted oscillator span loops
inside a flash-resident `std::visit` helper. Explicit per-waveform RAM helpers
keep those loops in `.time_critical.*`; this also reduced emulator call/dispatch
overhead. Their PCM results are identical to the preceding silent-skip stage.
The 640-byte maximum stack observation covers 32 scripted render calls in that
preceding stage, including triggers, slides and structural swaps; it excludes
setup/control publication, interrupts and the firmware's outer audio loop.

Host validation passes 329 CTest entries, including all existing suites and new
coverage for every preset, irregular blocks, queue probes, every oscillator
waveform, queued gate edges, disabled voices, master ramps, zero/oversized
blocks, and released-voice wakeup. Documentation links also pass.

The final firmware build passed at **225 MHz**, with audio code in SRAM:

```powershell
./scripts/build_pico2seq.ps1 -CpuMHz 225 -BuildDirectory build/fw-block-final
```

Artifacts are `build/fw-block-final/Pico2Seq.ino.{uf2,elf,bin,map}`. The compiler
reports 307,844 bytes of program storage and 110,404 bytes of global RAM.
The final ELF places the buffer renderer, block mixer, span/filter/skip helpers,
and all eight concrete oscillator span loops at SRAM execution addresses.
The UF2 contains code through `5d5dcd0`; subsequent documentation commits do not
change its firmware sources. Build warnings came from bundled libraries and
existing unused parameters; compilation and linking exited successfully.

Hardware A/B, upload and listening were omitted at the user's request. The
software checks were accepted and a 225 MHz firmware build requested instead.
Emulator and host results do not establish physical audio behavior.

## Master reverb RAM, stack and SRAM audit

Every number below was read from a linked ARM ELF, an ARM `sizeof` probe or a
`-fstack-usage` build made on 2026-09-30 with the toolchain below. Each figure is
labelled **measured** (read from an artifact), **counted** (arithmetic on measured sizes
and the source) or **estimate**. Nothing here is a board measurement; the checks that
need the board are listed at the end.

### Builds

arduino-cli 1.5.2-rc.1, `rp2040:rp2040` core 6.1.0, arm-none-eabi-gcc 16.1.0
(`pqt-gcc 5.0.0-9576866`, `-mcpu=cortex-m33 -mfloat-abi=softfp -std=gnu++23`), **225 MHz**,
`-O3 -ffast-math`, audio code in SRAM. Board options:
`flash=4194304_65536,arch=arm,freq=225,opt=Optimize3,profile=Disabled,rtti=Disabled,stackprotect=Disabled,exceptions=Disabled,dbgport=Disabled,dbglvl=None,usbstack=tinyusb,ipbtstack=ipv4only,uploadmethod=default`.

| Variant | Extra compiler flags | Purpose |
|---|---|---|
| Baseline | none (commit `93bb7a1` plus the two build fixes below) | Pre-reverb reference |
| **Half (default)** | none | The shipping build |
| Float | `-DPICO2SEQ_REVERB_STORAGE_HALF=0` | The plan's first choice, for comparison |
| Bypass | `-DPICO2SEQ_REVERB_BYPASS=1` | Bench only: no tank, so the same bus without reverb CPU |

```powershell
./scripts/build_pico2seq.ps1 -CpuMHz 225 -BuildDirectory build/reverb-half
./scripts/build_pico2seq.ps1 -CpuMHz 225 -ExtraFlags '-DPICO2SEQ_REVERB_STORAGE_HALF=0' -BuildDirectory build/reverb-float
./scripts/build_pico2seq.ps1 -CpuMHz 225 -ExtraFlags '-DPICO2SEQ_REVERB_BYPASS=1' -BuildDirectory build/reverb-bypass
```

The audit builds used the equivalent `arduino-cli compile --fqbn rp2040:rp2040:rpipico2
--board-options <above> --warnings all --build-property "build.extra_flags=-ffast-math
-DPICO2SEQ_AUDIO_IN_RAM=1 <variant flags>"` on a staged copy of the sketch made the way
`build_pico2seq.ps1` stages it (PowerShell was not available where the audit ran, so the
script's new `-ExtraFlags` parameter is untested). Two problems already present at `93bb7a1`
stopped the Arduino build before the audit could start and were fixed first: the sketch
compiles every `.cpp` under `src/`, which picked up rpdsp's host-only test programs (now
guarded with `#ifndef ARDUINO` in rpdsp), and `Application.cpp` used `g_errorState` without
declaring it.

### Static RAM (measured, `arm-none-eabi-size -A` on each ELF)

| Bytes | Baseline | **Half** | Float | Bypass |
|---|---:|---:|---:|---:|
| `.text` (flash) | 326,268 | 336,236 | 336,236 | 336,236 |
| `.rodata` (flash) | 55,576 | 56,272 | 56,272 | 56,272 |
| `.data` (SRAM: initialized data **and RAM-placed code**) | 46,240 | 54,456 | 53,816 | 46,784 |
| `.bss` | 55,568 | 56,696 | 56,696 | 56,696 |
| `.uninitialized_data` (retained session store) | 12,460 | 12,508 | 12,508 | 12,508 |
| **`.heap` (linked heap capacity)** | **409,744** | **400,352** | **400,992** | 408,024 |
| `.bin` | 440,404 | 459,284 | 458,644 | 451,612 |

The heap is whatever the 512 KiB main RAM has left after `.data`, `.bss` and the uninitialized
data: the SDK's `_sbrk` refuses to grow past `__StackLimit`, and `rp2040.getTotalHeap()`
reports the same span. Every byte the reverb adds to `.data`/`.bss` therefore shrinks the
heap one for one (Half: 8,216 + 1,128 + 48 = 9,392 = 409,744 − 400,352). The `.bss` growth is
the second channel buffer in `AudioEngine.cpp` (`leftBuffer` and `rightBuffer` replace one
`mixBuffer`: +1,024 B), the two 12.4 KB session snapshot buffers growing by 48 B each, and a
few flag bytes; the 48 bytes of `.uninitialized_data` are the effect record in the
retained-RAM store.

### Setup-time heap accounting

`VoiceManager` (which owns `MasterDelay` and `MasterReverb` by value), the four `Voice`s and
the audio buffer pool are the large heap allocations made during setup.

| Allocation | Baseline | **Half** | Float | Source of the number |
|---|---:|---:|---:|---|
| `VoiceManager` (contains `MasterDelay` 209,700 B) | 210,896 | 244,268 | 277,036 | **measured**: ARM `sizeof` (`MasterReverb` 33,372 B Half, 66,140 B Float) |
| `voices.reserve(4)` | 16 | 16 | 16 | **counted** (4 pointers) |
| 4 × `Voice` | 121,312 | 121,312 | 121,312 | **measured**: ARM `sizeof` = 30,328 each |
| 4 × `ManagedVoice` | 48 | 48 | 48 | **counted** (12 B each) |
| Audio pool: 2 pool structs (32 B), 4-buffer array (96 B), 4 × (12 B header + 1,024 B PCM16 stereo) | 4,304 | 4,304 | 4,304 | **counted** from `audio.cpp`/`buffer.h` with measured struct sizes |
| **Accounted payload** | **336,576** | **369,948** | **402,716** | |
| Linked heap capacity (above) | 409,744 | 400,352 | 400,992 | **measured** |
| **Left after the accounted payload** | **73,168** | **30,404** | **−1,724** | **counted** |

Allocator overhead adds roughly 8 bytes to each of the 21 allocations (**estimate**, about
0.2 KB; the newlib-nano internals were not measured). The table leaves out everything else
that allocates during setup or running — LittleFS mount buffers, the OLED frame buffer,
FastLED and TinyUSB state, `std::string` temporaries — because their sizes were not
measured; they can only reduce the figures in the last row.

**Result.** With Float the accounted allocations alone exceed the linked heap by 1.7 KB, so
that build cannot be expected to finish setup (an allocation, most likely the audio pool
created last, would fail). That shortfall does not depend on the unmeasured allocations: they
can only make it larger. This is the plan's fallback case, "If Float fails the RAM gate,
retain Half at the same capacity", so **Half is the default**
(`PICO2SEQ_REVERB_STORAGE_HALF`, `src/voice/MasterReverb.h`). Half leaves about 30 KB
before the unmeasured allocations (the baseline left about 73 KB); whether that is enough
is confirmed only by the running system's `[DIAG MEM]` line (below). The reverb's total RAM
cost with Half is 33,372 B (heap) + 9,392 B (static, of which 8,216 B is RAM-placed code) =
42,764 B. Float becomes possible only after RAM is recovered elsewhere; `next-steps.md` item 6
describes about 47 KiB reserved but unused by `MasterDelay`. That change would alter the
delay's coupled constants and is not part of this work.

### Hot-code placement (measured, `nm -S` on the Half ELF; addresses `0x2000_0000..` are SRAM)

| Function | Address | Region | Bytes |
|---|---|---|---:|
| `AudioEngine::renderNextBuffer` | `0x200032cc` | SRAM | 372 |
| `fill_audio_buffer` (with `interleavePcm16` inlined) | `0x200031f0` | SRAM | 220 |
| `VoiceManager::processStereoBlock` | `0x2000ddd0` | SRAM | 10 |
| `VoiceManager::renderBus_` (delay, linked compressor, gain/macro inlined) | `0x2000d0d8` | SRAM | 3,320 |
| `MasterReverb::render` | `0x2000530c` | SRAM | 202 |
| `MasterReverb::blend_` | `0x2000524c` | SRAM | 192 |
| `MasterReverb::applyTargets_` | `0x20004ad8` | SRAM | 1,908 |
| `MasterReverb::readTargets_` | `0x20004a7c` | SRAM | 92 |
| `DarkReverb<16384, Half>::process` (block) | `0x20003e40` | SRAM | 3,132 |
| `DarkReverb<16384, Half>::process` (per sample) | `0x20003570` | SRAM | 2,256 |

The first firmware build of this work had `render`, `blend_` and `applyTargets_` in SRAM but
**both `DarkReverb::process` overloads in flash** (`0x1001ed34`, `0x1001e464`; 3,132 and
2,256 bytes, reached from `render` through an SRAM veneer), and `readTargets_` in flash too.
Annotating the wrapper does not move an out-of-line template callee, exactly the case the
plan warned about. The fix is rpdsp's `RPDSP_HOT_FUNCTION` placement hook (empty by
default; `src/utils/AudioRam.h` defines it as the `.time_critical.rpdsp` section for RAM
builds, and `MasterReverb.h` includes that file before any rpdsp header). It costs exactly the
5,388 bytes of the two functions in SRAM. One measured alternative was rejected:
`__attribute__((flatten))` on `render` inlines the per-sample path twice and grows `render`
to 9,804 bytes. In the Float ELF the same two functions are 2,798 + 1,952 = 4,750 bytes
(12% smaller: no half-precision conversions), also in SRAM.

Calls from the RAM path that still execute from flash (measured from the disassembly, each
through an SRAM veneer): the Arduino core's wrapped `sinf`/`expf`/`logf` (called by
`applyTargets_` only while a control is easing, at most once per 64-frame tick, all leaf
functions) and `log10f`/`expf`/`memset`/`applyMasterCompSettings_` from `renderBus_`, which the
pre-reverb `processBlock` called identically. `MasterDelay::process` and
`Compressor::processStereo` have no symbol of their own: they are inlined into `renderBus_`.
The stereo bus cost `renderBus_` 228 more bytes than the old `processBlock` (3,320 vs 3,092).

`MasterReverb.cpp` fails to compile in a RAM build if `RPDSP_HOT_FUNCTION` is undefined, but it
cannot see an rpdsp header parsed before `AudioRam.h`, so repeat the ELF check after touching
the audio includes:

```powershell
arm-none-eabi-nm -S -C build/reverb-half/Pico2Seq.ino.elf | Select-String 'DarkReverb|MasterReverb::(render|blend_|applyTargets_|readTargets_)|renderBus_'
```

### Core 1 stack (2,048 bytes, measured frames, estimated chain)

Core 1 runs on the linker's `__StackOneBottom..__StackOneTop`, 2,048 bytes; the sketch does
not enable `core1_separate_stack`, and the build has no FreeRTOS (**measured**: symbols and
`.stack1_dummy`). Frame sizes come from a `-fstack-usage` build whose code is identical to the
Half build (`.text`, `.data`, `.bss` byte-equal):

| Function | Frame (bytes, **measured**) |
|---|---:|
| `AudioEngine::renderNextBuffer` | 40 |
| `fill_audio_buffer` | 32 |
| `VoiceManager::renderBus_` | 200 |
| `MasterReverb::render` | 64 |
| `MasterReverb::applyTargets_` (then leaf libm) | 112 |
| `DarkReverb::process` (block: state and coefficient copies, 128 B of chunk arrays) | 448 |
| `DarkReverb::process` (per sample, called for an odd head or tail) | 112 |
| `Voice::processBlock` → `renderSpan_` → `runMainFilter_` | 40 → 136 → 208 |

**Estimate.** The deepest direct chain is the reverb block: 40 + 32 + 200 + 64 + 448 + 112 =
**896 bytes**, against about 656 bytes for the deepest direct voice chain (40 + 32 + 200 + 40 +
136 + 208). The earlier 640-byte emulator observation covered voice rendering only and
excluded the outer audio loop, so it is not comparable with either number. The reverb and
voice chains are siblings under `renderBus_`, so they do not add. A DMA interrupt while rendering adds the hardware exception
frame (32 bytes, plus 72 when the FPU context is stacked lazily) and the handler chain
(`audio_i2s_dma_irq_handler` and its callees, frames of 8 to 48 bytes each: roughly 100 more), so about **1.1 KB of
2 KiB** in the worst case. The estimate leaves out indirect calls (recipe function pointers,
`std::visit` dispatch) and the interrupt handler's exact depth. The reverb block's frame is
the number to watch if the engine or its chunk size changes.

Core 0's new frames are `displayReverbPage` 112, `formatReverbValue` 40, `handleReverbPage` 40,
`ReverbEditor::toggleFreeze` 48 and `setFromFader` 16 bytes, all smaller than existing
siblings (`displayArpPage` 504, `displayEnvelopePage` 168), so the Reverb page does not
create a new deepest Core 0 chain (**measured** frames; the `snprintf` internals below them
are the same as the existing formatters').

### Assumptions behind these conclusions

| Assumption | Status |
|---|---|
| The heap capacity is `__StackLimit − __bss_end__`, the `.heap` section | **Confirmed by evidence**: the SDK's `_sbrk` limits growth to `__StackLimit`; `rp2040.getTotalHeap()` uses the same symbols; both equal the `.heap` size in each ELF |
| The listed objects come from that heap, `VoiceManager` and the voices before `voicesReady`, the audio pool afterwards on Core 1 | **Confirmed by evidence**: `VoiceSetup.cpp`, `VoiceManager.cpp`, `audio.cpp`, `buffer.h`, `AudioEngine.cpp` |
| The `sizeof` values are what the firmware compiler produces | **Confirmed by evidence**: an ARM probe built with the firmware's flags |
| Allocator overhead is about 8 bytes per allocation | **Unconfirmed** (newlib-nano internals not measured); worth about 0.2 KB either way |
| No other setup-time allocation is larger than a few KB | **Unconfirmed**. Half's 30 KB is therefore an upper bound. Float's shortfall does not depend on this: further allocations only enlarge it |
| A Float build fails during setup | **Unconfirmed** on hardware; only the arithmetic shortfall (−1,724 B before overhead) is confirmed |
| Half's tank uses the M33's hardware half-precision conversion | **Confirmed by evidence**: 40 `vcvtb` instructions in each Half `process` overload |
| `-fstack-usage` does not change the generated code | **Confirmed by evidence**: `.text`, `.data` and `.bss` are byte-equal between the two Half builds |
| The stack estimate is the sum of direct-call frames | **Unconfirmed** until a board reports a high-water mark: it omits indirect calls and the exact interrupt depth |
| Half's CPU cost fits the plan's 20 % headroom target | **Unconfirmed**: not measured, and the plan says to compare bypass, Half and Float on the board |

### Board checks still to do

Nothing above measured CPU time, the running heap, a real stack high-water mark, XIP cache
behavior or sound. The firmware prints what those checks need, every two seconds, on Core 0's
serial port (`[DIAG MEM]`, below). Build the three variants above, then for each
(identical presets, dense gates/slides, synced and unsynced delay, reverb Mix at 0 and at
100 %, faders moving on the Reverb page, freeze on and off, a 1000 s decay and a silent
long tail):

```powershell
./scripts/measure_audio_timing.ps1 -UploadDir build/reverb-bypass -Label bypass -Seconds 60 -RawLog build/reverb-bypass/diag.log
./scripts/measure_audio_timing.ps1 -UploadDir build/reverb-half   -Label half   -Seconds 60 -RawLog build/reverb-half/diag.log
Select-String '\[DIAG MEM\]' build/reverb-half/diag.log | Select-Object -Last 3
```

The plan's timing gate is **at least 20 % worst-case render headroom (`max_us` at or below
about 4,267 µs of the 5,333 µs budget)** with no growth in `underruns`, `txstalls` or
sustained `over`. Bypass minus Half is the reverb's CPU cost at the same clock; mix zero does
not remove it (the tank always runs). The plan sets no memory threshold. Proposed, not
measured: `heapFloor` of at least 8 KB and a Core 1 `stack1` free of at least 512 bytes;
`heapFloor` far below the estimate in the accounting table (about 30 KB) means something
allocates that this audit did not count. The Float build is expected to fail during setup
(unconfirmed; a run would only confirm it).

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

A separate Core 0 line reports memory headroom (every two seconds, next to the
`[DIAG C0]` line, so the timing parser above is unaffected):

```text
[DIAG MEM] reverb=half16384 heapTotal=400352 heapUsed=... heapFree=... heapFloor=... stack0=1234/2048 stack1=1500/2048 (free/total)
```

| Field | Meaning |
|---|---|
| `reverb` | Compiled variant: `half16384`, `float16384` or `bypass` (labels a capture with its build) |
| `heapTotal` | Linked heap capacity (`__StackLimit - __bss_end__`) |
| `heapUsed` / `heapFree` | Allocated now (`mallinfo().uordblks`) and the difference; fragmentation is not subtracted |
| `heapFloor` | `heapTotal` minus the heap ever taken from the system (the arena only grows), a conservative floor for the free heap at its lowest point |
| `stack0` / `stack1` | Bytes of each core's stack never reached since it was painted with a pattern at the core's entry point, over its total size. `-1` means not painted yet. It includes the entry depth and a small margin, so real use is slightly overstated |

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
