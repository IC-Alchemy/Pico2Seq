# Pico2Seq Template

A small Arduino sequencer template.

This branch keeps the reusable sequencer engine from Pico2Seq and removes the project-specific hardware: touch matrix, OLED, LED matrix, distance sensor, magnetic encoder, I2S audio, and onboard synth voice code. The result is a USB-MIDI step sequencer shell that can be adapted to whatever input hardware a new build needs.

## What It Does

- Runs four independent sequencer tracks.
- Keeps polymetric parameter lanes for note, velocity, filter, attack, decay, octave, gate length, gate, and slide.
- Uses gate-aware note editing: note writes are ignored on steps whose Gate value is low.
- Maps note indexes through the built-in scale tables in `src/scales/`.
- Sends USB MIDI notes and MIDI clock.
- Provides a weak `readHardwareInput()` hook for hardware adapters.
- Provides a serial command surface so the template can be exercised before hardware is added.

## Project Structure

```text
Pico2Seq.ino              # Arduino sketch, MIDI output, clock wiring, input hook
includes.h                # Minimal shared Arduino includes
diagnostic.h              # Placeholder for project-specific diagnostics
src/
  input/                  # Generic input event type and weak hardware hook
  sequencer/              # Step tracks, parameter definitions, timing
  scales/                 # Scale tables and names
docs/
  input-adapters.md       # How to connect hardware controls to the template
  sequencer-core.md       # Sequencer behavior and parameter model
```

## Build

This is still an Arduino sketch. There is no PlatformIO or CMake project in this repo.

The checked-in VS Code Arduino config targets Raspberry Pi Pico2 / RP2350 because that was the original board. The template code itself no longer depends on the old wiring, sensors, display, LEDs, or audio path.

### Required Arduino Libraries

- `Adafruit_TinyUSB`
- `MIDI Library`
- `uClock`

### Arduino IDE

1. Open `Pico2Seq.ino`.
2. Select the board you are targeting.
3. Install the required libraries above.
4. Verify the sketch.
5. Upload to hardware.
6. Open the serial monitor at `115200`.

### Arduino CLI Example

```powershell
arduino-cli compile --fqbn "rp2040:rp2040:rpipico2:flash=4194304_0,arch=arm,freq=225,opt=Optimize3,profile=Disabled,rtti=Disabled,stackprotect=Disabled,exceptions=Disabled,dbgport=Disabled,dbglvl=None,usbstack=tinyusb,ipbtstack=ipv4only,uploadmethod=default" --build-path "build" --warnings default "Z:\Codezzz\Pico2Seq"
```

## Serial Commands

Open the serial monitor and send newline-terminated commands:

```text
p                              toggle play/stop
t <seq> <step>                 toggle gate, 1-based
x <seq> <step> <param> <value> set parameter
m <seq> <param> <steps>        set parameter lane length
r <seq>                        randomize one sequencer
b <tempo>                      set BPM
c <scale>                      set scale index, 0-based
```

Parameter tokens:

```text
n note
v velocity
f filter
a attack
d decay
o octave
l gate length
g gate
s slide
```

Example:

```text
t 1 1
x 1 1 n 4
x 1 1 v 0.9
b 110
p
```

## Hardware Input Hook

The template declares this hook in `src/input/TemplateInput.h` and provides a weak default implementation in `src/input/TemplateInput.cpp`:

```cpp
void __attribute__((weak)) readHardwareInput(TemplateInputEvent &event)
{
    (void)event;
}
```

Add another `.ino` or `.cpp` file and define `readHardwareInput()` there to feed buttons, encoders, MIDI CC, GPIO, analog controls, touch surfaces, or sensors into the same `TemplateInputEvent` shape.

The sequencer core only receives normalized parameter input through `ParameterInputState`. Hardware-specific scaling belongs in the adapter layer.

## Development Notes

- Keep `ParamId` and `CORE_PARAMETERS` in `src/sequencer/SequencerDefs.h` aligned.
- Keep hardware dependencies out of `src/sequencer/`.
- Route outputs from `VoiceState` to whatever backend the new project needs: MIDI, CV/gate, synth engine, LEDs, or host messages.
- `build/` is generated Arduino output.

## Testing

There is no automated test suite configured in this repo.

Practical verification:

1. Compile for the selected Arduino board.
2. Upload to hardware.
3. Open serial at `115200`.
4. Use serial commands to toggle gates, edit notes, start clock, and confirm USB MIDI output.

## License

MIT License. See `LICENSE`.
