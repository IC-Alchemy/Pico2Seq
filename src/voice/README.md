# Adding voices and presets

A preset is a sound recipe plus a parameter layout. The sequencer keeps its
existing tracks; each sound decides what its timbre controls do.

| File | What belongs here |
| --- | --- |
| `VoiceConfig.h` | Shared configuration and immutable recipe/layout references |
| `VoiceParameters.h/.cpp` | Parameter ranges, DSP routing, display units and track seeding |
| `presets/OscillatorPresets.h` | Analog, digital, bass, lead and percussion settings |
| `presets/StringPresets.h` | Waveguide settings |
| `presets/TexturePresets.h` | Native Hypersaw and NoiseStorm settings |
| `presets/RecipePresets.h` | Recipe settings and their three timbre bindings |
| `presets/MusicalPresets.h` | Eight musical patches, defaults and timbre bindings |
| `presets/PresetBank.h` | One list of preset IDs, names and factories |
| `engines/RecipeSources.h` | Small rpdsp patches, with their state sizes declared |
| `engines/RecipeEngine.h` | Fixed storage and lifecycle for those patches |
| `Voice.cpp` | Shared pitch, gate, envelope, filter, mixing and cross-core delivery |

The bank currently has 29 presets. Existing IDs 0–20 retain their names and
order. The latest eight presets occupy IDs 21–28.
Name lookup is case-insensitive, including `VoiceManager` lookup/listing.

## Musical preset bank

These patches connect existing rpDSP functions and use the shared voice ADSR
and high-pass filter. They add no DSP algorithms or per-voice storage.
The three controls below occupy Filter, Attack and Decay, respectively.

| Preset | Sound and rpDSP source | Timbre controls |
| --- | --- | --- |
| VelvetKeys | Soft electric keys: two `osc_fbfm` operators at a 2:1 ratio, low feedback, rounded attack and a lingering release | Index / Ratio / Feedback |
| CopperBass | Rounded, harmonically rich bass: `osc_dsf` at harmonic spacing plus a sine sub from `osc_pdmorph`, short release | Bright / Spacing / Sub |
| ReedPipe | Held reed tone: `osc_formant` bursts blended with a sine fundamental | Formant / Bloom / Body |
| SilkPad | Slow swell: two gently detuned `osc_pdmorph` voices, free-running phase and a 1.25 s release | Silk / Detune / Blend |
| HollowBell | Hollow tuned bell: two `osc_pdmorph` sources ring-modulated at 2:1, mixed with the fundamental; zero sustain | Ratio / Edge / Ring |
| SyncLead | Firm melodic lead: `osc_revsync` blended with a pitched `osc_pdmorph` body | Sync / Edge / Bite |
| OrbitPluck | Rounded metallic pluck: sine-modulated `osc_tzfm` plus a clean sine body; zero sustain | Index / Ratio / Body |
| AirChime | Light harmonic chime: `osc_prism` blended with an octave sine, slow decay | Focus / Spread / OctMix |

ReedPipe's Bloom lengthens the formant bursts (48 kHz retention 0.990–0.9995,
converted by rpDSP's sample-rate helper during configuration). SilkPad's Detune
spans unison to a 1.006 frequency ratio on either side of the note, about
10 cents each way. Blend balances those two oscillators. Its slower attack
benefits from longer gates; bell and pluck gates can shorten their release tails.
Integer ratios keep FM/ring sounds harmonically related; fractional ratios
provide more metallic textures. The chosen defaults use harmonic ratios.

VelvetKeys, CopperBass and ReedPipe are the last three pads of page 1;
SilkPad through AirChime occupy pads 8–12 of page 2 (pad 7 advances the page).
All eight retain Note, Velocity, Octave, GateLength, Slide and the Gate trigger.
The voice editor includes all ten recipe sources and their matching layouts.

Host checks cover control influence, finite output at parameter extremes,
default output at C2/C4/C6, release/retrigger, preset seeding and editor selection.
Musical descriptions express the patch design; perceived balance and RP2350
realtime performance still need auditioning on hardware.

## The eight controls

| Track | Recipe voices | Existing exceptions |
| --- | --- | --- |
| Note | Scale-aware pitch | Analog labels it Master |
| Velocity | Amplitude | Analog uses it for Slave pitch |
| Filter | First timbre control | Cutoff on subtractive voices |
| Attack | Second timbre control | Envelope attack on subtractive voices |
| Decay | Third timbre control | Envelope decay/release on subtractive voices |
| Octave | Pitch transposition | Shared |
| GateLength | Note duration, applied by the sequencer | Natural waveguide tails ring after gate-off |
| Slide | Pitch glide | Existing waveguide engine changes pitch when plucked |

Gate is the additional on/off trigger track. Recipe voices share gate-on,
gate-off and retrigger handling. Their preset ADSR shapes the note when Attack
and Decay control timbre. The shared pitch path includes harmony, detune,
pitch bend and glide.

For example, FMGlass maps Filter/Attack/Decay to Index/Ratio/Feedback. A
`VoiceParameterBinding` specifies its label, destination `VoiceConfig` member,
range, response curve, display unit and whether to seed its track. That binding
maps automation into DSP values, converts preset values back into track values,
and formats the OLED value. `EXP` is a square curve, not an exponential in Hz.
The waveguide T60 range is now consistently 0.05–10 seconds in all three paths.

Only Velocity, Filter, Attack and Decay support float-member remapping.
Note, Octave, GateLength, Gate and Slide retain their shared musical roles.
Bindings default to the standard role and label. The legacy `paramSet` values
remain supported; new recipes provide `config.parameters` directly.

## Add a variation of an existing sound

Add a factory to the appropriate preset header. For example, in
`presets/RecipePresets.h`, inside `namespace VoicePresets`:

```cpp
constexpr VoiceConfig makeMellowFM() noexcept
{
    auto c = makeFmGlass();
    c.macro1 = 0.12f; // Index, in the binding's mapped units
    c.macro2 = 2.0f;  // Modulator/carrier ratio
    c.macro3 = 0.04f; // Feedback
    c.defaultRelease = 0.35f;
    return c;
}
```

Append one row to `presets/PresetBank.h`:

```cpp
VOICE_PRESET(MellowFM, "MellowFM", makeMellowFM)
```

Count, ID, display name, lookup by name and lookup by index follow that row.
Keep existing rows in order. No getter or UI switch is needed. A new header only
needs one include in `VoicePresets.cpp` before the bank is expanded.

## Add a different DSP patch

Use the checked-out [rpdsp module index](../rpdsp/README.md) and the state-size
comments in its headers. In `engines/RecipeSources.h`, implement a small patch:

```cpp
inline float reversingSync(float inc, const VoiceConfig &c, float *state) noexcept
{
    return rpdsp::osc_revsync(inc, c.macro1, state);
}
inline constexpr auto kReversingSync = makeVoiceRecipe<3>(reversingSync);
```

This illustrates the source contract. A complete playable preset should give
each of its three timbre controls a useful DSP destination: combine this source
with another recipe, or retain envelope controls in the layout. Do not expose
controls that the patch ignores.

Create a `VoiceParameterLayout`, then a factory using `recipeVoice()` as in
`RecipePresets.h`. All descriptor objects must have static lifetime, normally
`inline constexpr`. Append the bank entry. `Voice.cpp`, the sketch, and OLED
code need no changes for patches that fit this contract.

The per-sample callback receives `frequency / sampleRate`, mapped config values
and private state. `makeVoiceRecipe<N>` checks that N fits the engine's
16-float buffer. By default it resets on each note/retrigger. Pass `false` for
free-running state. An optional third argument is a `configure(sampleRate,
config, state)` callback for coefficients that should be calculated only when
controls change; it also runs after a trigger reset. It must preserve running
phase/filter history when called during a held note.

Audio code must not allocate, block, print, or register callbacks dynamically.
Large delay buffers and class-based models still need dedicated fixed storage,
as the existing waveguide and noise engines do. Do not increase the recipe
buffer for a single large effect without checking the cost across four voices.
More presets using the same patch add flash data, not another DSP instance per
preset. Native Hypersaw still uses one `rpdsp::Hypersaw` per voice.

## Selection and verification

In preset settings, pads 0–3 select the voice, pads 6/7 select the previous/next
page, and pads 8–31 select one of 24 presets on that page. Paging alone does not
change the sound. OLED and LEDs use the same page mapping; the final page only
offers populated slots. The current uint8_t IDs allow up to 255 presets.

Applying a preset seeds only its marked timbre tracks, keeping their independent
lengths and preserving note/gate patterns. Startup uses the same seeding path.
Config and state changes continue through the existing bounded control queue.
Source changes wait for gate-low; scalar controls apply immediately. Switching
sources clears their state, so this is not a crossfade or a guarantee that a
release tail will survive a preset change.

Run the host tests after adding a preset:

```powershell
cmake --build build_test --parallel
./build_test/tests/pico2seq_tests.exe --reporter compact
```

The tests enumerate the bank, check seeding, name lookup, pages, bounded output,
recipe control influence, pitch/glide, gate release and staged source changes.
Add a behavior test for any new lifecycle or state requirement. Compile firmware
with `scripts/build_pico2seq.ps1`. Host checks do not measure RP2350 audio load,
aliasing, perceived loudness, or physical controls; audition and profile new
patches on the device, especially nonlinear FM and DSF at high notes.
