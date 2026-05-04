# Pico2Seq Architecture

Gesture first. Clock underneath.

Pico2Seq is Arduino firmware for a Raspberry Pi Pico2 / RP2350 synth sequencer. It runs four sequencers, four voices, USB MIDI, an OLED, an LED matrix, a capacitive touch matrix, an AS5600 magnetic encoder, and a VL53L1X time-of-flight distance sensor.

The shape is practical. Audio stays on Core 0. UI and control work stay on Core 1. Shared state is small and explicit.

## Top Level

- `Pico2Seq.ino` - Arduino sketch entry point, dual-core setup, audio loop, UI loop, sequencer callback wiring.
- `includes.h` - Arduino-friendly header aggregator for project modules.
- `diagnostic.h` - global diagnostic flags and error bits.
- `src/` - firmware modules grouped by domain.
- `docs/` - subsystem notes and contributor context.

## Module Map

### Audio

`src/audio/` owns I2S setup, Pico audio buffers, and the audio callback path.

- `audio.h`, `audio_i2s.h`, `audio.cpp` - I2S and audio interface plumbing.
- `buffer.h` - audio buffer helpers.
- `sample_conversion.h` - sample conversion utilities.

Runtime constants in `Pico2Seq.ino`:

- `SAMPLE_RATE = 48000.0f`
- `SAMPLES_PER_BUFFER = 256`
- `NUM_AUDIO_BUFFERS = 3`
- `audio_buffer_pool_t *producer_pool`

### DSP

`src/dsp/` contains the local DSP code. It includes DaisySP-derived pieces adapted for this firmware.

Do not replace this folder with DaisyDuino or upstream DaisySP without checking the local API. The voice code depends on the vendored headers.

### Sequencer

`src/sequencer/` owns step data, parameter tracks, timing constants, and polymetric behavior.

- `Sequencer.h/.cpp` - main sequencer class.
- `SequencerDefs.h` - parameter ids, timing constants, `VoiceState`, `Step`, and `GateTimer`.
- `ParameterManager.h/.cpp` - parameter validation and management.
- `ShuffleTemplates.h` - shuffle timing templates.

Important constants:

- `SequencerConstants::MAX_STEPS_COUNT = 64`
- `SequencerConstants::DEFAULT_STEPS_COUNT = 16`
- `SequencerConstants::PULSES_PER_QUARTER_NOTE_PPQN = 480`
- `ParamId` order must match `CORE_PARAMETERS`.

### Voice

`src/voice/` owns voice state, voice rendering, voice lifecycle, and factory presets.

- `VoiceSystem.h` - central four-voice state container.
- `VoiceManager.h/.cpp` - voice lifecycle and `processAllVoices()`.
- `Voice.h/.cpp` - individual voice configuration and rendering.
- `VoicePresets.h/.cpp` - seven factory voice configurations.

`VoiceSystem::MAX_VOICES` is 4. Current gate arrays cover voices 0 and 1 only.

```cpp
struct VoiceSystem
{
    static constexpr uint8_t MAX_VOICES = 4;

    uint8_t voiceIds[MAX_VOICES] = {0, 0, 0, 0};
    VoiceState voiceStates[MAX_VOICES];

    volatile bool gates[2] = {false, false};
    GateTimer gateTimers[2];

    uint8_t getVoiceId(uint8_t voiceIndex) const;
    int16_t getVoiceIdFromUIIndex(int uiIndex) const;
    void setVoiceId(uint8_t voiceIndex, uint8_t voiceId);
    VoiceState &getVoiceState(uint8_t voiceIndex);
    volatile bool &getGate(uint8_t voiceIndex);
    GateTimer &getGateTimer(uint8_t voiceIndex);
    void stopAllGates();
    void tickAllGateTimers();
};
```

UI voice numbers are 1 through 4. Internal indexes are zero-based. Voice ids from `VoiceManager` are a separate mapping. Convert deliberately.

### UI

`src/ui/` owns UI state and event routing.

- `UIState.h` - central UI state structure.
- `UIEventHandler.h/.cpp` - matrix event routing and mode handling.
- `ButtonManager.h/.cpp` - button state and timing.
- `ButtonHandlers.h/.cpp` - button actions.
- `UIConstants.h` - button mappings and UI constants.

The UI writes sequencer parameters through handlers. Note edits are ignored when the selected step's Gate parameter is low. Check `updateParametersForStep()` before changing edit behavior.

### Hardware Interfaces

- `src/matrix/` - 4x8 capacitive touch matrix through MPR121.
- `src/LEDMatrix/` - 8x8 WS2812B feedback through FastLED.
- `src/OLED/` - 128x64 SH1106G display rendering.
- `src/midi/` - USB MIDI note and CC support.
- `src/sensors/` - AS5600 magnetic encoder and VL53L1X distance sensor.
- `src/scales/` - 13 built-in scale tables and helpers.
- `src/utils/` - debug and utility code.

## Runtime Flow

```text
Touch matrix / AS5600 / VL53L1X / MIDI
  -> UIState and event handlers
  -> four Sequencer instances
  -> VoiceState snapshots
  -> VoiceSystem and VoiceManager
  -> DSP voice rendering
  -> I2S audio output at 48 kHz

Sequencer state also feeds:
  -> USB MIDI notes and CC
  -> OLED parameter display
  -> LED matrix step and mode feedback
```

## Core Split

Core 0 handles audio.

- `setup()` initializes voices, global delay state, I2S format, and audio buffers.
- `loop()` takes an audio buffer, fills it, and returns it to the I2S system.
- `fill_audio_buffer()` calls `voiceManager->processAllVoices()` for each sample and writes mono-to-stereo 16-bit PCM.

Core 1 handles control.

- `setup1()` initializes MIDI, sensors, matrix, OLED, LED matrix, uClock, and event handlers.
- `loop1()` reads MIDI, processes pending PPQN ticks, scans controls, updates display/LEDs, and records selected step parameters.

Keep blocking work out of Core 0. Avoid heap allocation in the audio path. Treat Core 1 writes to audio-facing state as timing-sensitive.

## Sequencer Callback

`onStepCallback()` is the main timing bridge from uClock into the four sequencers.

The current order matters:

1. Store the current uClock step.
2. Advance all four sequencers into temporary `VoiceState` values.
3. Apply AS5600 base values and delay values.
4. Update synth state and store the resulting voice states.

Voice 0 and voice 1 also run gate timer and MIDI note handling. Voice 2 and voice 3 update synth state without that gate/MIDI branch.

## Hardware Map

- I2S audio: GP15 data, GP16 BCK, GP17 LRCK.
- Touch matrix: GP0-GP3 rows, GP4-GP11 columns.
- LED matrix data: GP1.
- I2C bus: GP12 SDA, GP13 SCL.
- OLED address: `0x3C`.
- AS5600 address: `0x36`.
- VL53L1X address: `0x29`.

The docs currently list GP1 for both a touch matrix row and WS2812B LED data. Check the active hardware revision before changing pins.

## Known Edges

- There is no configured automated test runner.
- `build/` is generated output.
- The root docs have referenced `.clang-format` and `.editorconfig`, but those files are not present in the current root.
- `src/test/TestHarness.h` exists, but it is not wired into CI or a host test command.

## Next Improvements

- Introduce an `AppContext` only if it reduces global-state coupling without hiding real-time boundaries.
- Add a narrow host-buildable test target for pure sequencer logic.
- Expand gate timer handling beyond voices 0 and 1 if voices 2 and 3 need the same MIDI/gate behavior.
- Reconcile the GP1 documentation conflict with the current hardware revision.
- Add a repeatable formatting/static-analysis command before documenting it as required.
