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
cursor. The OLED keeps a small `V1` through `V4` and the preset name visible, with a
modified marker, parameter units, and a stopped/base indicator. Press normal Play after
exiting to hear the edited patch in its sequence.

During Voice Edit, faders, the lidar, touch pads, transport chords, and the GP7
Param/Utility switch do not change the patch or sequence. The switch is sampled
again after exit. Button releases from the editor are consumed before normal
controls resume. Button 7 is Help in these stages; audition is not implemented.

## Base plus modifier

Every numeric sound setting has a per-voice base in `VoiceConfig`. The encoder
edits these requested values, including outside the editor when a sequenced
parameter is selected. It no longer edits a selected step's stored value.

For timbre, envelope, velocity, octave and gate length, the lidar records a modifier.
Note records melody scale steps directly. Both step editing and live recording normalize
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
continuous control storage to the modifier domain before octave mapping and
gate-duration timing. Note uses `clamp(recordedScaleStep + baseNote, 0, 36)`, so
the entire recording range writes a melody even with the default zero transpose.
New/reset Note steps start at zero; other continuous lanes start at their neutral
midpoint. Gate and Slide start off. The default gate lasts half a sixteenth note
(60 ticks, 83.3 ms at the starting 90 BPM). Randomization uses integer melody steps
0–12, modest timbre/envelope/velocity variation around the preset, neutral octave
and gate length, and the existing gate/slide probabilities. Loading a preset replaces the bases without reseeding lanes.

Note bases are scale steps; octave bases are quantized semitones in octaves from
-24 to +24. Attack and decay use a logarithmic 1 ms–10 s domain. Their sequencer
lanes do not change sustain or release. Engine-specific lanes retain their own
curves and units, including Hard Sync's Slave lane, string T60, and FM ratio.

Gate remains a trigger pattern: its base enables/disables the pattern. Slide's
base can enable slide throughout the pattern, otherwise the recorded Slide bits
control it. Neither binary track is treated as a continuous lidar modifier.

The step OLED and normal encoder screen show composed playback values, after
preset bases, clamping, quantization and engine-specific mapping. Note displays
note names and octaves, including oscillator harmonies/detuning (Bass starts at
`C2/C3`); unpitched percussion reads `Noise`. Envelope and gate durations use
ms/s, cutoff uses Hz, octave uses signed octaves, and FM/spacing use ratios.
Velocity uses its amplitude multiplier. Percentages remain only for actual
percentage-based timbre controls. Gate and Slide show their effective states.
The filter readout is the cutoff target before envelope modulation, not a
sample-by-sample measurement of the moving filter. Glide shows its target notes.

A selected step takes priority over the moving playhead. Live views follow each
parameter's independent track position. `REST` distinguishes a stored target on
an inactive step from a new sounding note (`R` on the home screen); existing release tails can continue.
The stopped editor displays the patch's unmodulated base in the same units.
The preset name is largest on the home screen and stays in the header while
editing. `*` marks changes to the loaded preset. The editor's Engine and recipe selections update their
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
- `src/voice/MusicalValues.h`: shared pitch/time conversion and OLED formatting.
- `Sequencer::getPlaybackStep`: read-only composed snapshot shared by playback
  and display; inspecting a value never triggers a note.
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
