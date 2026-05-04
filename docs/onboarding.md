# Pico2Seq Onboarding

Pico2Seq is an Arduino/RP2350 firmware project for a four-voice polyphonic step sequencer. It combines real-time I2S audio, a polymetric sequencer, USB MIDI, an OLED, an 8x8 WS2812B LED matrix, an MPR121 touch matrix, an AS5600 magnetic encoder, and a VL53L1X distance sensor.

## Current State

- Primary entry point: `Pico2Seq.ino`.
- Build target: Raspberry Pi Pico2 / RP2350 using the Arduino RP2040 core.
- Source shape: domain modules under `src/`, with `includes.h` acting as the Arduino-friendly header aggregator.
- Automated tests: none found. `src/test/TestHarness.h` exists, but there is no configured test runner or CI workflow.
- Local build artifacts exist under `build/`; treat them as generated output.
- Working tree note from onboarding pass: `.vscode/settings.json` already had local modifications before this file was added.

## Setup

Use Arduino IDE or `arduino-cli`. The checked-in VS Code Arduino config points at:

- Board: `rp2040:rp2040:rpipico2`
- Sketch: `Pico2Seq.ino`
- Port: `COM3`
- Output directory: `build`
- Extra flags: `-ffast-math`

Install the Arduino libraries listed in `README.md`:

- `FastLED`
- `Melopero_VL53L1X`
- `Adafruit_MPR121`
- `MIDI Library`
- `Adafruit_TinyUSB`
- `Adafruit_GFX`
- `Adafruit_SH110X`
- `OneButton`
- `uClock`

The DSP code is vendored in `src/dsp/`. Do not replace it with upstream DaisySP or DaisyDuino without checking the local API surface first.

## Verify Build

Arduino IDE path:

1. Open `Pico2Seq.ino`.
2. Select Raspberry Pi Pico2 / RP2350.
3. Install the libraries above.
4. Verify compile.
5. Upload to the Pico2 and watch serial initialization output.

CLI path used during this onboarding pass:

```powershell
arduino-cli compile --fqbn "rp2040:rp2040:rpipico2:flash=4194304_0,arch=arm,freq=225,opt=Optimize3,profile=Disabled,rtti=Disabled,stackprotect=Disabled,exceptions=Disabled,dbgport=Disabled,dbglvl=None,usbstack=tinyusb,ipbtstack=ipv4only,uploadmethod=default" --build-path "build" --warnings default "Z:\Codezzz\Pico2Seq"
```

As of this pass, the compile reaches link and then fails on unresolved symbols:

- `feedbackAmmount` is referenced by `src/sensors/AS5600Manager.cpp` and declared in `src/sensors/AS5600Manager.h`, while `Pico2Seq.ino` defines `feedbackAmount`.
- `VoicePresets::voiceSystem` and `VoicePresets::voiceManager` are referenced from `src/voice/VoicePresets.cpp`; the `extern` declarations are inside the `VoicePresets` namespace, so they do not resolve to the globals in `Pico2Seq.ino`.

The compile also reports expected non-fatal library and platform warnings, including FastLED forcing software SPI, deprecated LED constants, and rp2040/lwIP warnings.

## Hardware Map

Important pins and I2C addresses:

- I2S audio: GP15 data, GP16 BCK, GP17 LRCK.
- Touch matrix: rows GP0-GP3, columns GP4-GP11.
- LED matrix data: GP1.
- I2C bus: GP12 SDA, GP13 SCL.
- OLED: `0x3C`.
- AS5600: `0x36`.
- VL53L1X: `0x29`.

Check pin conflicts carefully. The docs currently list GP1 both as part of the touch matrix row set and as LED matrix data.

## Runtime Architecture

The firmware uses the Pico dual-core model:

- Core 0: audio setup and `loop()` real-time audio processing.
- Core 1: `setup1()` and `loop1()` for MIDI, touch matrix, sensors, OLED, LED matrix, and UI state.

Audio uses static/preallocated buffers:

- `SAMPLE_RATE = 48000.0f`
- `SAMPLES_PER_BUFFER = 256`
- `NUM_AUDIO_BUFFERS = 3`
- `audio_buffer_pool_t *producer_pool`

Avoid heap allocation and blocking work in the audio path. Do not mutate shared audio state from the UI side without considering inter-core visibility and timing.

## Key Modules

- `Pico2Seq.ino`: global system initialization, dual-core entry points, audio buffer fill, step update wiring.
- `src/audio/`: I2S and audio buffer plumbing.
- `src/dsp/`: local DSP building blocks including oscillators, filters, envelopes, delay, overdrive, wavefolder, and related utilities.
- `src/voice/`: voice configuration, presets, voice lifecycle, and `VoiceSystem`.
- `src/sequencer/`: parameter tracks, step definitions, gate timers, shuffle templates, and sequencer logic.
- `src/ui/`: centralized UI state plus button/event routing.
- `src/matrix/`: MPR121 touch matrix scanning.
- `src/sensors/`: AS5600 and VL53L1X integration.
- `src/OLED/`: SH1106G display rendering.
- `src/LEDMatrix/`: FastLED display and step/parameter feedback.
- `src/midi/`: USB MIDI and CC mapping.

## Non-Obvious Rules

- Step parameter edits are ignored if the step gate is low. Check `updateParametersForStep()` before changing edit behavior.
- UI voice numbers are 1 through 4; internal voice indexes are zero-based.
- `VoiceSystem::MAX_VOICES` is 4, but current gate state storage only covers voices 0 and 1.
- `SequencerConstants::MAX_STEPS_COUNT` is 64; the default is 16.
- `CORE_PARAMETERS` order must match the `ParamId` enum in `src/sequencer/SequencerDefs.h`.
- Documentation mentions `.clang-format` and `.editorconfig`, but neither file was present at repo root during this pass.

## Useful Docs

- `README.md`: setup, hardware, usage, and high-level architecture.
- `docs/architecture.md`: module map and data flow.
- `docs/voice.md` and `docs/VoiceSystem.md`: voice architecture.
- `docs/sequencer.md`: step sequencer behavior.
- `docs/matrix.md`: touch matrix details.
- `docs/sensors.md`: AS5600 and VL53L1X behavior.
- `docs/LEDMatrix.md`, `docs/oled.md`, `docs/midi.md`: output and integration subsystems.

## First Tasks For A New Contributor

1. Fix the current link errors and re-run the Arduino verify command.
2. Confirm the GP1 hardware conflict in docs or wiring.
3. Decide whether root formatting files should be restored or remove stale references to them.
4. Add a narrow host-buildable test target for pure logic modules such as sequencer parameter tracks before touching audio or hardware code.
