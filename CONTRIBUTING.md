# Contributing to Pico2Seq

Pico2Seq is Arduino firmware for the Raspberry Pi Pico 2 (RP2350), with a
portable C++17 core and host tests. Bug reports, documentation corrections,
tests, and focused firmware improvements are welcome.

Start with the [firmware guide](docs/firmware-structure.md) to find the right
module, or the [documentation index](docs/README.md) for a subsystem overview.
The [user manual](docs/manual.md) describes the instrument's controls.

## Set up a checkout

```bash
git clone --branch DeCluttered --recurse-submodules https://github.com/IC-Alchemy/Pico2Seq.git
cd Pico2Seq
git submodule update --init --recursive
```

Run the submodule command again after changing branches. Both `src/rpdsp` and
`src/VelocityEncoder` must match the commits recorded by the parent repository.
Review local submodule changes before updating; do not discard work to make a
build pass.

## Build and test on your computer

Host tests need CMake 3.16 or newer and a C++17 compiler. Ninja is optional.
CMake downloads the pinned Catch2 v3.5.2 dependency on the first configuration.
No Pico board or Arduino libraries are needed for these tests.

```bash
cmake -S . -B build_test_ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build_test_ninja --parallel 4
ctest --test-dir build_test_ninja --output-on-failure
```

To use Ninja explicitly, add `-G Ninja` on the first configure. Keep different
generators, compilers, and build variants in separate build directories.

For a focused check, run a Catch2 tag directly:

```bash
./build_test_ninja/tests/pico2seq_tests "[sequencer]"
./build_test_ninja/tests/pico2seq_tests "[voice_transfer]"
./build_test_ninja/tests/pico2seq_ui_tests
```

On Windows, append `.exe` when running a test executable directly.
The [testing guide](docs/testing.md) describes all eight executables, their
hardware stubs, and known baseline failures. Compare failed names and
assertions with the baseline; report existing failures separately from any
introduced by your change.

For documentation changes, also run:

```bash
python3 -B tests/verify_docs_links.py
```

This checks local links, heading anchors, and code fences; it does not check
remote URLs. Generated build trees and vendored dependency docs are excluded.

## Compile device firmware

CMake builds host tests only. Use Arduino IDE or Arduino CLI for the device
firmware, with the dependencies listed in the [README](README.md#prerequisites).
The standard target is **Raspberry Pi Pico 2, ARM, 225 MHz, Adafruit TinyUSB**.
Higher clocks are experiments that require an explicit choice.

The PowerShell helper stages a disposable sketch, excludes dependency examples,
and leaves the checkout unchanged:

```powershell
pwsh -NoProfile -File scripts/build_pico2seq.ps1 `
  -CpuMHz 225 `
  -BuildDirectory build/pico2seq-225
```

See the [README build instructions](README.md#fresh-github-clone-and-225-mhz-build)
for the complete board options. A firmware build is successful only when the
command exits with code 0 and produces UF2, ELF, BIN, and MAP artifacts. Keep the
ELF from the exact build when decoding a crash. Compilation does not establish
physical audio timing, sensor behavior, or successful flashing.

The 225 MHz configuration was compile-checked on Linux on 2026-10-06 with
Arduino-Pico 6.0.0 and the README's library versions, including FastLED 3.9.20.

## Firmware design rules

- Core 0 owns controls, sensors, USB CDC, displays, sequencing, and storage.
  Core 1 renders 48 kHz audio. Keep blocking I/O and heap allocation out of the
  audio and sequencer hot paths.
- Communicate across cores through the existing atomics and SPSC queues.
  Control setters publish updates; the audio core applies them. Voice tests
  must render enough samples to consume staged updates before asserting state.
- Keep `src/pico2seq-core` portable. Arduino, UI types, and hardware glue belong
  in `src/app` or the relevant hardware-facing module.
- Keep span buffers and mixer scratch in members or static storage. Core 1
  has a 2 KiB stack and renders spans of at most 32 samples.
- The audio core owns the master reverb. Publish control targets without
  clearing or reinitializing its tank during playback. HALF storage is the
  firmware default; see the [audio performance guide](docs/audio-performance.md).
- Under `-ffast-math`, use the existing bit-pattern finiteness checks rather
  than assuming `std::isfinite` will reject invalid values.
- Extend `UIState` for UI modes and interaction state. Internal voice indices
  are 0–3; panel labels are 1–4. Keep declarations and definitions consistent,
  including `noexcept`.
- DSP dependencies live in the `src/rpdsp` submodule. Make upstream dependency
  changes deliberately and record their updated pins in the parent repository.

The [architecture guide](docs/architecture.md) explains ownership and data flow.
The testing guide covers stub interfaces and the single-definition rule for
test globals. For changes to audio code placement, inspect the exact linked ELF
to confirm that the hot symbols remain in SRAM.

## Submit a change

Keep pull requests focused. Explain the problem, resulting behavior, relevant
tests, and any hardware checks performed. For bug reports, include the commit,
board/core/library versions, reproduction steps, and the exact failure output.
Include the matching ELF when a reported crash needs address decoding.

Update affected documentation and add regression coverage for changed behavior.
Keep source and documentation readable, with LF line endings and the surrounding
file's indentation. Preserve copyright notices and licenses in vendored code.

Build outputs, caches, editor settings, personal agent files, and firmware images
belong outside Git. Share firmware artifacts through releases, and put durable
project documentation in `docs/`. Shared formatting configuration and build
scripts remain trackable.
