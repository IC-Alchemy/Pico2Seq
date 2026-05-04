# Pico2Seq

Gesture belongs in the sequencer.

Pico2Seq is open firmware for a Raspberry Pi Pico2 / RP2350-based, four-voice synth sequencer. It pairs step programming with hand-controlled parameter movement: a touch matrix for gates and edits, an AS5600 magnetic encoder for per-step control, and a VL53L1X time-of-flight distance sensor for live modulation.

The project is built for embedded hardware, not a desktop runtime. Audio runs at 48kHz over I2S, the DSP code is vendored in `src/dsp/`, and the main sketch keeps audio and UI work separated across the Pico2 cores.

## What It Does

### Synthesis

- **4 independent voices** - each voice has its own oscillator, filter, envelope, and effects state.
- **Filter and drive stages** - ladder and state-variable filter code, plus overdrive and wavefolder processing.
- **ADSR envelopes** - configurable attack, decay, and release timing per voice.
- **Voice presets** - seven factory voice configurations, with room for custom voice setup.

### Sequencing

- **Polymetric parameter tracks** - notes, gates, filter, envelope, octave, velocity, and other parameters can use independent track lengths.
- **Per-step recording** - hold a parameter button and move the distance sensor to write values into the selected step.
- **Scale mapping** - 13 built-in musical scales with chromatic fallback.
- **Gate-aware note editing** - note edits are ignored on steps whose Gate parameter is low.

### Physical Control

- **32-button touch matrix** - 4x8 capacitive button layout for step editing and parameter selection.
- **AS5600 magnetic encoder** - base parameter control with velocity-sensitive behavior.
- **VL53L1X distance sensor** - time-of-flight hand control, mapped over the configured height range.
- **OLED display** - 128x64 SH1106G display for parameter and mode feedback.
- **8x8 WS2812B LED matrix** - step, voice, and control feedback through FastLED.

### Architecture

- **Dual-core firmware** - Core 0 handles audio; Core 1 handles UI, MIDI, sensors, OLED, and LEDs.
- **48kHz I2S audio** - custom Pico audio buffer and I2S plumbing under `src/audio/`.
- **Static audio buffers** - `SAMPLES_PER_BUFFER=256`, `NUM_AUDIO_BUFFERS=3`.
- **VoiceSystem** - centralized four-voice state with array-based access.
- **Local DSP code** - DaisySP-derived pieces are vendored and adapted under `src/dsp/`.

## Project Structure

```text
Pico2Seq.ino              # Main Arduino sketch and dual-core setup
includes.h                # Arduino-friendly header aggregator
diagnostic.h              # Diagnostics and error flags
src/
  audio/                  # I2S audio, PIO, buffer management
  dsp/                    # Local DSP building blocks
  sequencer/              # Step tracks, parameter definitions, timing
  voice/                  # VoiceSystem, VoiceManager, presets, voice DSP
  ui/                     # UI state, button handlers, event routing
  matrix/                 # MPR121 touch matrix scanning
  sensors/                # AS5600 and VL53L1X support
  midi/                   # USB MIDI and CC handling
  LEDMatrix/              # 8x8 LED feedback
  OLED/                   # SH1106G display code
  scales/                 # Scale definitions
docs/                     # Architecture and subsystem notes
```

## Hardware

### Required Parts

- Raspberry Pi Pico2 / RP2350 board
- I2S-compatible DAC or codec, such as PCM5102A or PT8211
- MPR121 capacitive touch sensor for a 4x8 matrix
- SH1106G 128x64 OLED display
- AS5600 magnetic encoder and magnet
- VL53L1X time-of-flight distance sensor
- 8x8 WS2812B LED matrix

### Wiring

**I2S audio**

- GP15 - DAC DATA
- GP16 - DAC BCK
- GP17 - DAC LRCK

**Touch matrix**

- GP0, GP1, GP2, GP3 - MPR121 electrodes 0-3, rows
- GP4, GP5, GP6, GP7, GP8, GP9, GP10, GP11 - MPR121 electrodes 4-11, columns

**I2C bus**

- GP12 - SDA
- GP13 - SCL
- OLED address: `0x3C`
- AS5600 address: `0x36`
- VL53L1X address: `0x29`

**LED matrix**

- GP1 - WS2812B data

Note: GP1 is currently documented for both the touch matrix row wiring and LED data. Check your hardware revision before wiring or changing pin definitions.

## Build

This is an Arduino sketch. There is no PlatformIO or CMake project in this repo.

### Arduino IDE

1. Open `Pico2Seq.ino`.
2. Select `Raspberry Pi Pico2` / `RP2350`.
3. Install the required libraries.
4. Verify the sketch.
5. Upload to the board.
6. Open the serial monitor for startup diagnostics.

### Required Arduino Libraries

- `Adafruit_MPR121`
- `Adafruit_SH110X`
- `Adafruit_TinyUSB`
- `FastLED`
- `Melopero_VL53L1X`
- `MIDI Library`
- `OneButton`
- `uClock`

The project includes its own adapted DSP sources in `src/dsp/`. Do not install DaisyDuino as a replacement unless you are intentionally porting APIs.

### Arduino CLI Example

```powershell
arduino-cli compile --fqbn "rp2040:rp2040:rpipico2:flash=4194304_0,arch=arm,freq=225,opt=Optimize3,profile=Disabled,rtti=Disabled,stackprotect=Disabled,exceptions=Disabled,dbgport=Disabled,dbglvl=None,usbstack=tinyusb,ipbtstack=ipv4only,uploadmethod=default" --build-path "build" --warnings default "Z:\Codezzz\Pico2Seq"
```

## Basic Operation

1. Power on the device. The four voices initialize with their configured presets.
2. Start playback from the play/stop control.
3. Use the touch matrix to set gates, select steps, and choose parameters.
4. Use the AS5600 encoder to adjust the active parameter.
5. Hold parameter buttons and move your hand over the VL53L1X sensor to record values into steps.

UI voice numbers are shown as 1-4. Internal voice indexes are zero-based.

## Development Notes

### Real-Time Boundaries

- Keep audio work on Core 0.
- Keep UI, MIDI, display, LED, and sensor polling on Core 1.
- Avoid heap allocation in the audio path.
- Treat shared audio state carefully; do not add unsynchronized UI-side writes to audio-critical state.

### VoiceSystem

The voice layer is centralized around `VoiceSystem`:

```cpp
VoiceState& state = voiceSystem.getVoiceState(voiceIndex);
voiceSystem.setGate(voiceIndex, true);
voiceSystem.stopAllGates();
```

Voice ids used by `VoiceManager` are not the same thing as UI voice numbers. Convert deliberately.

### Adding A Voice Preset

1. Add the new `VoiceConfig` in `src/voice/VoicePresets.cpp`.
2. Update preset count/name handling in `src/voice/VoicePresets.h` and `.cpp`.
3. Compile with the Pico2 board target.
4. Test on hardware across all four voices.

### Adding DSP

1. Add the DSP class under `src/dsp/`.
2. Wire it into `VoiceConfig` and `Voice`.
3. Check CPU and memory impact on hardware.
4. Listen for dropouts before calling it finished.

## Documentation

- `docs/architecture.md` - system architecture and data flow
- `docs/voice.md` - voice system, presets, and DSP behavior
- `docs/sequencer.md` - sequencing model and parameter tracks
- `docs/matrix.md` - touch matrix behavior
- `docs/midi.md` - MIDI I/O and CC support
- `docs/oled.md` - display behavior
- `docs/sensors.md` - AS5600 and VL53L1X integration
- `docs/LEDMatrix.md` - LED feedback system
- `docs/ButtonHandlers.md` - button event handling

## Testing

There is no automated test suite configured in this repo. The practical verification path is:

1. Compile for Raspberry Pi Pico2 / RP2350.
2. Upload to hardware.
3. Check serial startup output.
4. Verify audio output, touch matrix input, OLED, LED matrix, AS5600, VL53L1X, and USB MIDI.

## License

MIT License. See `LICENSE`.

## Related Work

- [Pico-DSP-Garden](https://github.com/IC-Alchemy/Pico-DSP-Garden) - related Pico DSP experiments from IC Alchemy.
