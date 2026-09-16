# Testing Embedded C++ on a Host Machine

## Overview

Pico2Seq firmware targets the Raspberry Pi Pico 2 (RP2350 microcontroller). Because microcontrollers execute bare-metal firmware without an underlying OS, executing device binaries natively on host development machines (Linux, macOS, Windows) is impossible without hardware emulation.

To enable rapid, automated regression testing, Pico2Seq employs a **host-side unit testing architecture**:
1. **Core Decoupling:** Pure mathematical algorithms, musical scales, sequencing state machines, DSP filters/oscillators, and UI decision logic are decoupled from hardware peripherals.
2. **Hardware Header Stubs (`tests/stubs/`):** Minimal lightweight mock headers shadow microcontroller-specific APIs (`Arduino.h`, `Wire.h`, `pico/sync.h`, `hardware/gpio.h`).
3. **Catch2 Test Framework:** Tests are written in modern C++17 using Catch2 v3.5.2 and executed locally via CMake or directly against the compiled test binary.

---

## Testing Strategy & Module Classification

Application glue lives in `src/app/`; see the
[firmware structure guide](firmware-structure.md). `test_app_runtime.cpp`
checks every PCM16 level, clipping/truncation and hand-distance recording
calibration (`[app]`). The Arduino build checks the hardware-bound app `.cpp`
files; host CMake does not compile that startup/I2S/control glue.

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
| **Tier 1: Zero Deps** | Alchemy Tile Wire Format | `src/AlchemyUI/src/{AlchemyProto,TileButton}.h` | Pure C++ register/frame decoding and the 11-byte `StatePacket` — no Arduino, no Wire. Tested natively. |
| **Tier 1: Zero Deps** | Satellite Link State | `src/AlchemyUI/src/SatelliteLink.h` | Sequence counter, timeout and last-known-good policy. Milliseconds arrive as arguments. Tested natively. |
| **Tier 2: Light Stubs** | Alchemy Tile Driver | `src/AlchemyUI/src/AlchemyTiles.cpp` | The real bus master, driven against the scriptable `TwoWire` in `tests/tile_stubs/` (`pico2seq_tile_tests`). |
| **Tier 2: Light Stubs** | Musical Scales | `src/pico2seq-core/scales/scales.cpp` | Requires minimal `Arduino.h` type aliases (`uint8_t`, `String`). |
| **Tier 2: Light Stubs** | Sequencer Logic | `src/pico2seq-core/sequencer/{Sequencer,ParameterManager}.cpp` | Requires `Arduino.h` and `pico/sync.h` spinlock stubs. |
| **Tier 2: Light Stubs** | Voice & Presets | `src/voice/{Voice,VoicePresets}.cpp` | Requires staged parameter and scale table injection. |
| **Tier 3: Hardware-Bound** | I2S, LED, OLED, MIDI, Sensors | `src/audio/`, `src/LEDMatrix/`, `src/OLED/`, `src/midi/`, `src/sensors/` | Hardware-dependent glue. Kept thin; validated on physical hardware. |

---

## 17 Host Unit Test Suites

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
| 10a | `tests/unit/test_satellite_link.cpp` | Satellite cached control state | SEQ dedupe and wrap, liveness from SEQ/HEARTBEAT (a tile that answers but stopped sampling still goes Stale), timeout into Stale, buttons released / faders held while stale, recovery re-publish, rejected reads never overwriting the cache (`[satellite_link]`) |
| 10c | `tests/unit/test_py32_slider_tile.cpp` | PY32 slider tile firmware (`py32_slider_tests`) | The sketch itself: identity block, coherent checksummed frame, SEQ vs HEARTBEAT, publish never stalled by an in-flight or abandoned read, survival of a peripheral rebuild, sticky edges cleared only once delivered, TXE+BTF in one snapshot, pointer parking (`[py32][slider]`, isolated `tests/py32_stubs/`) |
| 10d | `tests/unit/test_py32_button_tile.cpp` | PY32 button tile firmware (`py32_button_tests`) | Same slave contract plus the 8-bit bitmap in a 3-byte DATA block and the shared bus rate (`[py32][button]`) |
| 10b | `tests/unit/test_alchemy_tiles.cpp` | Alchemy tile driver (`pico2seq_tile_tests`) | One-transaction snapshot poll, sticky edges delivered once and never re-delivered from a re-read frame, a frozen-but-answering tile caught, a dead satellite holding faders but dropping button holds, corrupt/short frames refused (`[alchemy_tiles]`, isolated `tests/tile_stubs/`) |
| 11 | `tests/unit/test_app_runtime.cpp` | App runtime helpers | PCM16 DAC conversion (clipping/truncation, `[app][pcm]`), lidar recording calibration across the 55–700 mm window (`[app][recording]`) |
| 12 | `tests/unit/test_audio_i2s.cpp` | I2S output path (`pico2seq_audio_tests`) | Rendered buffers handed to DMA, starvation recovery (`[audio][i2s]`, isolated `tests/audio_stubs/`) |
| 13 | `tests/unit/test_freeze_watchdog.cpp` | `FreezeWatchdog` (`pico2seq_watchdog_tests`) | Watchdog scratch evidence, boot vs late-serial reconnect, no stale reports on normal boot (`[watchdog]`, isolated `tests/watchdog_stubs/`) |
| 14 | `tests/unit/test_voice_recipes.cpp` | Recipe/engine voices | Preset registry coherence (29 presets across core, recipes, and musical presets), waveguide tails across engine resets, recipe timbre lanes, envelope gate/retrigger behavior (`[voice][presets][waveguide][recipes]`) |
| 15 | `tests/unit/test_voice_edit.cpp` | Voice Editing mode | Base vs lidar-modifier independence, neutral-modifier preset round-trip, parameter catalogue reachability/clamping per engine, editor release semantics, muted-editor queue draining (`[voice_edit][recording]`) |
| 16 | `tests/unit/test_persistence.cpp` | Session persistence (`src/pico2seq-core/persistence/`, `src/voice/PatchCodec.*`) | CRC32 vector, frame magic/version/size/CRC rejection, locked 10,312-byte snapshot layout, snapshot validation bounds, pattern round-trip incl. raw tails, patch codec pointer re-derivation, golden full-project round-trip, watchdog resume decision table, retained-store validity (`[persistence]`) |
| 17 | `tests/unit/test_recipe_optimization.cpp` | `rpdsp` Recipe CPU Optimizations | Prepared oscillator phase/spectra, cached coefficient survival across edits/triggers, feedback operator history (`[optimization][recipes][voice]`) |

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

# Execute the main test runner directly (240 tests)
./build_test/tests/pico2seq_tests

# Or run individual specialized test executables:
./build_test/tests/pico2seq_voice_tests      # Focused voice ownership & queue suite (70 tests)
./build_test/tests/pico2seq_watchdog_tests   # FreezeWatchdog forensics suite (4 tests)
./build_test/tests/pico2seq_audio_tests      # I2S DMA/pool driver suite (1 test)
```

*(On Windows PowerShell, append `.exe` to executable names; `ctest --test-dir build_test` executes all 315 tests across all 4 targets)*

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

# Run only the satellite link-state tests (sequence / timeout / last-known-good)
./build_test/tests/pico2seq_tests "[satellite_link]"

# Run the tile driver against the scriptable I2C bus (separate target)
./build_test/tests/pico2seq_tile_tests

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

Four subsystems compile against their own dedicated stub sets instead of the shared
`tests/stubs/`: `src/audio/` (I2S driver) builds against `tests/audio_stubs/`,
`src/utils/FreezeWatchdog.h` builds against `tests/watchdog_stubs/`,
`src/AlchemyUI/src/AlchemyTiles.cpp` builds against `tests/tile_stubs/`, and the PY32
tile sketches in `tiles/` build against `tests/py32_stubs/` — all wired as separate
CMake targets in `tests/CMakeLists.txt`.

`tests/py32_stubs/` is the odd one: it shims the PY32Duino core and its HAL so the tile
sketches compile unmodified on the host, and `TileHarness.h` plays the RP2350 master
against their I2C slave ISR flag by flag. That is the only way to reach the ISR's own
edge cases — a master that stops mid-frame, BTF arriving with TXE already set, a
peripheral reset in the middle of a read. It models no timing at all; anything that
needs the part still needs the part.

`tests/tile_stubs/` shadows only `Wire.h` (`Arduino.h` still comes from `tests/stubs/`),
replacing the no-op bus with a scriptable one: tests attach `FakeTile` devices, run their
own 4 ms sample sweeps, and make them NACK, short-read, corrupt their checksum or freeze
(answer perfectly while nothing behind the frame changes). Assertions cover both what the
driver decoded and how many transactions it spent. That is the only place the
one-transaction snapshot poll can actually be proven.

---

## Future Test Coverage Opportunities

| Target Module | Functionality to Cover |
|---|---|
| `src/pico2seq-core/sequencer/Sequencer.cpp` | `advanceStep()` polyrhythmic step progression across independent tracks |
| `src/voice/VoicePresets.cpp` | Boundary assertion that all preset parameter values stay within [0.0, 1.0] |
| `src/rpdsp/` `Compressor` | Master mix gain reduction verification on high-amplitude audio streams |

---

## Related Documentation

- [`docs/architecture.md`](architecture.md) — System architecture and dual-core division
- [`docs/voice.md`](voice.md) — Voice synthesis and DSP chain documentation
- [`docs/sequencer.md`](sequencer.md) — Sequencer engine and polymetric parameter tracks
- [`docs/superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md`](superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md) — ControlSurfaceLogic design specification
- [`docs/alchemy-satellite-link.md`](alchemy-satellite-link.md) — Satellite state packet format and the sequence/timeout/last-known-good contract
- [`tiles/README.md`](../tiles/README.md) — PY32 tile firmware, its build, and the three rules the hub is built on

### Voice ownership regression suite

Build `pico2seq_voice_tests` and run `build_test/tests/pico2seq_voice_tests`
(`.exe` on Windows). This focused target includes voice/oscillator tests,
VoiceManager integration, queue wrap/full cases, gate ordering, and concurrent
producer/consumer stress. Use `[voice_transfer]` for ownership tests only.
The full `pico2seq_tests` target includes these tests and the DSP recipe suite;
a passing focused target does not imply the full suite builds. Hardware audio
timing and listening remain bench checks.
