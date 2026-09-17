# Voice Edit and sequenced modifiers

Voice Edit implements the first three development stages: the sound parameter
catalogue, stopped-transport controls and OLED, and independent patch bases with
sequenced modifiers. Patches now persist across power cycles: every save (Utility
button 1, autosave on transport stop) captures the live `VoiceConfig` control copy
per voice, and each boot restores it. Loading a factory preset still replaces the
bases; saved patches ride in the session snapshot, not the preset bank.

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
parameter is selected. With a step selected for editing, it edits that step's stored
value instead (see `editSelectedStep()` in `src/sensors/EncoderManager.cpp`).

For timbre macros, velocity, octave and gate length, the lidar records a modifier.
Note records melody scale steps directly. (The drone build removed the per-voice
ADSR envelope and main filter: every voice renders continuously, gate-off does not
silence it, and the Filter/Attack/Decay lanes exist only where the active engine
binds them to macros — on the nine oscillator presets they are unbound and inert,
so recordings are stored but shape nothing audible.) Both step editing and live recording normalize
the calibrated 55–700 mm sensor range to 0–1 (a 645 mm active span). With no hand in range (an invalid reading or
one more than 40 mm outside the window) nothing is recorded and steps keep their values.
In the normalized parameter domain:

```
if (lidarNormalized >= 0.5)
    effective = base + (lidarNormalized - 0.5) * 2 * (1 - base)
else
    effective = base + (lidarNormalized - 0.5) * 2 * base
```

The midpoint (`0.5`) is neutral (exactly equals `base`). Hand movement from 0.5 down to 0.0 spans
linearly from `base` down to `0.0`, while movement from 0.5 up to 1.0 spans linearly from `base`
up to `1.0`. This ensures that regardless of the preset base value, there are no dead zones and
hand movement spans the entire parameter range — for macro-bound lanes; the old
attack/decay time-span claims now apply only to engine macros (e.g. string T60 in seconds).
Unbound lanes on the oscillator presets accept recordings but nothing audible changes.

The portable sequencer retains its existing storage units: Note is 0–36,
GateLength is 0.001–1, and the other continuous lanes are 0–1. Playback converts
continuous control storage to the modifier domain before octave mapping and
gate-duration timing. Note uses `clamp(recordedScaleStep + baseNote, 0, 36)`, so
the entire recording range writes a melody even with the default zero transpose.
New/reset Note steps start at zero; other continuous lanes start at their neutral
midpoint. Gate and Slide start off. The default gate lasts half a sixteenth note
(60 ticks, 83.3 ms at the starting 90 BPM). Randomization uses integer melody steps
0–12, modest macro/velocity variation around the preset, neutral octave
and gate length, and the existing gate/slide probabilities. Loading a preset replaces the bases without reseeding lanes.

Note bases are scale steps; octave bases are quantized semitones in octaves from
-24 to +24. Cutoff/attack/decay bases exist only where the active engine binds
those lanes to macros — on macro engines the encoder edits the macro base in the
binding's own domain and unit (string T60 in seconds, FM ratio, percentage
macros); on the nine oscillator presets the lanes are unbound and inert (the OLED
shows `--`). There is no sustain or release to sequence: the drone build removed
the ADSR, so gate edges only re-fire the engine triggers and commit pitch.
Engine-specific lanes retain their own
curves and units, including Hard Sync's Slave lane, string T60, and FM ratio.

Gate remains a trigger pattern: its base enables/disables the pattern. Slide's
base can enable slide throughout the pattern, otherwise the recorded Slide bits
control it. Neither binary track is treated as a continuous lidar modifier.

With a parameter button held, and in Step Edit, the OLED shows composed playback
values, after preset bases, clamping, quantization and engine-specific mapping: the
value the voice plays. With no parameter held, the home screen shows the encoder
target's base, since a step's modifier or a clamp at a limit could otherwise hide an
encoder turn. Live edits (lidar, faders, encoder) refresh the sounding note in place
through `Sequencer::refreshVoiceParameters()`; they never retrigger it. Note displays
The step OLED and normal encoder screen show composed playback values, after
preset bases, clamping, quantization and engine-specific mapping. For 1.5 s after an
encoder turn they show the edited base instead (`Base` / `BASE`), since a step's
modifier or a clamp at a limit can otherwise hide the change. Note displays
note names and octaves, including oscillator harmonies/detuning (Bass starts at
`C2/C3`); unpitched percussion reads `Noise`. Macro lanes use their binding's
unit — percentages, seconds for T60, ratios for FM/spacing — and read `--` when
the lane is unbound. Gate durations use
ms/s, octave uses signed octaves.
Velocity uses its amplitude multiplier. Percentages remain only for actual
percentage-based timbre controls. Gate and Slide show their effective states.
Glide shows its target notes.

A selected step takes priority over the moving playhead. Live views follow each
parameter's independent track position. `REST` distinguishes a stored target on
an inactive step from a new sounding note (`R` on the home screen); existing release tails can continue.
The stopped editor displays the patch's unmodulated base in the same units.
The preset name is largest on the home screen and stays in the header while
editing. `*` marks changes to the loaded preset. The editor's Engine and recipe selections update their
associated parameter layout together. Selecting hard-sync waveforms switches
the Velocity lane to Slave tuning; selecting only ordinary waveforms restores
Velocity.

## Parameter coverage

The catalogue covers sequenced bases; engine and recipe selection; oscillator
count and each oscillator's waveform, level, tuning, pulse width and harmony;
high-pass cutoff/resonance (waveguide engines only); overdrive; glide; voice enable
and output level; all waveguide, Hypersaw and Noise FX settings; recipe macros
and additional FM feedback, triangle drive, spectral sub-source ratio/shape,
Prism chaos and reset-on-gate controls. The editor groups are: sequenced bases,
Source, Oscillator 1–3, High-pass, Overdrive, Engine and Output — the drone build
removed the Envelope and main-filter groups, and the legacy filter/envelope
fields in `VoiceConfig` survive only so saved patches round-trip unchanged (they
are audio-inert). Unsupported/inactive settings are hidden.

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
  muted.

Tests cover all presets, navigation, clamping, neutral and non-neutral modifiers,
unchanged recordings, gate duration, reset/entry releases and independent voices.
Firmware compilation and upload do not verify physical controls or listening;
check these on the instrument after flashing.
