# Finding your way around the firmware

Start with [Pico2Seq.ino](../Pico2Seq.ino). Its four Arduino entry points show
the division of work: Core 0 runs the controls; Core 1 renders the sound.
[Application.cpp](../src/app/Application.cpp) shows startup and the order of
work during each control-loop pass.

## Where to make a change

| You want to change... | Start here |
|---|---|
| Startup or control-loop order | `src/app/Application.cpp` |
| Bus, I2S or mode-switch pins | `src/app/HardwarePins.h` (LED pin remains in `src/LEDMatrix/ledMatrix.h`) |
| Sensor startup, polling or display cadence | `src/app/ControlIO.cpp` |
| Shared objects and hand-distance calibration | `src/app/AppState.h/.cpp` |
| Clock registration, transport and queued clock events | `src/app/ClockService.h/.cpp` |
| Step playback, software gates and live recording | `src/app/StepPlayback.h/.cpp` |
| Voice creation, preset application and track seeding | `src/app/VoiceSetup.h/.cpp` |
| I2S buffers, stereo output and optional global delay | `src/app/AudioEngine.h/.cpp` |
| Float-to-DAC sample conversion | `src/app/Pcm16.h` |
| A button, fader or encoder action | Existing `src/ui/` and `src/sensors/` modules |
| OLED screens or LED colours | Existing `src/OLED/` and `src/LEDMatrix/` modules |
| A preset or its eight musical controls | `src/voice/presets/PresetBank.h`, `src/voice/VoiceParameters.h/.cpp` |
| Step lengths, scales or sequencer rules | Portable `src/pico2seq-core/` |

The app modules connect existing subsystems. USB MIDI is disabled in this
checkout — there is no MIDI input path either (`MidiManager` reads nothing; USB
carries power and the CDC console only). `MidiNoteManager` compatibility calls
still participate in software gate/note bookkeeping; changing them needs a
separate musical-behavior review. There is no sketch-level persistence service.

## The order matters

Startup retains the original order: boot diagnostics, 100 ms stabilization,
serial, main I2C bus and watchdog, LEDs, distance sensor, encoder, touch sensor,
OLED, voice creation, OLED observer, matrix scan, tile bus and event handler,
then uClock at 90 BPM / 480 PPQN. Shuffle is enabled after starting the clock.
Missing MPR121 hardware retains the watchdog-driven restart path.

Each `Application::update()` pass:

1. Retries pending voice controls and snapshots the scale on Core 0.
2. Reads `millis()` once and polls held buttons.
3. Drains queued steps, prints diagnostics, then drains gate ticks.
4. Runs the due control scan: pads, tiles, encoder, distance, step recording.
5. Runs the due display refresh: voice-switch notice, step LEDs, OLED, LED show.

Controls are due every 1 ms; displays every 20 ms (50 Hz). These are minimum
intervals, not deadlines or catch-up loops. Slow bus/display work can extend a
pass. Unsigned subtraction preserves timer-wrap behavior.

`processSequencerStep()` (`src/app/StepPlayback.cpp`) advances all four voices in
order, applies all four
encoder offsets, then stages voice updates. Only the selected voice receives
hand-distance input. Voices 1/2 keep software gate timers; voices 3/4 retain
their audio-only path (indices 0/1 and 2/3 in code). The clock step still goes
through the existing sequencer API with its existing width and wrap rules.

Recording a Note requires a high Gate on the edited step. Immediate audio
feedback applies only to the currently playing step. Distance readings
74..1400 mm are rebased by 74 mm, then normalized by **1400**, not 1326.
Invalid readings become zero. Regression tests pin this calibration.

## Ownership and real-time rules

`AppState.cpp` defines the existing `uiState`, `seq1`..`seq4`, `voiceManager`,
`voiceSystem`, scale and transport symbols once. Their types and public entry
points remain compatible with existing callers. `AppState::sequencers` is an
immutable table of borrowed pointers in voice order. UI flags remain in
`UIState`; hardware objects and refresh timestamps are private to `ControlIO`.

Core 0 allocates the voice collection once during setup and publishes
`voicesReady` with release ordering after initialization. Core 1 acquires it
before reading the manager or voice IDs; until then it fills silent buffers.
Do not add/remove voices, replace the manager or destroy these objects during
playback. Existing per-voice `SpscQueue<ControlUpdate, 8>` queues carry live
voice changes; their overload/coalescing policy is unchanged. OLED callbacks
run on Core 0 and borrow objects with program-long lifetimes.

Core 1 owns the I2S pool. Output remains 48 kHz, PCM16 stereo, 256 frames per
buffer, three producer buffers and four consumer buffers. The mono mix is
converted once and copied to left and right. Conversion clamps, truncates
toward zero, then uses ARM `SSAT`; it does not round to nearest.

The existing blocking `take_audio_buffer(pool, true)` is the audio pacing
point. Rendering adds no heap allocation, serial output or locks. Keep bus,
display and logging calls away from this path. The two-second audio heartbeat
uses a four-entry SPSC queue: Core 1 snapshots IDs and a buffer counter;
Core 0 prints the existing message. Full diagnostic queues drop new reports
without delaying sound. Reports can be delayed by Core 0 work.

uClock's Core 0 ISR only stages steps and PPQN ticks (all in
`src/app/ClockService.cpp`). The step queue retains
16 usable entries and drops new steps when full. PPQN pending state is now
explicitly `volatile` for ISR visibility, but its **existing read/modify/write
race remains**: an interrupt between a decrement's load and store can lose a
tick. A separate counter-policy fix (`src/utils/PendingTickCounter.h` — lock-free
`post()`/`takeAll()`, already host-tested under `[ppqn]` and
`pico2seq_clock_tests`) needs timing regression and bench tests before wiring in.

The global delay remains disabled by `src/FeatureConfig.h`. Its optional DSP,
defaults and control globals retain their original behavior. When enabled,
its live delay/on/feedback controls still need a proper cross-core handoff;
they are not protected by voice queues. Its ~338 KiB delay line also needs a
complete runtime RAM budget before enabling it on hardware. This refactor
does not claim the optional path is race-free.

## Building and checking changes

Arduino recursively compiles `.cpp` files under `src/app/`; no source list or
new library installation is needed. Distribute the entire sketch directory,
including initialized submodules. The `.h` extensions work with the existing
staging script. Host CMake tests remain separate from the hardware build.

On this Windows setup:

```powershell
cmake -S . -B build_test -G Ninja '-DCMAKE_CXX_COMPILER=C:/Program Files/LLVM/bin/clang++.exe' '-DCMAKE_C_COMPILER=C:/Program Files/LLVM/bin/clang.exe' -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-D_USE_MATH_DEFINES
cmake --build build_test --parallel 8
.\build_test\tests\pico2seq_tests.exe --reporter console
ctest --test-dir build_test --output-on-failure
.\scripts\build_pico2seq.ps1 -KeepStage
```

`[app]` tests cover PCM conversion and distance calibration. Existing tests
cover voices/queues, sequencing, control-surface logic, the PPQN counter policy
(`[ppqn]` tag, plus the standalone `pico2seq_clock_tests` target) and tile
protocol. Neither host tests nor compilation verify physical controls, bus
timing, I2S timing or sound. See [testing.md](testing.md).

Before a performance, check cold/warm boot; play/stop and long runs across
step wrap; all four voice selections and preset changes; gate/retrigger/slide
and release tails; pads, faders, encoder and lidar recording; displays/LEDs;
and audio under heavy control activity. Check USB remains CDC-only and no
gate writes reach the I2S pins.
