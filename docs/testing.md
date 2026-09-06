# Testing Embedded C++ on a Host Machine

## Overview

Pico2Seq firmware targets the Raspberry Pi Pico 2 (RP2350 microcontroller). Because microcontrollers execute bare-metal firmware without an underlying OS, executing device binaries natively on host development machines (Linux, macOS, Windows) is impossible without hardware emulation.

To enable rapid, automated regression testing, Pico2Seq employs a **host-side unit testing architecture**:
1. **Core Decoupling:** Pure mathematical algorithms, musical scales, sequencing state machines, DSP filters/oscillators, and UI decision logic are decoupled from hardware peripherals.
2. **Hardware Header Stubs (`tests/stubs/`):** Minimal lightweight mock headers shadow microcontroller-specific APIs (`Arduino.h`, `Wire.h`, `pico/sync.h`, `hardware/gpio.h`).
3. **Catch2 Test Framework:** Tests are written in modern C++17 using Catch2 v3.5.2 and executed locally via CMake or directly against the compiled test binary.

---

## Testing Strategy & Module Classification

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
| **Tier 3: Hardware-Bound** | I2S, LED, OLED, MIDI, Sensors | `src/audio/`, `src/LEDMatrix/`, `src/OLED/`, `src/midi/`, `src/sensors/` | Hardware-dependent glue. Kept thin; validated on physical hardware. |

---

## 9 Host Unit Test Suites

The host test executable (`pico2seq_tests`) links all unit suites under `tests/unit/`:

| # | Test Suite File | Tested Components | Key Test Areas |
|---|---|---|---|
| 1 | `tests/unit/test_helpers.cpp` | Global Test Helper Symbols | Provides single definition of extern symbols (`slideMode`, `MAX_DELAY_SAMPLES`) |
| 2 | `tests/unit/test_rpdsp_additions.cpp` | `rpdsp` DSP Extensions | `dspmap::fmap` curves (local carry-over), Waveshaper transfer functions, DSPFunctions |
| 3 | `tests/unit/test_scales.cpp` | Musical Scale Lookup Tables | 13 scales monotonic ordering, root notes at 0, MIDI boundary validation, chromatic fallback |
| 4 | `tests/unit/test_sequencer.cpp` | Core Step Sequencer | `ParameterTrack<N>` wrapping, `NoteDurationTracker` countdowns, start/stop, gate toggling |
| 5 | `tests/unit/test_step_tick_queue.cpp` | `StepTickQueue` ISR→loop handoff ring | FIFO push/pop order, full-queue drop behavior, empty/size invariants (`[steptick]`) |
| 6 | `tests/unit/test_voice.cpp` | Synthesizer Voice Engine | Voice state transitions, staged parameter application on `process()`, scale injection, filter sweep, preset registry (15 named presets, engine selection, finite bounded audio per preset), waveguide / noise-FX engine behavior |
| 7 | `tests/unit/test_voiceoscillator.cpp` | Voice Oscillator Dispatch | `VoiceOscillator` variant dispatch, band-limited waveforms, pulse width modulation, pitch changes |
| 8 | `tests/unit/test_control_surface_logic.cpp` | Tile UI Decision Logic | `ModeStabilizer` debouncing, `PadBank` voice-pair resolution, `ShiftLatch` latching, `FaderMap` deadband |
| 9 | `tests/unit/test_alchemy_proto.cpp` | Alchemy Tile Wire Format | Per-tile-type button block offsets (slider DATA 8..10 vs button DATA 0..2), fader decode, SEQ/STATUS decode, frame checksum, identity validation, `TileButton` press/hold/tap |

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

# Compile the test runner executable
cmake --build build_test --parallel

# Execute the test runner directly
./build_test/tests/pico2seq_tests
```

*(On Windows PowerShell, run `./build_test/tests/pico2seq_tests.exe`)*

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

# Run only StepTickQueue handoff-ring tests
./build_test/tests/pico2seq_tests "[steptick]"

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
When testing files that declare `extern` globals (e.g. `slideMode` or `MAX_DELAY_SAMPLES`), define those symbols **only once** in `tests/unit/test_helpers.cpp` to prevent linker multiple-definition collisions across test translation units.

---

## Future Test Coverage Opportunities

| Target Module | Functionality to Cover |
|---|---|
| `src/voice/VoiceManager.cpp` | Multi-voice routing (`addVoice`, `processAllVoices`), global preset application |
| `src/pico2seq-core/sequencer/Sequencer.cpp` | `advanceStep()` polyrhythmic step progression across independent tracks |
| `src/voice/VoicePresets.cpp` | Boundary assertion that all preset parameter values stay within [0.0, 1.0] |
| `src/rpdsp/` `Compressor` | Master mix gain reduction verification on high-amplitude audio streams |

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
