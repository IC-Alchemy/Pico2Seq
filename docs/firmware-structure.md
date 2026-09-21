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
| Step playback, voice-state publication and live recording | `src/app/StepPlayback.h/.cpp` |
| Voice creation, preset application and track seeding | `src/app/VoiceSetup.h/.cpp` |
| I2S buffers, stereo output and final-mix gain | `src/app/AudioEngine.h/.cpp` |
| Voice Editing mode (parameter catalogue, editor transport) | `src/app/VoiceEditor.h/.cpp`, `src/voice/VoiceEditParameters.h/.cpp`, `src/ui/VoiceEditControls.h` |
| Float-to-DAC sample conversion | `src/app/Pcm16.h` |
| A button, fader or encoder action | Existing `src/ui/` and `src/sensors/` modules |
| OLED screens or LED colours | Existing `src/OLED/` and `src/LEDMatrix/` modules |
| A preset or its eight musical controls | `src/voice/presets/PresetBank.h`, `src/voice/VoiceParameters.h/.cpp` |
| Step lengths, scales or sequencer rules | Portable `src/pico2seq-core/` |

The app modules connect existing subsystems. USB is TinyUSB CDC-only; the
firmware has no MIDI transport or dormant MIDI tracker. Optional MIDI callbacks
remain in the portable sequencer for other applications. See [MIDI status](midi.md).

Sketch-level persistence lives in `src/app/Session*` (capture/apply, save/load
requests), `src/app/SessionStorage*` (LittleFS file I/O), and
`src/app/RetainedSession*` (retained-RAM mirror). The byte-level snapshot
format and codecs are portable code in `src/pico2seq-core/persistence/` (plus
`src/voice/PatchCodec.*`). See `docs/architecture.md` for the save policy.

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

`processSequencerStep()` advances all four voices in order, applies all four
encoder offsets, then stages voice updates. Only the selected voice receives
hand-distance input. All four voices honor GateLength through their sequencer's
note-duration state. `Sequencer::tickNoteDuration()` is the sole duration
authority; expiry in `processPendingGateTicks()` publishes a mid-step gate-off
`VoiceState` to `VoiceManager`. `VoiceSystem` holds only IDs and control
snapshots, with no parallel gate flags, timers or MIDI lifecycle tracking.
The clock step still uses the sequencer API's existing width and wrap rules.

Recording a Note requires a high Gate on the edited step. Immediate audio
feedback applies only to the currently playing step. Distance readings
55..700 mm are rebased by 55 mm, then normalized by the `MIN/MAX_DISTANCE_HEIGHT_MM`
span (645 mm) in `AppState::PerformanceInput` — the constants live in
`src/sensors/SensorConstants.h`. Readings within `EDGE_TOLERANCE_MM` of the
window clamp to its edge; invalid or further readings clear `handPresent`, and
recording then leaves steps unchanged. Regression tests pin this calibration.

## Ownership and real-time rules

`AppState.cpp` defines the existing `uiState`, `seq1`..`seq4`, `voiceManager`,
`voiceSystem`, scale and transport symbols once. Their types and public entry
points remain compatible with existing callers. `AppState::sequencers` is an
immutable routing table of borrowed pointers in voice order; concrete
`seq1`..`seq4` construction is retained. `OLEDDisplay::update()` and
`updateStepLEDs()` borrow this table via `Sequencer *const *` and a `size_t`
count, rather than accepting four sequencer references.

UI flags remain in `UIState`: `selectedVoiceIndex` owns voice selection, while
`isPresetSelection()` and `isVoiceParameterSettings()` derive settings views
from `settingsMode` and `currentSubMode`. The old `isVoice2Mode`,
`inPresetSelection` and `inVoiceParameterMode` mirrors are gone. Hardware
objects and refresh timestamps are private to `ControlIO`.

Core 0 allocates the voice collection once during setup and publishes
`voicesReady` with release ordering after initialization. Core 1 acquires it
before reading the manager or voice IDs; until then it fills silent buffers.
Do not add/remove voices, replace the manager or destroy these objects during
playback. Existing per-voice `SpscQueue<ControlUpdate, 8>` queues carry live
voice changes; their overload/coalescing policy is unchanged. OLED callbacks
run on Core 0 and borrow objects with program-long lifetimes.

Core 1 owns the I2S pool. Output remains 48 kHz, PCM16 stereo, 256 frames per
buffer. Four producer buffers pass directly to DMA, with no separate consumer
sample storage or buffer copying in the interrupt. This retains the previous
effective depth (three queued buffers plus one playing). Startup fills all four
before enabling I2S. The mono mix is converted once and copied to left and right. Conversion clamps, truncates
toward zero, then uses ARM `SSAT`; it does not round to nearest.

The existing blocking `take_audio_buffer(pool, true)` is the audio pacing
point. Rendering adds no heap allocation, serial output or locks. Keep bus,
display and logging calls away from this path. The two-second audio heartbeat
uses a four-entry SPSC queue: Core 1 snapshots IDs, a buffer counter, render
timing and DMA/PIO health; Core 0 prints the message. Full diagnostic queues drop new reports
without delaying sound. Reports can be delayed by Core 0 work.
See [audio performance](audio-performance.md) for counter meanings and hardware checks.

uClock's Core 0 ISR only stages steps and PPQN ticks. The step queue retains
16 usable entries and drops new steps when full. PPQN pending state is now
explicitly `volatile` for ISR visibility, but its **existing read/modify/write
race remains**: an interrupt between a decrement's load and store can lose a
tick. A separate counter-policy fix needs timing regression and bench tests.

The global delay effect was removed entirely (2026-09-11) — its DSP, defaults,
control globals, and the `src/FeatureConfig.h` switch are gone from the tree,
reclaiming the ~338 KiB the delay line would have reserved.

A redesigned master delay returned (2026-09-20): `MasterDelay`
(`src/voice/MasterDelay.h`) rides the summed block inside
`VoiceManager::processBlock()` — a 48,000-float (1.0 s, ~187.5 KiB) rpdsp
`DelayLine` owned by the heap-allocated `VoiceManager`, read fractionally
with cubic interpolation, with a DC blocker + one-pole lowpass + tanh bound
in the feedback loop. Core 0 publishes mix and delay time through lock-free
`std::atomic<float>` targets on `VoiceManager`; the audio core reads them
once per block and eases per sample. Fader 2 is the wet mix; Shift + fader 2
is the delay time (10 ms–1 s).

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
cover voices/queues, sequencing, control-surface logic, tile protocol and the
Voice Editing mode (`test_voice_edit.cpp`).
Neither host tests nor compilation verify physical controls, bus timing,
I2S timing or sound. See [testing.md](testing.md).

Before a performance, check cold/warm boot; play/stop and long runs across
step wrap; all four voice selections and preset changes; gate/retrigger/slide
and release tails; pads, faders, encoder and lidar recording; displays/LEDs;
and audio under heavy control activity. Check USB remains CDC-only and no
gate writes reach the I2S pins.
