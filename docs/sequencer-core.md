# Sequencer Core

The core lives in `src/sequencer/`.

It is intentionally small: fixed-size parameter tracks, independent lane lengths, step processing, gate duration, and note-off callbacks. It does not own a display, sensor, LED matrix, synth engine, or board pin map.

## Parameters

`ParamId` and `CORE_PARAMETERS` define the lane order and value ranges:

- `Note`
- `Velocity`
- `Filter`
- `Attack`
- `Decay`
- `Octave`
- `GateLength`
- `Gate`
- `Slide`

The array order must match the enum order.

## Timing

Each parameter lane has its own step count. `Gate` defines the main visible sequence position. Other lanes can loop at different lengths to create polymetric movement.

`uClock` supplies the step callback in the template sketch. A different project can use another clock source as long as it calls:

```cpp
sequencer.advanceStep(stepIndex, input, &state);
```

Call `tickNoteDuration()` from a PPQN or other high-resolution tick if the output backend needs gate-length note-off handling.

## Output State

`VoiceState` is the current sequencer output for one track. It can drive MIDI, CV/gate, a synth engine, lighting, or any other backend.

Important fields:

- `noteIndex`
- `velocityLevel`
- `filterCutoff`
- `attackTimeSeconds`
- `decayTimeSeconds`
- `octaveOffset`
- `gateLengthTicks`
- `isGateHigh`
- `hasSlide`
- `shouldRetrigger`

## Note Editing Rule

Note writes are ignored when the target step's `Gate` lane is low. This keeps inactive steps from silently accumulating pitch changes.
