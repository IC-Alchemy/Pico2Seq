# Voice Preset Overhaul Implementation Plan

Saved 2026-09-03. Approved plan for: waveguide presets (WgPluck/Nylon/Bell/Shimmer)
bypass ladder+envelope and expose wg sequencer params; Hypersaw/NoiseStorm get
engine-specific sequencer params; all 15 presets selectable; preset selection no
longer stops music.

## Architecture

`VoiceConfig::paramSet` (STANDARD/WAVEGUIDE/HYPERSAW/NOISESTORM) tells
`Voice::applyPendingParams_()` how to interpret the sequencer Filter/Attack/Decay
slots per voice, and drives UI naming via `VoicePresets` helpers. Slots are
**re-purposed, not enum-removed**: `ParamId` is one global enum for all 4 voices
and Alchemy param buttons map positionally onto ParamId values
(`AlchemyControlBridge.cpp:233-234`), so enum insert/remove would rewire physical
buttons and add unused tracks to every voice. Preset apply re-seeds re-purposed
tracks from preset values so encoders/OLED/voice agree.

## Slot mappings

- WG presets: Filter→Bright(ess), Attack→Pick hardness, Decay→T60
  (`fmap EXP 0.05–10s`). Engine: `hasFilter=false`, `hasEnvelope=false`, HPF
  bypassed; string rings naturally; velocity scales output in `finalizeOutput`.
- Hypersaw: Filter→Cutoff (kept), Attack→Detune (0–1 semitone symmetric),
  Decay→Drive (overdrive).
- NoiseStorm: Filter→Color, Attack→Regen, Decay→Chaos; ladder keeps static preset
  cutoff via `VoiceConfig::filterCutoffBase`.

## Key findings

1. Selection bug: `UIEventHandler.cpp:501` caps pads at `maxAllowedButton=14` →
   only presets 0–6 reachable. Fix: `VoicePresets::presetIndexForPad` (pads 8–22).
2. "Music stops": apply is non-destructive (staged `setConfig`); the stop happens
   *entering* settings (Play while running → `onClockStop()` → seq stop + MIDI
   all-notes-off). Fix: long-press Play/Stop toggles settings without stopping;
   short release while running+settings closes settings only.
3. Latent bug: `computeEnvelope()` returned on `!hasEnvelope` before the
   gate-rise branch that sets `wgPluckPending_` — `hasEnvelope=false` never plucked.
4. Velocity only entered at the ladder input; bypassing filter needs a new path.

## Tasks

1. **Voice: `hasFilter` bypass + envelope/pluck ordering fix.** Add
   `VoiceParamSet` enum + `paramSet`/`hasFilter`/`filterCutoffBase` fields;
   reorder `computeEnvelope()`; gate `updateFilter` and ladder path on
   `hasFilter` (velocity applied directly when bypassed). Tests: bypassed
   envelope still plucks; bypassed filter scales with velocity.
2. **WG preset cleanup.** Makers set `paramSet=PARAMSET_WAVEGUIDE`,
   `hasEnvelope=false`, `hasFilter=false`, `highPassFreq/Res=0`; delete
   filter+envelope assignments; retune outputLevel (Pluck .85, Nylon .9,
   Bell .75, Shimmer .8). Rewrite decay test with `wgT60=0.3f` (no ADSR release).
3. **Per-paramSet routing** in `applyPendingParams_()` (WG: brightness/hardness/
   T60; HYPER: detune spread + overdrive drive + live cutoff; NOISE:
   color/regen/chaos + static `filterCutoffBase`; STANDARD: unchanged) +
   `VoicePresets::wgT60ToNormalized` (inverse of `fmap EXP 0.05–10`) +
   `Voice::applyEnvelopeDefaults_()`.
4. **Helpers:** `getPresetParamSet`, `getSequencerParamName` (Bright/Pick/T60,
   Detune/Drive, Color/Regen/Chaos; nullptr ⇒ standard), `presetIndexForPad`
   (base pad 8, −1 out of range). paramSet on Hypersaw/NoiseStorm makers.
5. **Gate-safe switching:** defer oscillator prepare/waveform + engine
   resetAlternateEngines_ to gate-low (`structuralPending_` + `applyStructuralConfig_()`);
   scalars apply immediately.
6. **Selection UI:** `handlePresetSelection` uses `presetIndexForPad` (drop the
   14 cap); sub-mode toggle works while running; Alchemy Play/Stop tile
   long-press toggles settings live, short release while running+settings exits
   settings only; ButtonHandlers filter mode/resonance guarded on `!hasFilter`;
   OLED prompt dynamic (pads 8-22).
7. **Re-seed re-purposed tracks** in `Pico2Seq.ino::applyVoicePreset` via
   `seedRepurposedParamTracks()` (per-paramSet slot values over all steps).
8. **OLED + docs:** preset-aware param labels/formatting (T60 in seconds via
   EXP map); docs/voice.md, docs/oled.md, skill reference updated; full suite green.

## Accepted behavior shifts

- MIDI CC 74/73/72 carries re-purposed values on non-standard presets.
- Encoder base offsets / randomize act on the same 0–1 slots (valid for all meanings).
- Re-selecting the same preset re-seeds the 3 re-purposed tracks (deliberate).
- Firmware can't be built headlessly — bench-verify WG levels, long-press flow, OLED labels.
