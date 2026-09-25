# Voice Edit and sequenced step values

Voice Edit implements the first three development stages: the sound parameter
catalogue, stopped-transport controls and OLED, and independent patch bases with
sequenced step values. Patches now persist across power cycles: every save (Utility
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

## Patch values and step values

Every numeric sound setting has a per-voice base (the patch value) in
`VoiceConfig`. The encoder edits these requested values, including outside the
editor when a sequenced parameter is selected. With a step selected for editing, it
edits that step's value instead (see `editSelectedStep()` in
`src/sensors/EncoderManager.cpp`). The faders never edit patch values.

### Absolute lanes

Velocity, Filter, Attack, Decay, Sustain and Release are **absolute** lanes
(`ParameterDefinition::patchDefault`). A step either holds its own normalized
0–1 value, which plays as stored whatever the patch says, or holds
`SequencerConstants::LANE_FOLLOWS_PATCH` (-1) and plays the voice's patch value
(`VoiceEdit::laneBase()`). New, cleared and reset steps follow the patch, so an
untouched pattern plays each preset exactly as designed.

The lidar (hand height), the encoder in Step Edit and the ENV-mode faders all
write absolute values: the calibrated 55–700 mm sensor range maps straight onto
the lane's 0–1 range. With no hand in range (an invalid reading or one more than
40 mm outside the window) nothing is recorded and steps keep their values.

Until 2026-09-19 these lanes stored offsets around the patch (0.5 = patch, below
0.5 scaled toward 0 by the patch value). A patch value at the end of its range
therefore swallowed every lower-half hand height or encoder turn: with a Filter
base of 0 the OLED sat at the minimum while the hand moved, and a Decay base near
0 left sustain-0 voices such as Square silent. Sessions saved in that format
convert on load (`VoiceEdit::convertOffsetValues()`): neutral steps follow the
patch, every other step becomes the value it played.

`Shift` + an ENV fader move, the Shift+pad step clear and Shift+Randomize (clear
pattern) return steps to the patch value.

### Offset lanes

Note, Octave and Gate length stay offsets on top of the patch. Note uses
`clamp(recordedScaleStep + baseNote, 0, 36)`, so the entire recording range writes a
melody even with the default zero transpose. Octave adds whole octaves to the patch
octave; Gate length spreads around the patch gate length with 0.5 as neutral.
New/reset Note steps start at zero, Octave and Gate length at their neutral midpoint.
Gate and Slide start off. The default gate lasts half a sixteenth note (60 ticks,
83.3 ms at the starting 90 BPM).

Randomization uses integer melody steps 0–12, neutral octave and gate length, and
for the absolute lanes values spread around each voice's patch value (triangular,
depth D reaches at most D% of the way to either end). Randomized Attack never exceeds
the longer of the patch attack and the lane center (~45 ms), so a short gate on a
sustain-0 voice stays audible. Loading a preset replaces the bases without reseeding
lanes.

Note bases are scale steps; octave bases are quantized semitones in octaves from
-24 to +24. Attack uses a logarithmic 1 ms–2 s domain, Decay 1 ms–10 s and Release 10 ms–8 s,
Sustain is a 0–100% level. Engine-specific lanes retain their own curves and units,
including Hard Sync's Slave lane, string T60, and FM ratio. Strings have no envelope,
so their Sustain and Release lanes bind to pick Position and Stiffness. Hypersaw,
NoiseStorm and the recipes keep their Attack/Decay lanes for engine controls; their
Sustain/Release lanes still drive the real envelope.

On oscillator voices such as Digital and Square, hold the third parameter button
to record filter-envelope amount with lidar; the OLED shows the amount and peak
cutoff. Hold the fifth button (silkscreened Decay) to record Release. Release takes
effect when the gate closes; a new note retriggers the envelope. Its encoder base
uses the same time curve as playback. Waveguide keeps Bright and Stiffness on
these buttons. The held-button OLED reads recorded playback values directly,
including the selected step in Step Edit, rather than calculating a sensor preview.

Gate remains a trigger pattern: its base enables/disables the pattern. Slide's
base can enable slide throughout the pattern, otherwise the recorded Slide bits
control it. Neither binary track is treated as a continuous lidar lane.

The step OLED and normal encoder screen show composed playback values, after
preset bases, clamping, quantization and engine-specific mapping: the value the voice
plays. For 1.5 s after an encoder turn they show the edited base instead
(`Base` / `BASE`), since a step's own value can otherwise hide the change. Live edits
(lidar, ENV faders, encoder) refresh the sounding note in place through
`Sequencer::refreshVoiceParameters()`; they never retrigger it. The ADSR's stages are
linear ramps counted in samples, so `Voice` holds a new attack (decay, release) length
while that stage runs, and a new sustain level while decay or sustain runs; each lands
when its stage ends or at the next note-on. Cutoff, velocity and pitch apply at once. A patch publish glides the cutoff smoother
to its new target instead of snapping it. Note displays
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
