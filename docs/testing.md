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
| **Tier 1: Zero Deps** | UI Control Surface Logic | `src/ui/ControlSurfaceLogic.h/.cpp` | Pure state machines (`ModeStabilizer`, `PadBank`, `ShiftLatch`, `FaderMap`, `StepEditOwnership`, `EncoderMotion`). Tested natively. |
| **Tier 1: Zero Deps** | OLED Page Routing | `src/ui/OledView.h/.cpp` | Pure page/value routing decisions; `oled.cpp` only renders a `Route`. Tested natively. |
| **Tier 1: Zero Deps** | Alchemy Tile Wire Format | `src/AlchemyUI/src/{AlchemyProto,TileButton}.h` | Pure C++ register/frame decoding — no Arduino, no Wire. Tested natively. |
| **Tier 2: Light Stubs** | Musical Scales | `src/pico2seq-core/scales/scales.cpp` | Requires minimal `Arduino.h` type aliases (`uint8_t`, `String`). |
| **Tier 2: Light Stubs** | Sequencer Logic | `src/pico2seq-core/sequencer/{Sequencer,ParameterManager}.cpp` | Requires `Arduino.h` and `pico/sync.h` spinlock stubs. |
| **Tier 2: Light Stubs** | Voice & Presets | `src/voice/{Voice,VoicePresets}.cpp` | Requires staged parameter and scale table injection. |
| **Tier 3: Hardware-Bound** | I2S, LED, OLED, MIDI, Sensors | `src/audio/`, `src/LEDMatrix/`, `src/OLED/`, `src/midi/`, `src/sensors/` | Hardware-dependent glue. Kept thin; validated on physical hardware. |

---

## Focused UI transition checks

`UIState` derives settings-page queries from `settingsMode/currentSubMode` and
stores parameter-change feedback separately. `src/ui/UITransitions.h` contains
pure state transitions; hardware handlers own MIDI cleanup and tile edge history.
`tests/unit/test_ui_transitions.cpp` covers page reopening, feedback expiration
(including timer wrap), slide cleanup, and tile-selection versus pad-focus rules.

The focused target includes these tests, the existing ControlSurfaceLogic
suite, and the new focus/ownership policy suites (`test_parameter_focus.cpp`,
`test_input_ownership.cpp`):

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
- Enter slide with a Shift-latched parameter, pending pad hold, or encoder length hold:
  old holds/latches cannot reappear on release. Both slide toggles leave step editing clear.
- Press/release parameter tiles while sliding, then leave slide: no stale latch returns.
- Editor voice selection keeps per-voice cursors; session restore retains the saved voice
  without invoking live tile-selection note cleanup.

## 29 Host Unit Test Sources

The main host test executable (`pico2seq_tests`) links 27 of the 29 unit suites
under `tests/unit/` — the watchdog and I2S suites compile against their own
dedicated stub sets as separate targets (see below):

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
| 11 | `tests/unit/test_app_runtime.cpp` | App runtime helpers | PCM16 DAC conversion (clipping/truncation, `[app][pcm]`), lidar recording calibration across the 55–700 mm window (`[app][recording]`) |
| 12 | `tests/unit/test_audio_i2s.cpp` | I2S output path (`pico2seq_audio_tests`) | Rendered buffers handed to DMA, starvation recovery (`[audio][i2s]`, isolated `tests/audio_stubs/`) |
| 13 | `tests/unit/test_freeze_watchdog.cpp` | `FreezeWatchdog` (`pico2seq_watchdog_tests`) | Watchdog scratch evidence, boot vs late-serial reconnect, no stale reports on normal boot (`[watchdog]`, isolated `tests/watchdog_stubs/`) |
| 14 | `tests/unit/test_voice_recipes.cpp` | Recipe/engine voices | Preset registry coherence (29 presets across core, recipes, and musical presets), waveguide tails across engine resets, recipe timbre lanes, envelope gate/retrigger behavior (`[voice][presets][waveguide][recipes]`) |
| 15 | `tests/unit/test_voice_edit.cpp` | Voice Editing mode | Base vs lidar-modifier independence, neutral-modifier preset round-trip, parameter catalogue reachability/clamping per engine, editor release semantics, muted-editor queue draining (`[voice_edit][recording]`) |
| 16 | `tests/unit/test_persistence.cpp` | Session persistence (`src/pico2seq-core/persistence/`, `src/voice/PatchCodec.*`) | CRC32 vector, frame magic/version/size/CRC rejection, locked 10,312-byte snapshot layout, snapshot validation bounds, pattern round-trip incl. raw tails, patch codec pointer re-derivation, golden full-project round-trip, watchdog resume decision table, retained-store validity (`[persistence]`) |
| 17 | `tests/unit/test_recipe_optimization.cpp` | `rpdsp` Recipe CPU Optimizations | Prepared oscillator phase/spectra, cached coefficient survival across edits/triggers, feedback operator history (`[optimization][recipes][voice]`) |
| 18 | `tests/unit/test_sequencer_view.cpp` | Read-only sequencer routing view | 4-voice routing table, invalid-edit rejection, last-voice display fallback (`[app][sequencer_view]`) |
| 19 | `tests/unit/test_settings_pads.cpp` | Settings pad catalogue | 32-pad parameter map, settings availability/toggles/choices, numeric edits delegate to voice adjustments (`[settings_pads]`) |
| 20 | `tests/unit/test_ui_transitions.cpp` | Pure UI transitions | Settings page derivation, transient feedback, slide entry clears conflicting controls, tile selection vs pad focus (`[control_surface][ui_transitions]`) |
| 21 | `tests/unit/test_voice_playback.cpp` | Playback lifecycle | Retriggers as events, per-voice gate expiry, stop clears lifecycles, voice focus never ends sounding notes (`[voice_playback]`) |
| 22 | `tests/unit/test_parameter_randomize.cpp` | Randomizer | Depth/seed determinism, lane amounts, gates/slides untouched (`[randomize][sequencer]`) |
| 23 | `tests/unit/test_voice_block.cpp` | Block processing parity | `processBlock` == per-sample `process()`, queued gate edges reach samples in one block, VoiceManager parity (`[voice_block][voice_transfer]`) |
| 24 | `tests/unit/test_parameter_focus.cpp` | Parameter focus policy (`ShiftLatch::focus()`) | All six record buttons resolve to their ParamId + encoder base target; focus follows the most recent physical hold over the latch; armed lanes stay available to live recording; transition clears cannot resurrect stale holds; step-edit fallback chain (`[parameter_focus]`) |
| 25 | `tests/unit/test_input_ownership.cpp` | Manual-edit ownership (`StepEditOwnership`) + encoder accumulation | Manual edit suppresses lidar only for its exact voice/lane/step; hand-leave/rearm semantics; reset on target change; `EncoderMotion` accumulation, detents, direction reset (`[input_ownership]`) |
| 26 | `tests/unit/test_step_write.cpp` | Shared step write (`Sequencer::writeStepParameter`) | Per-lane min/mid/max acceptance, clamping + non-finite rejection, Note quantization/Octave zones, change detection after rounding, invalid lane/step rejection, `NoteGateRule::AtStep`/`AtGateCursor`, rest-step rules, live `advanceStep` cursor recording (`[step_write]`) |
| 27 | `tests/unit/test_edit_publication.cpp` | Publication + audio response | Neutral modifiers reproduce bases, endpoint composition, no-retrigger in-place refresh, `refreshVoiceParametersAt` preview, measurable Filter/Velocity/Attack/Decay response on oscillator presets, hard-sync slave pitch-vs-amplitude (`[edit_publication]`) |
| 28 | `tests/unit/test_oled_view.cpp` | OLED routing (`OledView::route`) | Page selection per focused lane, BASE feedback outranks holds, selected-step composed values, gate-rejected Note retention, encoder-target fallback, priority ladder, route purity (no mutation) (`[oled_view]`) |
| 29 | `tests/unit/test_parameter_mapping.cpp` | Lane value mapping (`DspMapping`) | `fmap`/`fmapCentered` exponential and centered curves, `normalizeCentered` inversion, centered lane bindings (`[mapping]`) |

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

# Execute the main test runner directly (358 tests)
./build_test/tests/pico2seq_tests

# Or run individual specialized test executables:
./build_test/tests/pico2seq_voice_tests      # Focused voice/step-write/publication suite (97 tests)
./build_test/tests/pico2seq_ui_tests         # Focus/ownership UI policy suite (61 tests)
./build_test/tests/pico2seq_watchdog_tests   # FreezeWatchdog forensics suite (4 tests)
./build_test/tests/pico2seq_audio_tests      # I2S DMA/pool driver suite (1 test)
```

*(On Windows PowerShell, append `.exe` to executable names; `ctest --test-dir build_test` executes all 521 tests across all 5 targets)*

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
| `src/rpdsp/` `Compressor` | Master mix gain reduction verification on high-amplitude audio streams |

---

## Related Documentation

- [`docs/architecture.md`](architecture.md) — System architecture and dual-core division
- [`docs/voice.md`](voice.md) — Voice synthesis and DSP chain documentation
- [`docs/sequencer.md`](sequencer.md) — Sequencer engine and polymetric parameter tracks

### Voice ownership regression suite

Build `pico2seq_voice_tests` and run `build_test/tests/pico2seq_voice_tests`
(`.exe` on Windows). This focused target includes voice/oscillator tests,
VoiceManager integration, queue wrap/full cases, gate ordering, and concurrent
producer/consumer stress. Use `[voice_transfer]` for ownership tests only.
The full `pico2seq_tests` target includes these tests and the DSP recipe suite;
a passing focused target does not imply the full suite builds. Hardware audio
timing and listening remain bench checks.
