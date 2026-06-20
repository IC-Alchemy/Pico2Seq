# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Pico2Seq is firmware for a 4-voice polyphonic step sequencer/synthesizer running on a
Raspberry Pi Pico 2 (RP2350). It is an **Arduino sketch**, not a hosted application — there
is no `main()`, no OS, and most code only makes sense in terms of the dual-core
`setup()`/`loop()` (Core 0) and `setup1()`/`loop1()` (Core 1) Arduino lifecycle defined in
`Pico2Seq.ino`.

Two build systems coexist and never touch each other:
- **Arduino IDE** builds and flashes the actual firmware from `Pico2Seq.ino`.
- **CMake** builds *only* the host-side unit test suite in `tests/`. It cannot build or flash
  the firmware itself.

## Commands

### Run the unit test suite (the only thing you can actually build/run from a CLI)

```bash
cmake -B build_test -DCMAKE_BUILD_TYPE=Debug
cmake --build build_test --parallel
./build_test/tests/pico2seq_tests --reporter console
```

Run a single tag/group instead of the full suite:

```bash
./build_test/tests/pico2seq_tests "[adsr]"
./build_test/tests/pico2seq_tests "[sequencer]"
./build_test/tests/pico2seq_tests "[voice]"
```

Other useful invocations:

```bash
./build_test/tests/pico2seq_tests --list-tests        # list all tests without running
./build_test/tests/pico2seq_tests --reporter console -s  # full detail on failures
ctest --test-dir build_test --output-on-failure       # what CI runs
```

CI (`.github/workflows/tests.yml`) runs on every push/PR: `cmake -B build`, `cmake --build build
--parallel`, then `ctest --test-dir build --output-on-failure`.

### Firmware build (cannot be done headlessly)

There is no CLI firmware build — it requires Arduino IDE with RP2040/RP2350 board support,
the libraries listed in `README.md` (Adafruit_MPR121, Adafruit_SH110X, Adafruit_TinyUSB,
FastLED, Melopero_VL53L1X, uClock, MIDI), and a physical Pico2 board with the wired
peripherals described in `README.md` to test on. Don't claim to have "built the firmware" —
at most you can verify the test suite passes and reason about the code.

## Testing strategy — read this before touching anything under `src/`

The full rationale lives in `docs/testing.md`; the essential model:

Embedded code can't run as a normal binary on a dev machine, so the test suite only covers
code with **no hardware dependencies**, using header stubs in `tests/stubs/` (e.g. `Arduino.h`,
`Wire.h`, `pico/sync.h`) to satisfy `#include`s without real hardware. `tests/stubs/` mirrors
real header paths exactly — a stub for `pico/sync.h` must live at `tests/stubs/pico/sync.h`.

What's tested vs. not, per `tests/CMakeLists.txt`:
- **Tested** (compiled into `pico2seq_tests`): `src/dsp/*` (adsr, oscillator, ladder, svf,
  overdrive, wavefolder, dcblock, tremolo), `src/scales/scales.cpp`,
  `src/sequencer/{ParameterManager,Sequencer}.cpp`, `src/voice/{Voice,VoicePresets}.cpp`.
- **Not tested, by design** (hardware-bound glue — keep logic out of these):
  `src/audio/` (PIO/DMA/I2S), `src/LEDMatrix/` (WS2812B GPIO/DMA), `src/OLED/` (I2C display),
  `src/midi/` (TinyUSB stack).

When adding a new module to be tested:
1. Check its `#include` chain for new hardware headers; add a minimal stub under `tests/stubs/`
   if needed (no-op functions are fine — stub the interface, not the implementation).
2. Add the source file to `add_executable(pico2seq_tests ...)` in `tests/CMakeLists.txt`.
3. If the file references `extern` globals defined in `Pico2Seq.ino`/`audio.cpp` (not compiled
   into the test binary), define them once in `tests/unit/test_helpers.cpp` — never in more than
   one test file, or the linker will complain about multiple definitions.
4. Write `tests/unit/test_yourmodule.cpp`.

Untested modules that would benefit from coverage (per `docs/testing.md`):
`VoiceManager.cpp` (`addVoice`, `processAllVoices`, preset application), `Sequencer::advanceStep`
polyrhythmic behavior, `dsp/tremolo.cpp`, `dsp/compressor.cpp`, `VoicePresets.cpp` range validation.

Two real gotchas documented in `docs/testing.md`, worth knowing before you write voice tests:
- `Voice::updateParameters()` stages changes into `stagedState_`; they only take effect after the
  next `process()` call. Checking `getState()` right after `updateParameters()` sees stale data.
- `Voice` takes its scale table via `setScaleTable()`/`setCurrentScalePointer()` injection rather
  than reading globals — pass `nullptr` to both to exercise the chromatic fallback path.

## Architecture

`docs/architecture.md` is the canonical deep-dive; `docs/{voice,sequencer,matrix,midi,oled,
sensors,ButtonHandlers}.md` cover each subsystem. The essentials:

### Dual-core split (the most important thing to keep in mind for any change)

- **Core 0** (`setup()`/`loop()` in `Pico2Seq.ino`): audio synthesis only. Pulls a buffer,
  calls `voiceManager->processAllVoices()` per-sample, writes I2S output. Nothing else should
  run here — this is real-time critical and must never block or allocate.
- **Core 1** (`setup1()`/`loop1()`): everything else — MIDI I/O, AS5600 encoder and VL53L1X
  distance sensor polling, MPR121 touch matrix scanning, `uClock` sequencer step ticking, LED
  matrix and OLED updates, UI state.
- Cross-core communication is via `volatile` globals (e.g. `VoiceSystem::gates`,
  `ppqnTicksPending`) — there are no mutexes. When touching shared state, check whether it's
  read/written from both cores and keep the existing `volatile` discipline.

### `VoiceSystem` — the central data structure

`src/voice/VoiceSystem.h` replaced what used to be parallel global arrays (`voice1Id`,
`voice2Id`, ...) with array-based, bounds-checked access for `MAX_VOICES = 4` voices:
`voiceIds[]`, `voiceStates[]` (one `VoiceState` per voice), `gates[]`/`gateTimers[]` (only
voices 0–1 have hardware gate/MIDI support; voices 2–3 are audio-only). Always go through its
accessors (`getVoiceState`, `getVoiceId`, `getGate`, `getGateTimer`) rather than indexing arrays
directly — they clamp out-of-range indices.

Voice index 0–3 is used consistently across the codebase; some functions (`updateVoiceParameters`,
gate/MIDI helpers) still branch on `isVoice2` as a holdover from the original 2-voice design —
this only applies to voices 0/1, since 2/3 never had gates or MIDI wired up.

### Data flow (input → sound)

```
Matrix/AS5600/VL53L1X/MIDI input  (Core 1)
  → UIEventHandler / ButtonHandlers  → UIState (single struct, no loose globals)
  → 4 independent Sequencer instances (seq1..seq4, one per voice, polymetric: each
    ParamId track can have its own step count, e.g. Note:16 steps, Filter:8 steps)
  → VoiceState produced per step (onStepCallback, called by uClock on each 16th note)
  → VoiceSystem (gate timing, MIDI note on/off via MidiNoteManager) → VoiceManager
  → Voice DSP chain (oscillators → ladder filter → ADSR → overdrive/wavefolder)
  → fill_audio_buffer()  (Core 0)  → I2S @ 48kHz
```

`Sequencer::ParameterTrack<N>` (in `SequencerDefs.h`) is the polymetric building block: each
`ParamId` (Note, Velocity, Filter, Attack, Decay, Octave, GateLength, Gate, Slide) gets its own
fixed-size array with an independent `currentStepCount` and modulo-wrapping `getValue()`. This
is what makes "Note track at 16 steps, Filter track at 8 steps" possible on the same voice.

### Key conventions to preserve when editing

- **No heap allocation in the audio/sequencer hot path.** Static/fixed-size arrays
  (`ParameterTrack<MAX_SIZE>`, `voiceStates[MAX_VOICES]`) are deliberate — don't introduce
  `std::vector`/`new` into anything reachable from `loop()` or `onStepCallback()`.
  `VoiceManager` is one of the few places using `std::unique_ptr`, and only for setup-time
  ownership, not per-sample work.
- **`UIState` is the single source of UI truth.** Don't add new free-floating globals for UI
  flags/modes — extend the `UIState` struct (`src/ui/UIState.h`) the way every existing mode
  flag, debounce timestamp, and menu index already lives there.
- **Voice index is always 0-based (0–3)** in internal APIs; some UI/MIDI code displays it
  1-based to users (see `applyVoicePreset` printing `voiceIndex` with a comment noting "0-based
  display"). Don't conflate the two.
- **Declarations and definitions must agree on `noexcept`.** Arduino's GCC silently accepted
  mismatches; the host GCC used for tests does not (see `docs/testing.md` step 7 for the
  `Voice.h`/`Voice.cpp` bug this caught). If you add a method with `noexcept` in the `.cpp`,
  the header declaration needs it too, or the test build breaks.
- **DSP code under `src/dsp/` is a local, embedded-optimized fork of DaisySP**, not the
  upstream library — check existing files for conventions before adding a new processor rather
  than pulling in DaisySP fresh.
- Project naming history: some older sub-READMEs (`src/matrix/README.md`, `src/midi/README.md`)
  still say "Mudras Sequencer"/"PicoMudrasSequencer" from before the project was renamed to
  Pico2Seq, and reference a `PROGRAMMERS_MANUAL.md` that no longer exists. Don't treat those as
  current naming — `Pico2Seq.ino` and the root `README.md` are authoritative.
