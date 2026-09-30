# Testing Embedded C++ on a Host Machine

## Overview

Pico2Seq firmware targets the Raspberry Pi Pico 2 (RP2350 microcontroller). Because microcontrollers execute bare-metal firmware without an underlying OS, executing device binaries natively on host development machines (Linux, macOS, Windows) is impossible without hardware emulation.

To enable rapid, automated regression testing, Pico2Seq employs a **host-side unit testing architecture**:
1. **Core Decoupling:** Pure mathematical algorithms, musical scales, sequencing state machines, DSP filters/oscillators, and UI decision logic are decoupled from hardware peripherals.
2. **Hardware Header Stubs (`tests/stubs/`):** Minimal lightweight mock headers shadow microcontroller-specific APIs (`Arduino.h`, `Wire.h`, `pico/sync.h`, `hardware/gpio.h`).
3. **Catch2 Test Framework:** Tests are written in modern C++17 using Catch2 v3.5.2 and executed locally via CMake or directly against the compiled test binary.

---

## Testing Strategy & Module Classification

`pico2seq_tests '[voice_envelope]'` covers the live ADSR page's entry/exit
gesture, release suppression, movement pickup, all-step scope, arp publication,
voice isolation, repurposed timbre lanes, and audible sustain/release changes
without retriggering. OLED/LED layout and tile timing still need a hardware check.

Application glue lives in `src/app/`; see the
[firmware structure guide](firmware-structure.md). `test_app_runtime.cpp`
checks every PCM16 level, clipping/truncation and hand-distance recording
calibration (`[app]`). The Arduino build checks the hardware-bound app `.cpp`
files; host CMake does not compile that startup/I2S/control glue.

`test_lidar_recording.cpp` compiles the real `StepPlayback.cpp` recording and
publication path. Run `pico2seq_tests.exe '[lidar]'` to check held/latched Filter
and Release buttons, calibrated distance, independent lane lengths, selected-step
edits while running/stopped, voice isolation, patch reset, encoder targets, and
Digital/Square rendered filter contours and release tails. These are host logic
and DSP checks; physical lidar, OLED and listening still need a hardware check.

```
                     PICO2SEQ CODEBASE
                             |
             +---------------+---------------+
             |                               |
             v                               v
    [ Pure Logic / DSP ]            [ Hardware Drivers ]
    ├── rpdsp DSP algorithms        ├── src/audio/ (PIO, DMA, I2S)
    ├── scales & lookup tables      ├── src/LEDMatrix/ (FastLED WS2812B)
    ├── pico2seq-core sequencer     ├── src/OLED/ (SH1106G I2C)
    ├── voice synthesis & presets   ├── src/midi/ (TinyUSB stack)
    ├── ControlSurfaceLogic         └── src/sensors/ (TMAG/VL53 drivers)
    └── AlchemyProto wire format
             |                               |
             v                               v
    [ Host Unit Tests ]             [ Manual / Bench Testing ]
    (tests/stubs/ + Catch2 v3)      (Tested on Pico2 Hardware)
```

### Module Testability Tiers

| Tier | Subsystem | Files | Testing Approach |
|---|---|---|---|
| **Tier 1: Zero Deps** | DSP & Sound Synthesis | `src/rpdsp/`, `src/voice/VoiceOscillator.h` | Pure math, `<cmath>`, `<variant>`, `<array>`. Tested natively. |
| **Tier 1: Zero Deps** | Sequencer Core Templates | `src/pico2seq-core/sequencer/SequencerDefs.h` | Template data structures (`ParameterTrack<N>`). Tested natively. |
| **Tier 1: Zero Deps** | UI Control Surface Logic | `src/ui/ControlSurfaceLogic.h/.cpp` | Pure state machines (`ModeStabilizer`, `PadBank`, `ShiftLatch`, `FaderMap`). Tested natively. |
| **Tier 1: Zero Deps** | Alchemy Tile Wire Format | `src/AlchemyUI/src/{AlchemyProto,TileButton}.h` | Pure C++ register/frame decoding — no Arduino, no Wire. Tested natively. |
| **Tier 2: Light Stubs** | Musical Scales | `src/pico2seq-core/scales/scales.cpp` | Requires minimal `Arduino.h` type aliases (`uint8_t`, `String`). |
| **Tier 2: Light Stubs** | Sequencer Logic | `src/pico2seq-core/sequencer/{Sequencer,ParameterManager}.cpp` | Requires `Arduino.h` and `pico/sync.h` spinlock stubs. |
| **Tier 2: Light Stubs** | Voice & Presets | `src/voice/{Voice,VoicePresets}.cpp` | Requires staged parameter and scale table injection. |
| **Tier 3: Hardware-Bound** | I2S, LED, OLED, Sensors | `src/audio/`, `src/LEDMatrix/`, `src/OLED/`, `src/sensors/` | Hardware-dependent glue. Kept thin; validated on physical hardware. |

---

## Focused UI transition checks

`UIState` derives settings-page queries from `settingsMode/currentSubMode` and
stores parameter-change feedback separately. `src/ui/UITransitions.h` contains
pure state transitions; hardware handlers own MIDI cleanup and tile edge history.
`tests/unit/test_ui_transitions.cpp` covers page reopening, feedback expiration
(including timer wrap), slide cleanup, and tile-selection versus pad-focus rules.

The focused target includes these tests and the existing ControlSurfaceLogic suite:

```bash
cmake --build build_test --target pico2seq_ui_tests --parallel
./build_test/tests/pico2seq_ui_tests
```

Use a separate build directory when changing generators or compilers. On Windows,
with x64 Clang and the Visual Studio SDK installed (not the ARM `g++` toolchain):

```bash
cmake -S . -B build_clang -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug
cmake --build build_clang --target pico2seq_ui_tests --parallel
./build_clang/tests/pico2seq_ui_tests.exe
```

Bench regression checklist (host tests cannot verify the physical tile/LED/OLED glue):
- Open settings after leaving its parameter page: presets appear and pad taps apply presets.
- Select each voice through tiles and pad holds: tiles exit step editing; pad holds keep
  the parameter target and focus the held step without ending sounding notes.
- Enter slide with a Shift-latched parameter, pending pad hold, or voice length hold:
  old holds/latches cannot reappear on release. Both slide toggles leave step editing clear.
- Press/release parameter tiles while sliding, then leave slide: no stale latch returns.
- Hold each voice button for 400 ms in both panel modes: the existing OLED length gauge
  and blinking band should identify that voice. Tap its lit bank to set 2–16 steps;
  the partner bank must do nothing. Release the voice and pads in either order:
  no gate toggles or step selection should leak through. Short voice taps and Shift
  chords retain their actions; holding Utility button 6 must not open length entry.
  Host coverage for timing, ownership, and modal cancellation is tagged `[gate_length]`
  in `test_ui_transitions.cpp`; physical controls and display behavior need hardware checks.
- Editor voice selection keeps per-voice cursors; session restore retains the saved voice
  without invoking live tile-selection note cleanup.

## Master delay and compressor integration

`test_master_bus.cpp` checks the real `VoiceManager` against standalone
delay and compressor processing in the order: voices, delay, master gain,
compressor. It covers dry bypass, all three compressor anchors, block sizes
through 513 samples, tails after voices go silent, transport mute, zero
master volume, the 10–750 ms fader range, and synced time following BPM at 48 kHz. The block suite also
compares scalar and block rendering while delay mix/time change.

`test_master_delay.cpp` retains fractional timing, repeat darkening, bounded
feedback and mix smoothing checks, plus a 45 BPM whole-note echo and safe
mode switches. `test_master_compressor.cpp` checks that a four-voice mix
changes the PCM16 samples delivered to I2S. `test_control_surface_logic.cpp` checks
faders 1–3 re-arm on Shift edges while gate length stays engaged,
the 19 note divisions and retains the step-envelope assignments and compressor macro gesture.

```powershell
cmake --build build_test --parallel
& ./build_test/tests/pico2seq_tests.exe '[master],[master_delay],[control_surface]'
ctest --test-dir build_test -C Release --output-on-failure
```

On hardware, check fader 1 tempo/feedback, fader 2 mix/time in both ms and sync modes,
Shift + Utility Delay/Session toggling the mode, and fader 3 volume/macro independently,
their OLED notices, both Shift edges, and all four ENV sliders. Listen to
delay tails with different compressor settings; inspect underrun counters
while all four voices play. Host tests and firmware compilation do not
establish board timing, physical controls, or listening acceptance.

Integration validation on 2026-09-21 started from `DeCluttered` at `df6b50d`
and merged `delay` at `70f2d86`. Clang Release CTest passed 484/504 checks;
all 20 failures also occurred on untouched `df6b50d` (462/482), with identical
failure names. The focused effect/control run passed 67 cases. RP2350
firmware compiled at 150 MHz with audio code in SRAM and all four artifacts
(UF2, ELF, BIN, MAP) verified. It was not flashed or tested on hardware.

The subsequent Shift + tempo-fader feedback change passed 68 focused cases
and 486/506 full CTest checks, with the same 20 baseline failures. Live
feedback changes are checked through the real combined bus at 0%, 100% and
35%, and the saturated-delay test now exercises a full 1.0 feedback coefficient.
The 150 MHz firmware was rebuilt with all four artifacts verified; physical
fader/OLED behavior and audio timing remain unverified.

## Master reverb, stereo bus and Reverb page

The signal path is voices → `MasterDelay` → `MasterReverb` (`rpdsp::DarkReverb`) → shared master gain →
linked stereo compressor → separate left/right PCM16. The reverb tank keeps running at mix zero, and a
settled mix of zero must reproduce the legacy mono bus bit-for-bit.

| Tag | File | What it pins |
|---|---|---|
| `[reverb][master_reverb]` | `test_master_reverb.cpp` | Mix zero is the dry bus exactly while the tank keeps evolving; the wet path is the engine driven by the mono bus on both inputs at the intended level; one shared eased mix; lock-free control targets and clamping; coherent snapshots, later single edits beating an earlier snapshot (even a return to the old value) and a full snapshot ring; eased coefficients that land exactly and then do no setter work; **any split of the render into calls gives the same output**; extreme controls stay finite and bounded; `prepare()` keeps published targets |
| `[freeze]` | same | Freeze holds the tail, its transitions do not click (measured per control tick against the tail's own floor; a plain-engine freeze and a damping-only ramp both fail this check), and a rapid toggle never engages the engine |
| `[storage]` | same | Half and Float agree at the same capacity (residual below −55 dB, equal peaks within 0.1 dB); the compiled variant names itself for `[DIAG MEM]` |
| `[alloc]` | same | A global `operator new` replacement counts allocations while rendering, changing controls and publishing snapshots: zero |
| `[reverb_bus]` | `test_master_bus.cpp` | Bus order (voices, delay, reverb, shared gain, linked compressor) with a wrong-order twin that must differ; reverb mix zero equals the legacy mono bus on both channels including PCM; distinct wet channels sharing one compressor gain; delay repeats reach the reverb; tails survive silent voices and obey transport mute and volume (against an unmuted twin); odd, empty, oversized and overlapping blocks; `init()` clears the tail but keeps published targets |
| `[app][pcm][stereo]` | `test_app_runtime.cpp` | Left and right convert and clip independently |
| `[reverb_page]` | `test_reverb_page.cpp`, `test_reverb_editor.cpp` | Entry gesture (Shift, then 6, then a press of 2) and only that, coexistence with the ADSR chord, presses-only actions, no stale presses after another screen, gate-length holds blocked, fader layers, exact curve limits (monotonic and invertible), OLED formatting, and that a fader move reaches the sound without a click |
| `[persistence][effects]` | `test_persistence.cpp` | Format 3: locked sizes and offsets, v1/v2 upgrade with default effects (mix 0), bit-exact v3 round trip, NaN/infinity/out-of-range rejection for every field, damaged and unknown frames refused, codec never stores freeze, limits equal `ReverbParams` |
| `[stack]` | `test_stack_watermark.cpp` | The paint/scan arithmetic behind `[DIAG MEM]`'s stack headroom |
| `reverb-bypass:` | `test_reverb_bypass.cpp` (`pico2seq_reverb_bypass_tests`) | The bench-only `-DPICO2SEQ_REVERB_BYPASS=1` build renders the dry bus and still hands controls over |

```bash
./build_test/tests/pico2seq_tests "[reverb]"          # adapter, freeze, storage, allocation
./build_test/tests/pico2seq_tests "[reverb_bus]"      # stereo master bus
./build_test/tests/pico2seq_tests "[reverb_page]"     # Reverb page and editor
./build_test/tests/pico2seq_tests "[persistence]"     # includes format 3
./build_test/tests/pico2seq_audio_tests               # I2S pool and driver (builds again on GCC 13)
```

**Storage variants.** The host tests default to the firmware's Half tank. Re-run the identical suites
against the 24-bit tank with a second build directory (the option is applied to every target, so
`MasterReverb`, which `VoiceManager` embeds, has one definition per executable):

```bash
cmake -B build_test_float -DCMAKE_BUILD_TYPE=Debug -DPICO2SEQ_REVERB_STORAGE=FLOAT
cmake --build build_test_float --parallel
ctest --test-dir build_test_float --output-on-failure
```

**Known baseline failures.** Recorded 2026-09-30 on Linux with GCC 13.3, Debug: at `93bb7a1` 33 tests fail in
`pico2seq_tests`/`pico2seq_voice_tests`. Their assertions concern octave/gate defaults, note names, pitch lookup,
release defaults, cutoff limits and filter counts; whether the code or the expectation is stale was not
investigated. `pico2seq_audio_tests` also did not compile: the host test builds `audio_i2s.c` as C++, which requires
designated initializers in declaration order. The reverb work leaves the 33 failing test names and their
assertion text unchanged (compare the Catch2 XML reporter output, not the count) and fixes the audio target
by reordering the three initializers. Treat a failure outside that set as new.

**Normal versus fast-math.** The firmware is built with `-O3 -ffast-math`, so the same suites were also run that way:

```bash
cmake -B build_test_fast -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS_RELEASE=-O3 -ffast-math"
cmake --build build_test_fast --parallel
ctest --test-dir build_test_fast --output-on-failure
```

Recorded 2026-09-30 (GCC 13.3, x86-64): the baseline fails 44 tests this way and the reverb branch fails the same 44
(by name): the 33 above plus 11 that only fail under fast-math, all in older tests (Arpeggiator dynamics/gate/pattern
buttons, `EncoderMotion` non-finite sizes, oscillator spans, `sinNormalizedPhase`). Every new test passes in both modes.
Tolerances: comparisons are bit-exact wherever the code copies or runs the same path twice (mix zero equals the dry
bus, persistence round trips). The two split-invariance checks (a render cut into different
calls: `test_master_reverb.cpp`, and the whole bus in `test_master_bus.cpp`) are bit-exact in IEEE builds and use
**2e-4 relative** under `__FAST_MATH__`, the slack rpdsp's own reverb test allows, because a compiler may contract or
reassociate the block and per-sample loops differently; on GCC 13.3 they were bit-exact in fast-math too. The ARM
compiler's fused multiply-add contraction is not exercised on the host. rpdsp's linked-compressor test uses 1e-5
relative under fast-math. Finiteness is always tested on the bit pattern, because `-ffinite-math-only` lets a compiler
fold `std::isfinite`; one persistence check that used `nextafter` below a zero limit (a denormal that fast-math flushes
to zero) was changed to avoid it.

**What host tests do not establish:** board CPU time, XIP/SRAM behavior at run time, heap and stack headroom on the
device, DMA timing and listening. Those are the board checks in
[audio-performance.md](audio-performance.md#master-reverb-ram-stack-and-sram-audit).

## Host Unit Test Suites

The host test executable (`pico2seq_tests`) links all unit suites under `tests/unit/`:

| # | Test Suite File | Tested Components | Key Test Areas |
|---|---|---|---|
| 1 | `tests/unit/test_helpers.cpp` | Global Test Helper Symbols | Provides single definition of extern symbols (`slideMode`, `currentScale`) |
| 2 | `tests/unit/test_rpdsp_additions.cpp` | `rpdsp` DSP Extensions | `dspmap::fmap` curves (local carry-over), Waveshaper transfer functions, DSPFunctions |
| 3 | `tests/unit/test_dsp_recipe_regressions.cpp` | `rpdsp` Recipe Regressions | ADSR envelope curves and retriggers, compressor across sample rates, vowel/tape/frequency-shifter/buffer recipes (`[recipe_regression]`) |
| 4 | `tests/unit/test_scales.cpp` | Musical Scale Lookup Tables | 13 scales monotonic ordering, root notes at 0, MIDI boundary validation, chromatic fallback |
| 5 | `tests/unit/test_sequencer.cpp` | Core Step Sequencer | `ParameterTrack<N>` wrapping, `NoteDurationTracker` countdowns, start/stop, gate toggling |
| 6 | `tests/unit/test_voice.cpp` | Synthesizer Voice Engine | Voice state transitions, staged parameter application on `process()`, scale injection, filter sweep, preset registry (29 named presets, engine selection, finite bounded audio per preset), waveguide / noise-FX engine behavior |
| 7 | `tests/unit/test_voice_transfer.cpp` | `Voice` control→audio handoff | `SpscQueue` FIFO ordering, no torn multiword payloads under concurrent transfers, queued gate edges reach samples (`[voice_transfer]`) |
| 8 | `tests/unit/test_voiceoscillator.cpp` | Voice Oscillator Dispatch | `VoiceOscillator` variant dispatch, band-limited waveforms, pulse width modulation, pitch changes |
| 9 | `tests/unit/test_control_surface_logic.cpp` | Tile UI Decision Logic | `ModeStabilizer` debouncing, `PadBank` voice-pair resolution, `ShiftLatch` latching, `FaderMap` deadband |
| 10 | `tests/unit/test_alchemy_proto.cpp` | Alchemy Tile Wire Format | Per-tile-type button block offsets (slider DATA 8..10 vs button DATA 0..2), fader decode, SEQ/STATUS decode, frame checksum, identity validation, `TileButton` press/hold/tap |
| 11 | `tests/unit/test_app_runtime.cpp` | App runtime helpers | PCM16 DAC conversion (clipping/truncation, independent stereo channels, `[app][pcm]`), lidar recording calibration across the 55–700 mm window (`[app][recording]`) |
| 12 | `tests/unit/test_audio_i2s.cpp` | I2S output path (`pico2seq_audio_tests`) | Rendered buffers handed to DMA, starvation recovery (`[audio][i2s]`, isolated `tests/audio_stubs/`) |
| 13 | `tests/unit/test_freeze_watchdog.cpp` | `FreezeWatchdog` (`pico2seq_watchdog_tests`) | Watchdog scratch evidence, boot vs late-serial reconnect, no stale reports on normal boot (`[watchdog]`, isolated `tests/watchdog_stubs/`) |
| 14 | `tests/unit/test_voice_recipes.cpp` | Recipe/engine voices | Preset registry coherence (29 presets across core, recipes, and musical presets), waveguide tails across engine resets, recipe timbre lanes, envelope gate/retrigger behavior (`[voice][presets][waveguide][recipes]`) |
| 15 | `tests/unit/test_voice_edit.cpp` | Voice Editing mode | Base vs lidar-modifier independence, neutral-modifier preset round-trip, parameter catalogue reachability/clamping per engine, editor release semantics, muted-editor queue draining (`[voice_edit][recording]`) |
| 16 | `tests/unit/test_persistence.cpp` | Session persistence (`src/pico2seq-core/persistence/`, `src/voice/PatchCodec.*`) | CRC32 vector, frame magic/version/size/CRC rejection, locked snapshot layout (10,312 bytes format 1, 12,400 bytes format 2, 12,448 bytes format 3 with the effect record), v1/v2 upgrade, effect validation, snapshot validation bounds, pattern round-trip incl. raw tails, patch codec pointer re-derivation, golden full-project round-trip, watchdog resume decision table, retained-store validity (`[persistence]`) |
| 17 | `tests/unit/test_recipe_optimization.cpp` | `rpdsp` Recipe CPU Optimizations | Prepared oscillator phase/spectra, cached coefficient survival across edits/triggers, feedback operator history (`[optimization][recipes][voice]`) |
| 18 | `tests/unit/test_master_compressor.cpp` | Master-bus macro knob (`VoiceManager`) | `rpdsp::Compressor` Warm/Glue/Punch curve anchors, gain reduction on high-amplitude streams, Punch squeezes harder than Warm to DAC-safe levels, gradual (not instant) morphs, silence passthrough (`[master][compressor]`, also in `pico2seq_voice_tests`) |
| 19 | `tests/unit/test_master_delay.cpp` | Master-bus delay | Fractional reads, filtered repeats, feedback bounds and mix smoothing (`[master_delay]`) |
| 20 | `tests/unit/test_master_bus.cpp` | Combined delay, reverb and compressor | Bus order, dry bypass, tails, mute, volume, audible fader range, stereo reverb bus (`[master_bus]`, `[reverb_bus]`) |
| 21 | `tests/unit/test_master_reverb.cpp` | `MasterReverb` adapter | Mix-zero bit-exactness, lock-free control hand-off, snapshots, eased controls, freeze, storage variants, allocation (`[reverb][master_reverb]`) |
| 22 | `tests/unit/test_reverb_page.cpp`, `test_reverb_editor.cpp` | Reverb page | Entry gesture, fader layers and curves, OLED formatting, published values (`[reverb_page]`) |
| 23 | `tests/unit/test_stack_watermark.cpp` | `StackWatermark` | Paint/scan arithmetic (`[stack]`) |
| 24 | `tests/unit/test_reverb_bypass.cpp` | Bench bypass build (`pico2seq_reverb_bypass_tests`) | Dry bus, control hand-off |

---

## Stubs Architecture (`tests/stubs/`)

When compiling test targets, `tests/stubs/` is placed **first** in the compiler's include search paths, shadowing embedded headers before system or toolchain headers can be resolved:

```cmake
# tests/CMakeLists.txt
target_include_directories(pico2seq_tests PRIVATE
  ${STUB_DIR}          # <-- Stubs first: shadows Arduino.h, Wire.h, etc.
  ${PROJECT_SOURCE_DIR}
  ${SRC_DIR}
  ${CORE_DIR}
  ${RPDSP_DIR}
)
```

### Stub Header Inventory

```
tests/stubs/
├── Arduino.h          # Stubs pinMode, digitalWrite, digitalRead, millis, micros, delay, HardwareSerial
├── Wire.h             # Stubs TwoWire Wire I2C transmission methods
├── hardware/
│   └── gpio.h         # Stubs Pico SDK GPIO functions (gpio_init, gpio_set_dir, gpio_put)
└── pico/
    └── sync.h         # Stubs Pico SDK spinlock API (spin_lock_t, spin_lock_blocking, spin_unlock)
```

**Stub Design Rule:** *Stub the interface, not the implementation.* Functions in stubs provide no-op bodies or return predictable default values (e.g., `millis()` returning constant or tick values, I2C `endTransmission()` returning 0 for success).

---

## Build & Test Workflow

### 1. Build and Run the Test Suite Locally

```bash
# Configure the build directory (Debug mode)
cmake -B build_test -DCMAKE_BUILD_TYPE=Debug

# Compile the test runner executables
cmake --build build_test --parallel

# Execute the main test runner directly
./build_test/tests/pico2seq_tests

# Or run individual specialized test executables:
./build_test/tests/pico2seq_voice_tests      # Focused voice ownership, master bus and reverb suite
./build_test/tests/pico2seq_ui_tests         # Control surface, UI transitions, Reverb page
./build_test/tests/pico2seq_watchdog_tests   # FreezeWatchdog forensics suite
./build_test/tests/pico2seq_audio_tests      # I2S DMA/pool driver suite
./build_test/tests/pico2seq_reverb_bypass_tests  # Bench-only reverb bypass build
```

*(On Windows PowerShell, append `.exe` to executable names; `ctest --test-dir build_test` runs every discovered test across the six targets: 692 on 2026-09-30, 33 of them the known baseline failures above.)*

### 2. Run with CTest

```bash
# Run all discovered tests with full output on failure
ctest --test-dir build_test --output-on-failure
```

### 3. Run Specific Test Tags or Filters

```bash
# Run only DSP and mathematical utility tests
./build_test/tests/pico2seq_tests "[rpdsp]"

# Run only sequencer tests
./build_test/tests/pico2seq_tests "[sequencer]"

# Run only voice-transfer (SpscQueue control handoff) tests
./build_test/tests/pico2seq_tests "[voice_transfer]"

# Run only voice oscillator tests
./build_test/tests/pico2seq_tests "[voiceosc]"

# Run only control surface logic tests
./build_test/tests/pico2seq_tests "[control_surface]"

# Run only Alchemy tile wire-format tests
./build_test/tests/pico2seq_tests "[alchemy_proto]"

# Run only voice engine tests
./build_test/tests/pico2seq_tests "[voice]"

# Run only musical scale table tests
./build_test/tests/pico2seq_tests "[scales]"

# List all test cases without running
./build_test/tests/pico2seq_tests --list-tests
```

---

## Key Testing Pitfalls & Gotchas

### 1. Staged Parameter Updates in `Voice`
`Voice::updateParameters()` does not immediately overwrite active synthesis state; changes enter a bounded queue, and each `Voice::process()` consumes at most one update. If several setters run first, render enough samples to consume them in order. Setters before `init()` establish initial state. In unit tests, always invoke `process()` before asserting against `getState()`:

```cpp
voice.updateParameters(voiceState);
voice.process();  // Consumes one queued update into audio-owned state
REQUIRE(voice.getState().velocityLevel == 0.5f);
```

### 2. Scale Pointer Dependency
`Voice` consumes scale tables via dependency injection rather than reading global variables. In unit tests, inject either a real scale table or `nullptr` to verify the chromatic fallback behavior:

```cpp
voice.setScaleTable(scale, SCALES_COUNT);
voice.setCurrentScalePointer(&scaleIndex);
```

### 3. External Symbol Single-Definition Rule
When testing files that declare `extern` globals (e.g. `slideMode` or `currentScale`), define those symbols **only once** in `tests/unit/test_helpers.cpp` to prevent linker multiple-definition collisions across test translation units.

Two subsystems compile against their own dedicated stub sets instead of the shared
`tests/stubs/`: `src/audio/` (I2S driver) builds against `tests/audio_stubs/`, and
`src/utils/FreezeWatchdog.h` builds against `tests/watchdog_stubs/` — both wired as
separate CMake targets in `tests/CMakeLists.txt`.

---

## Future Test Coverage Opportunities

| Target Module | Functionality to Cover |
|---|---|
| `src/pico2seq-core/sequencer/Sequencer.cpp` | `advanceStep()` polyrhythmic step progression across independent tracks |
| `src/voice/VoicePresets.cpp` | Boundary assertion that all preset parameter values stay within [0.0, 1.0] |

---

## Related Documentation

- [`docs/architecture.md`](architecture.md) — System architecture and dual-core division
- [`docs/voice.md`](voice.md) — Voice synthesis and DSP chain documentation
- [`docs/sequencer.md`](sequencer.md) — Sequencer engine and polymetric parameter tracks
- [`docs/superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md`](superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md) — ControlSurfaceLogic design specification

### Voice ownership regression suite

Build `pico2seq_voice_tests` and run `build_test/tests/pico2seq_voice_tests`
(`.exe` on Windows). This focused target includes voice/oscillator tests,
VoiceManager integration, queue wrap/full cases, gate ordering, and concurrent
producer/consumer stress. Use `[voice_transfer]` for ownership tests only.
The full `pico2seq_tests` target includes these tests and the DSP recipe suite;
a passing focused target does not imply the full suite builds. Hardware audio
timing and listening remain bench checks.
