# Voice Edit and sequenced modifiers

Voice Edit implements the first three development stages: the sound parameter
catalogue, stopped-transport controls and OLED, and independent patch bases with
sequenced modifiers. Auditioning and saving patches across power cycles are later
stages. Changes currently remain in RAM until a preset is loaded or power is lost.

## Controls

Hold Shift (button 8 on the button tile) and press slider button 4 to enter.
All four sequencers stop and audio is muted. Release the entry buttons before
editing. Slider buttons 1–4 select the voice being edited.

| Button tile | Action |
|---|---|
| 1 / 2 | Previous / next parameter group |
| 3 / 4 | Previous / next parameter within the group |
| 5, held | Fine encoder adjustment |
| 6, held for 700 ms | Reset the selected base to the loaded preset's value |
| 7 | Toggle the control guide |
| 8 | Exit, retaining edits and leaving transport stopped |

Turn the encoder to edit the displayed base. Groups and parameters are filtered
by the active engine and enabled processors. Each voice remembers its parameter
cursor. The OLED shows `EDIT V1` through `EDIT V4`, a modified marker, the parameter
and units, and whether a sequence lane modifies that base. Press normal Play after
exiting to hear the edited patch in its sequence.

During Voice Edit, faders, the lidar, touch pads, transport chords, and the GP7
Param/Utility switch do not change the patch or sequence. The switch is sampled
again after exit. Button releases from the editor are consumed before normal
controls resume. Button 7 is Help in these stages; audition is not implemented.

## Base plus modifier

Every numeric sound setting has a per-voice base in `VoiceConfig`. The encoder
edits these requested values, including outside the editor when a sequenced
parameter is selected. It no longer edits a selected step's stored value.

The lidar records only a modifier. Both step editing and live recording normalize
the calibrated 55–1200 mm sensor range to 0–1. In the normalized parameter domain:

```
modifier = lidarNormalized - 0.5
effective = clamp(baseNormalized + modifier, 0, 1)
```

The midpoint is neutral. For a 70% velocity base, recorded readings of 25%, 50%,
and 75% give effective velocities of 45%, 70%, and 95%. Changing the base leaves
those three recordings unchanged. Limits clamp the result. There is no additional
depth control in these stages.

The portable sequencer retains its existing storage units: Note is 0–36,
GateLength is 0.001–1, and the other continuous lanes are 0–1. Playback converts
that storage to the modifier domain and combines it with the base before note
selection, octave mapping, and gate-duration timing. New/reset steps have neutral
modifiers, including the hidden Note values of silent steps. Randomization
randomizes modifiers. Loading a preset replaces the bases without reseeding lanes.

Note bases are scale steps; octave bases are quantized semitones in octaves from
-24 to +24. Attack and decay use a logarithmic 1 ms–10 s domain. Their sequencer
lanes do not change sustain or release. Engine-specific lanes retain their own
curves and units, including Hard Sync's Slave lane, string T60, and FM ratio.

Gate remains a trigger pattern: its base enables/disables the pattern. Slide's
base can enable slide throughout the pattern, otherwise the recorded Slide bits
control it. Neither binary track is treated as a continuous lidar modifier.

The step OLED shows the stored signed modifier. The normal encoder line shows
the base with units. The editor's Engine and recipe selections update their
associated parameter layout together. Selecting hard-sync waveforms switches
the Velocity lane to Slave tuning; selecting only ordinary waveforms restores
Velocity. SVF offers LP/BP/HP; ladder filters offer the six native responses.

## Parameter coverage

The catalogue covers sequenced bases; engine and recipe selection; oscillator
count and each oscillator's waveform, level, tuning, pulse width and harmony;
ADSR; main-filter topology, response, cutoff, resonance, drive, compensation and
envelope amount/floor; high-pass cutoff/resonance; overdrive; glide; voice enable
and output level; all waveguide, Hypersaw and Noise FX settings; recipe macros
and additional FM feedback, triangle drive, spectral sub-source ratio/shape,
Prism chaos and reset-on-gate controls. Unsupported/inactive settings are hidden.

Oscillator phases, random seeds, buffer sizes, DSP callback pointers, and numerical
stability constants are engine implementation state, not editable patch values.
When changing engine/recipe, resetting a parameter uses the loaded preset's
values adapted to the new engine's valid ranges.

## Implementation and ownership

- `src/voice/VoiceEditParameters.*`: stable parameter IDs, typed accessors,
  labels, units, ranges, navigation, applicability, and modifier composition.
- `src/ui/VoiceEditControls.h`: hardware-free interaction state inside `UIState`.
- `src/app/VoiceEditor.*`: transport transitions, encoder routing and publication.
- `Sequencer::setPlaybackTransform`: a portable callback applied only during
  playback; it never changes stored steps. Each context points at its voice's
  stable control-owned requested config, not audio-owned applied state.
- The existing SPSC queues carry config and effective voice state to Core 1.
  Scalar edits avoid unnecessary oscillator rebuilds; structural changes wait
  for gate-low. Queue retries and audio processing continue while transport is
  muted, including for voices with the envelope disabled.

Tests cover all presets, navigation, clamping, neutral and non-neutral modifiers,
unchanged recordings, gate duration, reset/entry releases and independent voices.
Firmware compilation and upload do not verify physical controls or listening;
check these on the instrument after flashing.
