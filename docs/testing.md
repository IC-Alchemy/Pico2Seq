# Testing Embedded C++ on a Host Machine

## Why this document exists

This project runs on a microcontroller — the RP2350 inside the Raspberry Pi Pico 2. That makes
testing tricky. You can't run `./my_program` on your laptop when the binary is meant for a chip with
no operating system, custom hardware peripherals, and a completely different instruction set.

This document explains, step by step, how the test suite in `tests/` was built, *why* each decision
was made, and how you can apply the same approach to any future embedded project.

---

## The core idea: test on the host, not the device

The key insight is that **most of the interesting logic in an embedded project is pure computation**
that has nothing to do with hardware. Audio DSP algorithms, musical scale lookups, step-sequencing
logic — none of these actually need a microcontroller to run. They only need a compiler.

The hardware-specific parts (I2C drivers, GPIO, PIO assembly, DMA) are genuinely hard to unit test
without the hardware. But those thin layers shouldn't contain business logic anyway.

The strategy therefore has two parts:

1. **Separate** the pure-logic code from the hardware-dependent code (we mostly had this already).
2. **Stub** the hardware headers so the pure-logic code compiles cleanly on a Linux or Mac host.

The resulting tests run in milliseconds on any laptop and can be automated in CI.

---

## Step 1: Audit the codebase for testability

Before writing a single test, you need to answer: *what can I actually test without hardware?*

The audit process:

1. Read every `#include` in every source file.
2. Group files into: *no hardware deps*, *minimal deps*, *heavy hardware deps*.
3. Draw the dependency graph mentally (or on paper).

Here is what that audit found for Pico2Seq:

### Tier 1 — Zero hardware dependencies (test immediately)

| Module | Files | Reason |
|--------|-------|--------|
| `src/dsp/` | `adsr`, `ladder`, `svf`, `oscillator`, `overdrive`, `wavefolder`, ... | Only `<stdint.h>`, `<cmath>` — pure math |
| `src/sequencer/SequencerDefs.h` | constants, enums, `ParameterTrack<N>` template | Only `<stdint.h>`, `<variant>` |

These compile on any C++17 host without modification.

### Tier 2 — Light stubs needed

| Module | What it needs |
|--------|--------------|
| `src/scales/scales.cpp` | Includes `Arduino.h` just for type aliases — a 3-line stub is enough |
| `src/sequencer/Sequencer.cpp` | Uses `Serial.print()` and `pinMode()`/`digitalWrite()` — a 20-line stub |
| `src/sequencer/ParameterManager.cpp` | Uses `pico/sync.h` for spinlocks — stub the spin_lock API |
| `src/voice/Voice.cpp` | Depends on sequencer headers (Tier 2 above) transitively |

### Tier 3 — Skip for now

| Module | Why |
|--------|-----|
| `src/audio/` | PIO assembly, DMA, hardware I2S — deeply Pico-specific |
| `src/LEDMatrix/` | WS2812B GPIO/DMA drivers |
| `src/OLED/` | I2C display hardware |
| `src/midi/` | TinyUSB stack |

Tier 3 modules should be kept thin (mostly hardware glue) and tested manually or with integration
tests on real hardware. Don't fight it — just keep the logic out of them.

---

## Step 2: Choose a test framework

There are three common options for embedded C++ projects:

| Framework | Language | Why choose it | Why not |
|-----------|----------|--------------|---------|
| **Catch2** | C++ | Single-header, modern syntax, FetchContent-friendly | Slightly heavier than Unity |
| **Unity** | C | Extremely lightweight, standard in embedded | C-only, verbose macros |
| **GoogleTest** | C++ | Powerful, good mocking | Heavy dependency, overkill here |

**Catch2 was chosen** because:
- The codebase is C++, so Catch2's C++ idioms (template matchers, `REQUIRE_THAT`, `SECTION`) fit naturally.
- It is fetched automatically via CMake's `FetchContent` — no manual installation required.
- The v3 API avoids the old macro-heavy style.

---

## Step 3: Choose a build system approach

The project uses Arduino IDE, which is not CMake-based. There are two options:

**Option A: Add CMake as the primary build for everything (including the device firmware).**
This is the right long-term answer but requires porting the entire Arduino build to CMake — a large
change that could break the existing workflow.

**Option B: Add CMake *only* for tests, in parallel.**
Arduino IDE builds `Pico2Seq.ino` as it always did. CMake builds `tests/` only. The two build systems
never touch each other.

**Option B was chosen** because it's zero-risk to the existing workflow. The root `CMakeLists.txt`
is invisible to Arduino IDE (it ignores `.txt` files). CI uses CMake. Developers keep using
Arduino IDE for flashing the device.

The structure is:

```
Pico2Seq/
├── Pico2Seq.ino          ← Arduino IDE entry point (unchanged)
├── CMakeLists.txt        ← CMake entry point (for tests ONLY)
└── tests/
    ├── CMakeLists.txt    ← Fetches Catch2, defines test executable
    ├── stubs/            ← Fake Arduino/Pico SDK headers
    └── unit/             ← Test files
```

---

## Step 4: Write the stub headers

Stubs are the most important — and least obvious — part of host-based embedded testing.

### How stubs work

When `Sequencer.cpp` writes `#include "Arduino.h"`, the compiler searches include paths in order.
By putting `tests/stubs/` **first** in `target_include_directories`, the compiler finds our fake
`Arduino.h` *before* looking in the system include paths or the Arduino SDK.

```cmake
target_include_directories(pico2seq_tests PRIVATE
  ${STUB_DIR}          # <-- stubs first, shadows real Arduino.h
  ${PROJECT_SOURCE_DIR}
  ${SRC_DIR}
)
```

### What to put in a stub

A stub only needs to provide enough for the code to *compile*. It does not need to do anything real.
Functions can be no-ops. Classes can have empty method bodies.

**Principle: stub the interface, not the implementation.**

```cpp
// tests/stubs/Arduino.h  — the ENTIRE stub is 30 lines

#pragma once
#include <stdint.h>
#include <cstddef>
#include <string>

#define INPUT   0
#define OUTPUT  1
#define HIGH    1
#define LOW     0

inline void pinMode(uint8_t, uint8_t) {}       // no-op
inline void digitalWrite(uint8_t, uint8_t) {}  // no-op
inline unsigned long millis() { return 0; }    // always returns 0 in tests

struct HardwareSerial {
    void begin(long) {}
    template<typename T> void print(T) {}
    template<typename T> void println(T) {}
    void println() {}
};
inline HardwareSerial Serial;  // discard all serial output
```

The same principle applies to `Wire.h` (I2C):

```cpp
// tests/stubs/Wire.h — all I2C calls silently succeed

class TwoWire {
public:
    void begin() {}
    void beginTransmission(uint8_t) {}
    uint8_t endTransmission(bool = true) { return 0; }  // pretend success
    uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
    int read() { return 0; }
    int available() { return 0; }
};
inline TwoWire Wire;
```

And for the Pico SDK's spinlock API (`pico/sync.h`), which `ParameterManager` uses for
thread-safety:

```cpp
// tests/stubs/pico/sync.h — no real locking needed in single-threaded tests

typedef void spin_lock_t;
inline spin_lock_t* spin_lock_init(unsigned int) { return nullptr; }
inline uint32_t spin_lock_blocking(spin_lock_t*) { return 0; }
inline void spin_unlock(spin_lock_t*, uint32_t) {}
```

In a single-threaded test environment, no actual synchronisation is needed. The no-op stubs mean
`ParameterManager::getValue()` and `setValue()` work correctly without the hardware spinlocks.

### A note on stub directory structure

The stub filenames and paths must exactly mirror the real headers:

```
tests/stubs/
├── Arduino.h          mirrors  <Arduino.h>
├── Wire.h             mirrors  <Wire.h>
├── pico/
│   └── sync.h         mirrors  <pico/sync.h>
└── hardware/
    └── gpio.h         mirrors  <hardware/gpio.h>
```

If `ParameterManager.h` contains `#include "pico/sync.h"`, the stub must be at
`tests/stubs/pico/sync.h` — the subdirectory name matters.

---

## Step 5: Define external symbols

Some files declare `extern` symbols that are defined elsewhere in the full firmware:

```cpp
// Sequencer.cpp
extern bool slideMode;

// ParameterManager.cpp (via AS5600Manager.h)
extern const size_t MAX_DELAY_SAMPLES;
```

In the full firmware these are defined in `Pico2Seq.ino` or `audio.cpp`. In the test build those
files are not compiled. The linker will error if these symbols are missing.

The fix is to define them once in a test helper file:

```cpp
// tests/unit/test_helpers.cpp
#include <cstddef>

bool slideMode = false;
const size_t MAX_DELAY_SAMPLES = 48000;
```

**Important:** define each symbol in exactly one translation unit. If you define `slideMode` in
both `test_sequencer.cpp` and `test_voice.cpp`, the linker will complain about "multiple
definitions". This is why `test_helpers.cpp` exists as a dedicated file.

---

## Step 6: Write the tests — from easiest to hardest

A good rule: test the most independent modules first. If a lower-level module has a bug, tests for
higher-level modules that depend on it will also fail, making the failure harder to diagnose.

### DSP tests (`test_dsp.cpp`) — the easiest

DSP classes like `Adsr` and `LadderFilter` have clean `Init()` / `Process()` interfaces and no
external state. They are pure functions of their inputs over time.

Useful things to test for DSP:

```
1. Initial state is correct (ADSR starts in IDLE, not ATTACK)
2. Output is bounded ([0.0, 1.0] for an envelope, [-1.0, 1.0] for an oscillator)
3. Known inputs produce known outputs (MIDI note 69 → 440 Hz)
4. The algorithm actually does something (low-pass filter attenuates high frequencies)
```

That last point is important: test the *behaviour*, not just that the code doesn't crash.

```cpp
TEST_CASE("Ladder filter attenuates above cutoff in LP24 mode", "[ladder]") {
    daisysp::LadderFilter filt;
    filt.Init(48000.0f);
    filt.SetFreq(200.0f);  // cutoff at 200 Hz
    filt.SetFilterMode(daisysp::LadderFilter::FilterMode::LP24);

    float sum_sq = 0.0f;
    const int N = 2048;
    for (int i = 0; i < N; ++i) {
        // Feed an 8 kHz signal — well above the 200 Hz cutoff
        float s = std::sin(2.0f * 3.14159f * 8000.0f * i / 48000.0f);
        float out = filt.Process(s);
        if (i > 512) sum_sq += out * out;  // skip filter transient
    }
    float rms = std::sqrt(sum_sq / (N - 512));
    REQUIRE(rms < 0.1f);  // should be heavily attenuated
}
```

This test would catch a broken filter coefficient calculation, a sign error in the feedback loop,
or an accidentally bypassed filter mode.

### Scale tests (`test_scales.cpp`) — data integrity

The scales are static lookup tables. The tests are simple invariant checks:

- Every scale starts at semitone 0 (the root note).
- Scale values never decrease (they're ordered lowest to highest).
- All values fit within MIDI range.
- The chromatic scale has consecutive semitones.

These tests are fast to write and catch the most common data-entry mistakes.

### Sequencer tests (`test_sequencer.cpp`) — state machine logic

The sequencer has several independently testable sub-components. Testing them bottom-up:

**`ParameterTrack<N>`** is a template class for storing step automation values. Test it directly
without constructing a full `Sequencer`:

```cpp
TEST_CASE("ParameterTrack wraps step index at currentStepCount", "[paramtrack]") {
    ParameterTrack<16> track;
    track.init(0.0f);
    track.currentStepCount = 4;  // 4-step sequence
    track.setValue(0, 1.0f);
    REQUIRE(track.getValue(4) == 1.0f);  // step 4 wraps to step 0
}
```

**`NoteDurationTracker`** is a simple counter. Test that it activates, counts down, and deactivates:

```cpp
TEST_CASE("NoteDurationTracker deactivates after counter reaches zero", "[duration]") {
    NoteDurationTracker t;
    t.start(3);
    t.tick(); t.tick(); t.tick();
    REQUIRE_FALSE(t.isActive());
}
```

**`Sequencer`** itself: test the public API — start/stop, step count configuration, `toggleStep`,
`setStepParameterValue`.

### Voice tests (`test_voice.cpp`) — the deepest layer

The `Voice` class is the most complex. A few things to know when testing it:

**`updateParameters()` is staged.** It doesn't apply changes immediately — it stores them in
`stagedState_`. The parameters are only applied inside `process()`. So if you call
`updateParameters()` and then check `getState()`, you see the old state. You must call `process()`
first:

```cpp
v.updateParameters(vs);
v.process();  // ← applies the staged state
REQUIRE(v.getState().velocityLevel == 0.42f);
```

This was discovered when a test failed with the old value. The fix is to understand the deferred
update pattern used for thread-safety.

**Scale injection** is already designed for testing. The codebase comment in `Voice.h` says:
> "Voice no longer reads global scale variables directly. Instead, scale data is injected via
> setter methods... This reduces global-state coupling and makes the class easier to unit test."

So to test `Voice` with a known scale, inject it:

```cpp
Voice v(0, cfg);
v.setScaleTable(scale, SCALES_COUNT);  // inject the real scales
uint8_t scaleIdx = 0;
v.setCurrentScalePointer(&scaleIdx);   // tell it to use scale index 0
v.init(48000.0f);
```

Passing `nullptr` to both setters tests the chromatic fallback path.

---

## Step 7: Fix bugs uncovered by the build

Compiling with a strict host compiler often surfaces real bugs that an embedded-specific compiler
was silently accepting. This project had one:

### The `noexcept` mismatch in `Voice.h`

The `.cpp` file implemented several methods with `noexcept`:

```cpp
// Voice.cpp
float Voice::process() noexcept { ... }
float Voice::calculateNoteFrequency(float, int8_t, int) noexcept { ... }
static void Voice::initFrequencyLookupTable() noexcept { ... }
```

But `Voice.h` declared them *without* `noexcept`:

```cpp
// Voice.h (before fix)
float process();
float calculateNoteFrequency(float note, int8_t octaveOffset, int harmony);
static void initFrequencyLookupTable();
```

In C++, the declaration and definition must agree on `noexcept`. Arduino's GCC was accepting this
silently; GCC 13 on Linux correctly rejects it as an error.

**The fix** is straightforward — add `noexcept` to the declarations in `Voice.h`. This is actually
the correct direction: the `.cpp` file states the stronger guarantee (`noexcept`), and the header
should expose that guarantee to callers. Without it, any code that catches exceptions from these
methods can never actually catch one — a small logical inconsistency.

---

## Step 8: Set up CI

A test that only runs locally is half a test. GitHub Actions runs the tests on every push:

```yaml
# .github/workflows/tests.yml
name: Tests
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: cmake -B build -DCMAKE_BUILD_TYPE=Debug
      - run: cmake --build build --parallel
      - run: ./build/tests/pico2seq_tests --reporter console
```

This catches regressions automatically. When you add new code that breaks a DSP algorithm or
changes a scale table, the CI run fails and you know immediately.

---

## The overall mental model

Here is the decision process in a single diagram:

```
For each source file, ask:
├── Does it #include any hardware header?
│   ├── NO  → Test it directly, no stubs needed
│   └── YES → Can I stub the hardware header?
│       ├── YES (Arduino.h, Wire.h, pico/sync.h) → Stub it, test the logic
│       └── NO  (PIO assembly, DMA, USB stack)   → Skip unit tests; test on hardware
```

Then for each thing you want to test, ask:

```
Is it:
├── A pure function?              → Test inputs → outputs directly
├── A state machine?              → Test transitions, not just individual states
├── A data structure / algorithm? → Test invariants, edge cases, and wrapping behavior
└── A hardware driver?            → Skip; write integration tests on real hardware
```

---

## How to add more tests

### Adding tests for a new module

1. Check its `#include` chain. Does it need any new stubs?
2. If yes, add a minimal stub file to `tests/stubs/` (mirroring the real path).
3. Add the new source file(s) to `tests/CMakeLists.txt` under `add_executable`.
4. Create `tests/unit/test_yourmodule.cpp` and write the tests.
5. Run `cmake --build build_test && ./build_test/tests/pico2seq_tests`.

### Adding tests for a module that uses globals

If a source file uses `extern` globals defined elsewhere:
1. Add them to `tests/unit/test_helpers.cpp` (not to individual test files).
2. Make sure each symbol is defined exactly once.

### Good test-to-write targets in this project

These modules currently have no tests and would benefit from them:

| Module | What to test |
|--------|-------------|
| `src/voice/VoiceManager.cpp` | `addVoice()`, `processAllVoices()`, preset application |
| `src/sequencer/Sequencer.cpp` — `advanceStep()` | Polyrhythmic step advancement with mock button states |
| `src/dsp/tremolo.cpp` | Output modulation depth, frequency tracking |
| `src/dsp/compressor.cpp` | Gain reduction on loud signals |
| `src/voice/VoicePresets.cpp` | Preset parameter ranges stay valid |

---

## Quick reference

### Run tests locally

```bash
cmake -B build_test -DCMAKE_BUILD_TYPE=Debug
cmake --build build_test --parallel
./build_test/tests/pico2seq_tests
```

### Run a specific test group

```bash
./build_test/tests/pico2seq_tests "[adsr]"
./build_test/tests/pico2seq_tests "[sequencer]"
./build_test/tests/pico2seq_tests "[voice]"
```

### List all tests without running them

```bash
./build_test/tests/pico2seq_tests --list-tests
```

### Test output with full detail on failures

```bash
./build_test/tests/pico2seq_tests --reporter console -s
```

### Arduino IDE build (unchanged)

Open `Pico2Seq.ino` in Arduino IDE and build/upload as normal. CMake files are invisible to it.
