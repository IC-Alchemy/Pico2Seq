# Pico2Seq Onboarding

Start with the sketch. Then follow the signal.

Pico2Seq is Arduino firmware for a Raspberry Pi Pico2 / RP2350 four-voice synth sequencer. It combines I2S audio, USB MIDI, a polymetric step engine, a 4x8 MPR121 touch matrix, an AS5600 magnetic encoder, a VL53L1X time-of-flight distance sensor, an SH1106G OLED, and an 8x8 WS2812B LED matrix.

## Current State

- Entry point: `Pico2Seq.ino`.
- Build target: Raspberry Pi Pico2 / RP2350 with the Arduino RP2040 core.
- Source layout: domain modules under `src/`.
- Header entry: `includes.h` aggregates project headers for Arduino compatibility.
- Audio: 48 kHz I2S with preallocated Pico audio buffers.
- Tests: no automated test runner or CI workflow is configured.
- Generated output: `build/`.

The current checkout already has local edits outside these docs. Do not assume a clean tree.

## Setup

Use Arduino IDE or `arduino-cli`.

The checked-in VS Code Arduino config points at:

- Board: `rp2040:rp2040:rpipico2`
- Sketch: `Pico2Seq.ino`
- Port: `COM3`
- Output directory: `build`
- Extra build flag: `-ffast-math`

Install the Arduino libraries listed in `README.md`:

- `Adafruit_MPR121`
- `Adafruit_SH110X`
- `Adafruit_TinyUSB`
- `FastLED`
- `Melopero_VL53L1X`
- `MIDI Library`
- `OneButton`
- `uClock`

The DSP code is vendored in `src/dsp/`. Do not replace it with upstream DaisySP or DaisyDuino unless you are doing an intentional API port.

## Verify Build

Arduino IDE path:

1. Open `Pico2Seq.ino`.
2. Select Raspberry Pi Pico2 / RP2350.
3. Install the required libraries.
4. Verify the sketch.
5. Upload to hardware.
6. Open the serial monitor for startup diagnostics.

CLI path used for this repo:

```powershell
arduino-cli compile --fqbn "rp2040:rp2040:rpipico2:flash=4194304_0,arch=arm,freq=225,opt=Optimize3,profile=Disabled,rtti=Disabled,stackprotect=Disabled,exceptions=Disabled,dbgport=Disabled,dbglvl=None,usbstack=tinyusb,ipbtstack=ipv4only,uploadmethod=default" --build-path "build" --warnings default "Z:\Codezzz\Pico2Seq"
```

FastLED and the RP2040 platform may print warnings during compile. Treat warnings as signal, but do not confuse expected library/platform warnings with link failures.

An older onboarding note recorded link failures around `feedbackAmmount` and `VoicePresets::voiceSystem` / `VoicePresets::voiceManager`. In the current source, the sketch defines `feedbackAmount`, and `src/voice/VoicePresets.cpp` declares `voiceManager` and `voiceSystem` outside the `VoicePresets` namespace. Re-run the compile command after code changes instead of relying on the old failure state.

## Hardware Map

Important pins and addresses:

- I2S audio: GP15 data, GP16 BCK, GP17 LRCK.
- Touch matrix: GP0-GP3 rows, GP4-GP11 columns.
- LED matrix data: GP1.
- I2C bus: GP12 SDA, GP13 SCL.
- OLED: `0x3C`.
- AS5600: `0x36`.
- VL53L1X: `0x29`.

The docs currently list GP1 both as a touch matrix row and as WS2812B LED data. Check the active hardware revision before wiring or changing pin definitions.

## Runtime Architecture

The firmware uses the Pico dual-core model.

Core 0:

- `setup()`
- `loop()`
- audio synthesis
- I2S buffer fill

Core 1:

- `setup1()`
- `loop1()`
- MIDI
- touch matrix
- sensors
- OLED
- LED matrix
- UI state

Audio constants in the sketch:

- `SAMPLE_RATE = 48000.0f`
- `SAMPLES_PER_BUFFER = 256`
- `NUM_AUDIO_BUFFERS = 3`
- `audio_buffer_pool_t *producer_pool`

Avoid heap allocation and blocking work in the audio path. Do not mutate shared audio state from the UI side without considering timing and inter-core visibility.

## Key Modules

- `Pico2Seq.ino` - global initialization, dual-core entry points, audio buffer fill, step update wiring.
- `src/audio/` - I2S and audio buffer plumbing.
- `src/dsp/` - local DSP blocks: oscillators, filters, envelopes, delay, overdrive, wavefolder, and related utilities.
- `src/voice/` - voice configuration, presets, voice lifecycle, and `VoiceSystem`.
- `src/sequencer/` - parameter tracks, step definitions, gate timers, shuffle templates, and sequencing.
- `src/ui/` - centralized UI state and button/event routing.
- `src/matrix/` - MPR121 touch matrix scanning.
- `src/sensors/` - AS5600 and VL53L1X integration.
- `src/OLED/` - SH1106G display rendering.
- `src/LEDMatrix/` - FastLED display and step/parameter feedback.
- `src/midi/` - USB MIDI and CC mapping.

## Non-Obvious Rules

- Step note edits are ignored if the step Gate parameter is low. See `updateParametersForStep()`.
- UI voice numbers are 1 through 4. Internal voice indexes are zero-based.
- `VoiceManager` voice ids are not UI indexes. Use `voiceSystem.getVoiceId()` or `getVoiceIdFromUIIndex()` deliberately.
- `VoiceSystem::MAX_VOICES` is 4.
- Current gate state storage covers voices 0 and 1.
- `SequencerConstants::MAX_STEPS_COUNT` is 64. The default is 16.
- `CORE_PARAMETERS` order must match the `ParamId` enum in `src/sequencer/SequencerDefs.h`.
- The root currently does not contain `.clang-format` or `.editorconfig`, even though older docs referenced them.

## Useful Docs

- `README.md` - setup, hardware, usage, and high-level architecture.
- `docs/architecture.md` - module map and data flow.
- `docs/voice.md` and `docs/VoiceSystem.md` - voice architecture.
- `docs/sequencer.md` - step sequencer behavior.
- `docs/matrix.md` - touch matrix details.
- `docs/sensors.md` - AS5600 and VL53L1X behavior.
- `docs/LEDMatrix.md` - LED feedback.
- `docs/oled.md` - OLED behavior.
- `docs/midi.md` - MIDI behavior.

## First Tasks For A New Contributor

1. Run the Arduino verify command.
2. Confirm the GP1 hardware conflict against the active board revision.
3. Add a narrow host-buildable test target for pure sequencer logic.
4. Decide whether formatting config should be restored or stale references removed.
5. Touch audio and cross-core state only after reading the current call order in `onStepCallback()` and `loop1()`.
