# Input Adapters

The template keeps hardware outside the sequencer.

Hardware code should translate physical controls into `TemplateInputEvent` commands or `ParameterInputState` recording values. The sequencer should not know whether the value came from an encoder, button matrix, analog input, MIDI CC, sensor, or host message.

## Event Input

Use `TemplateInputEvent` for discrete actions:

- Toggle run state.
- Toggle a gate.
- Set a step parameter.
- Change a parameter lane length.
- Randomize a sequencer.
- Set tempo.
- Set scale.

Override the weak hook from another source file:

```cpp
void readHardwareInput(TemplateInputEvent &event)
{
    if (!myButtonPressed()) {
        return;
    }

    event.action = TemplateInputAction::ToggleGate;
    event.sequencerIndex = 0;
    event.stepIndex = 0;
}
```

Indexes are zero-based inside the event. The serial commands are one-based for human use and convert before creating events.

## Normalized Recording

Use `ParameterInputState` when a continuous control should record into the playing lane:

```cpp
ParameterInputState input;
input.held[static_cast<size_t>(ParamId::Filter)] = true;
input.normalizedValue = analogRead(A0) / 1023.0f;
sequencer.advanceStep(step, input, &state);
```

Keep values in the range `0.0` to `1.0`. The sequencer maps that range through `CORE_PARAMETERS`.

## Adapter Rules

- Clamp hardware values before passing them into the sequencer.
- Keep pin maps and library includes in adapter files, not in `src/sequencer/`.
- Keep output routing separate from input routing.
- Preserve gate-aware note editing unless a project explicitly changes the musical rule.
