# Pico2Seq — User Manual

Pico2Seq is a 4-voice polyphonic step sequencer and synthesizer built around a Raspberry
Pi Pico 2 (RP2350) in a laser-cut wooden chassis. Each of its four synthesizer voices is
driven by its own independent step sequencer, and every sound parameter (pitch, velocity,
filter, envelopes, octave, gate, slide) can run on its own step length, so patterns drift
against each other polymetrically instead of staying locked in lockstep. The playing
surface is a 32-pad capacitive touch grid backed by a mirrored LED matrix, with four
analog faders, an 8-button function set, a magnetic encoder joystick, a hands-free laser
distance sensor, an OLED display, and a USB CDC diagnostics console — all on one panel.

> This manual was compiled from the firmware source and documentation in this repository
> (2026-09-03; updated 2026-09-16 for Project Snapshot persistence / flash session management,
> hot audio in SRAM, the 29-preset sound bank on one browser page, the 55–700 mm lidar window with
> pause-on-out-of-range, and encoder base value editing). The code is authoritative; anything that
> could not be verified against the code is explicitly marked **[unverified]**. Voice
> numbering: the panel and docs use **Voice 1–4**; the internal firmware and some OLED
> screens use 0-based indices **0–3** for the same voices.

---

## Contents

1. [Panel & hardware layout](#1-panel--hardware-layout)
2. [Quick start](#2-quick-start)
3. [Sequencing concepts](#3-sequencing-concepts)
4. [Voices & presets](#4-voices--presets)
5. [Control reference](#5-control-reference)
6. [LED themes](#6-led-themes)
7. [MIDI & connectivity](#7-midi--connectivity)
8. [Firmware & developers](#8-firmware--developers)
9. [Troubleshooting & gotchas](#9-troubleshooting--gotchas)
10. [Glossary](#10-glossary)

---

## 1. Panel & hardware layout

### 1.1 Panel overview (schematic)

Placement of the OLED, encoder, and sensor window is approximate — the firmware defines
their wiring, not their exact panel position. The labeled jacks, LED grid, pad grid, and
slider slots match the physical panel.

```
 +-------------------------------------------------------------------------------+
 |                 PICO2SEQ — control panel (schematic, not to scale)            |
 |                                                                               |
 |   (o) TMAG5273           +----------------+          . . . . . . .            |
 |    magnetic encoder   o  |    OLED 128x64 |   o      . LED matrix .   ||||     |
 |    (rotate/push)      o  |     display    |   o      .  8 wide  x  .   ||||  5  |
 |                          +----------------+   o      .  4 rows     .   ||||  ver- |
 |   +------------+      4 rows x 8 cols         o      . (pad mirror) .   ||||  tical|
 |   | Gate Input |      . . . . . . . .                  . . . . . . .     ||||  slider|
 |   +------------+      .  32 capacitive  .                              ||||  slots |
 |   | Stereo Out  |      .  touch pads    .   [ ] Mode switch               ||||      |
 |   +------------+      .  (step grid)    .      Param / Utility          ||||      |
 |                          . . . . . . . .                                ||||      |
 |   (o) VL53L1X distance-sensor window                     V1 V2 V3 V4   ||||      |
 |                                                            (voice      ||||      |
 |                                                             buttons)   ||||      |
 +-------------------------------------------------------------------------------+
 |   o    o    o    o    o    o    o    o     <- bottom edge: jacks / mounting    |
 +-------------------------------------------------------------------------------+
```

### 1.2 The 32 touch pads (step grid)

A green MPR121 capacitive-touch board behind the wooden panel senses a **4 row x 8 column
grid of 32 pads**. Every pad is a sequencer step — there are no mode/menu functions on the
pads. The grid is organized as **two 16-step banks**:

```
        col:  1  2  3  4  5  6  7  8
 row 1:        1  2  3  4  5  6  7  8      LOW bank  = pads 1–16
 row 2:        9 10 11 12 13 14 15 16     (steps 0–15 of the pair's low voice)
 row 3:        1  2  3  4  5  6  7  8      HIGH bank = pads 17–32
 row 4:        9 10 11 12 13 14 15 16     (steps 0–15 of the pair's high voice)
```

Which voices the two banks address depends on the currently selected voice:

| Selected voice | Low bank (pads 1–16) | High bank (pads 17–32) |
|---|---|---|
| Voice 1 or Voice 2 | Voice 1, steps 1–16 | Voice 2, steps 1–16 |
| Voice 3 or Voice 4 | Voice 3, steps 1–16 | Voice 4, steps 1–16 |

So the panel always shows the **selected voice pair**: the selected voice stays on its own
bank and its pair partner takes the other bank. This mirrors exactly onto the LED matrix.

What pads do, per situation:

- **Normal tap** — toggles the gate (note on/off) of that step on the bank's voice.
- **Long-press a pad** (about 0.4 s) — enters **Step Edit mode** for that step. The
  faders switch to **ENV mode** and edit only that step's envelope (§1.3), the OLED
  shows the step's Attack / Decay / Sustain / Release, and the magnetic encoder edits the
  step's selected parameter.
- **Hold a parameter button + tap a pad** — sets that parameter track's **length** to the
  pad number (pad 5 = 5 steps). This is how you make polymetric tracks (§3.2).
- **Shift + pad** — clears that step (gate off, all parameters back to defaults).
- **While Gate Length mode is active** (hold the Utility-mode encoder button) — a pad sets
  the selected voice's **Gate track length** (2–16 steps) instead of toggling a step.

### 1.3 Faders (slider slots)

The panel carries five vertical slider slots; the firmware exposes **four fader channels**
on the SliderModule tile (12-bit resolution). **[unverified: the fifth slot's function —
the firmware only maps four faders.]** The mode switch does not change the faders.

**Normally** (no step selected), in both mode-switch positions:

| Fader | Controls |
|---|---|
| 1 | Master tempo (uClock BPM, 45–200) |
| 2 | Swing amount (continuous shuffle depth) |
| 3 | Master volume (VoiceManager's gain on Core 1's final mix; saved with the session) |
| 4 | Gate length across the selected voice's active steps |

**ENV mode** — long-press a pad to select a step (Step Edit). The faders then edit **only
that voice and that step**:

| Fader | Voices with an envelope | Strings (no envelope) |
|---|---|---|
| 1 | Attack | Pick hardness |
| 2 | Decay | T60 (ring time) |
| 3 | Sustain level | Pick position |
| 4 | Release | Stiffness |

Hypersaw, NoiseStorm and the recipe voices use faders 1–2 for the engine controls their
Attack/Decay lanes already carry (Detune / Mix, Regen / Chaos, recipe macros); faders 3–4
are their real Sustain and Release.

- The fader position **is** the step's value (absolute): bottom = shortest time / zero
  level, top = longest / full. Values are heard the next time the step plays; a sounding
  note keeps its running stage, so edits never click.
- A step you have not touched **follows the patch** (the preset's own envelope). The OLED
  shows such values in parentheses. **Shift + move a fader** returns that lane of the step
  to the patch value.
- A fader only takes over after an obvious move, so selecting a step never snaps its
  values to wherever the faders rest. The same applies after changing voice or leaving
  Step Edit.
- Faders no longer set voice bases or record live. Use the encoder for bases (§1.6) and
  the distance sensor for live recording (§1.7).

### 1.4 Voice buttons (V1–V4)

Next to the faders: four buttons for **direct voice selection** in both modes.

- **Tap V1–V4** — selects the voice all voice-scoped controls (encoder, distance sensor,
  faders in Param mode) act on. They are also the only way to change the voice the preset
  browser edits.
- **Shift chords** (hold the Shift button, then press a voice button):
  - **Shift + V1** — Play / Stop transport
  - **Shift + V2** — Randomize the selected voice
  - **Shift + V3** — Cycle musical scale
  - **Shift + V4** — Enter **Voice Editing mode** (hold both, then release; transport stops
    and audio mutes — see §5 and [`docs/voice-edit.md`](voice-edit.md))

### 1.5 The 8-button function set & the mode switch

Eight buttons (ButtonModule8) change meaning with the **mode switch** on GPIO 7:
**down/LOW = Param mode**, **up/HIGH = Utility mode**. Bit 7 is **Shift** in both modes.

| # | Param mode | Utility mode |
|---|---|---|
| 1 | Note | Play / Stop |
| 2 | Velocity | Save / Load (tap: save session, hold: reload) |
| 3 | Filter | Scale cycle |
| 4 | Attack | Swing pattern cycle |
| 5 | Decay | LED theme cycle |
| 6 | Octave | Encoder target cycle |
| 7 | Slide | Randomize |
| 8 | Shift | Shift |

Full behavior of each is in §5. When the switch flips, holds and latches are cleared and a
brief **PARAM** / **UTIL** banner appears on the OLED.

### 1.6 TMAG5273 magnetic encoder (joystick)

A 3D hall-effect sensor read as a rotation dial with **velocity-sensitive acceleration**:
turn it slowly for ultra-fine single-step adjustments, quickly to sweep a whole parameter
range. It edits whatever the **encoder target** is — cycle targets with the Utility-mode
"Encoder target" button. The target order is:

**Velocity → Filter → Attack → Decay → Note → Octave → Slide Time → (back to Velocity)**

- Voice targets (Velocity/Filter/Attack/Decay/Note) set that parameter's **base value for
  the selected voice** (its patch value). Steps that follow the patch play it; steps with
  their own recorded value keep theirs — see §9 and
  [`docs/voice-edit.md`](voice-edit.md).
- Note and Octave move one step per short turn; continuous targets follow turn speed.
- Slide Time sets the portamento glide time.
- In Step Edit mode the encoder edits the **selected step's value** instead: the held
  parameter, else the toggled edit parameter, else the encoder target's lane (named at the
  bottom of the OLED's ENV page). A step that follows the patch starts from the patch
  value. Note and Octave move one step per detent.
- While the transport runs, a base change reaches the sounding note at once without
  retriggering it.
- The OLED normally shows the playing step's composed value of the encoder target (or, while
  a parameter button is held, of that parameter — the value live recording writes and the
  voice plays). For 1.5 s after an encoder turn it shows the edited
  **base** instead, marked `Base` on the home screen and `BASE` on a parameter screen. A
  held parameter also shows the current lidar reading in mm.

**Hold** the Utility-mode encoder button (about a second) to enter **Gate Sequence Length
mode**: the LEDs show a blinking band on the selected voice's rows, and touching pads 1–16
sets that voice's Gate track length (2–16 steps). Release the button to exit.

### 1.7 VL53L1X distance sensor

A laser time-of-flight sensor (usable range **55–700 mm**) above the panel. It is the
**hands-free performance controller**: hold (or Shift+latch) a parameter button, then move
your hand over the sensor, and the reading is recorded live into that parameter's sequence
at its currently playing step on the **selected voice** — e.g. sweep Filter over a pattern
without touching anything. Hold several parameter buttons to record them all at once. Each
parameter records at its own position, so a 5-step Filter track is written 5 steps round. Each new step starts from the hand's current height.
Velocity, Filter, Attack and Decay keep recording while their step plays: the step follows
your hand, and the sounding note changes without retriggering — cutoff and velocity at
once, attack and decay from the next note (a running attack or decay keeps its length, so
live edits never click). Hand height **is** the value: near the sensor is the bottom of the
parameter's range, 700 mm the top, whatever the voice's patch value. Note and Octave take one value per note, on the step, so hand
jitter cannot warble a sounding pitch. With the transport stopped the hand writes the step
each parameter is paused on. Pitch recording only
lands while the playing gate is ON. In Step Edit mode the sensor records into the selected
step instead.

Since the Voice Editing mode landed (2026-09-11) the sensor records a **relative modifier**
rather than an absolute value: the reading is normalized to 0–1, the midpoint (≈50 %) is
neutral, and the recorded value offsets the parameter's base up or down. Changing the base
afterwards does not change what you recorded — see [`docs/voice-edit.md`](voice-edit.md).

Moving your hand out of range (above about 740 mm, or no reading at all) **pauses**
recording: steps keep their values instead of jumping to the minimum. While a parameter
button is held, the OLED parameter screen always shows the sensor's current reading in mm
at the right: plain (`412mm`) inside the recording window, in parentheses (`(812mm)`)
outside it, and `--mm` with no measurement. A held parameter's screen takes priority over
the settings and sequence-length screens.

### 1.8 OLED display

A 128x64 monochrome OLED (SH1106G). It renders the highest-priority active view from a
five-tier hierarchy:

1. **Mode banner** — transient `PARAM` / `UTIL` splash when the mode switch flips.
2. **Settings & presets** — a preset browser for
   the selected voice (current name in large type, `<`/`>` neighbors, "Sound Buffet" list
   of all four voices' presets) and voice-architecture toggles (envelope on/off, overdrive
   on/off, filter mode, filter resonance). Reached by stopping the transport or by
   long-pressing Play, which toggles Settings without stopping playback (preset applies
   are staged and click-safe while running).
3. **Gate Sequence Length gauge** — while Gate Length mode is held: voice number, length
   value, and a proportional bar.
4. **Parameter edit screens** — when a parameter button is held/latched or a step is in
   edit: parameter name, voice and step indicators, the formatted value at the playing
   (or selected) step, `LIVE`/`STEP` and `NOTE`/`REST`, and — while a parameter button is
   held — the distance sensor reading in mm. A held parameter outranks tiers 2 and 3.
5. **Voice Editing screens** — while Voice Editing mode is active: `EDIT V1`–`EDIT V4`,
   a modified marker, the parameter name and units, and whether a sequencer lane modifies
   that base.
6. **Status screen** (default) — scale name, shuffle template name, selected voice (shown
   0-based as `Voice: 0`–`Voice: 3`), and a beat-synchronized playhead dot row.

Transient confirmations (`RANDOMIZED` + voice) also appear here,
as does the `ENC: <param> <value>` line while the encoder is active.

### 1.9 LED matrix (pad mirror)

An **8 columns x 4 rows WS2812B RGB matrix** behind the panel's hole grid. It mirrors the
touch pad-for-pad: LED rows 1–2 are the pair's low-voice band, rows 3–4 the high-voice
band. It shows:

- **Step gates** for the visible voice pair (lit = gate on), with a distinct **playhead
  accent** stepping through the 16th-note grid while the transport runs.
- A slow **breathing animation** across the grid when the transport is stopped.
- **Polyrhythmic overlays** — tinted accents show where the Note, Velocity, and Filter
  tracks are within their (different) lengths.
- **Parameter edit / settings / preset selection** feedback while those modes are active.
- In Gate Length mode, a blinking band up to the selected Gate length.

Ten color themes are available (cycle with Utility button 5) — see §6.

### 1.10 Jacks, connectors and power

| Connector | Function |
|---|---|
| **Stereo Out** (3.5 mm jack pair on the panel's left) | The main audio output: 48 kHz, 16-bit stereo I2S audio from the PIO/I2S pins (BCLK GP10, LRCK GP11, DATA GP12), e.g. to a PCM5102A-class DAC. All four voices are mixed here (mono mix duplicated to both channels). |
| **Gate Input** (panel label) | **[unverified]** No gate/external-clock input is referenced anywhere in the firmware source or docs. Treat this panel hole as non-functional in the current firmware. |
| **USB** (Pico 2) | Power + serial diagnostics console at 115200 baud (USB CDC only — USB MIDI was removed 2026-09-06). |
| **Bottom-edge jack row** | Mounting/connector positions for the wired peripherals (I2S DAC, I2C buses, tile bus). **[unverified: exact per-hole assignments are a build-time wiring matter, see `README.md` wiring table.]** |

There are **no hardware gate outputs** in the current firmware: the old GPIO gate/clock
pins were reassigned to the I2S audio output. "Gate" now means the internal gate timing
only — each of the four sequencers owns its note duration and publishes gate-off
to audio on expiry. Nothing is transmitted over MIDI.

---

## 2. Quick start

1. **Connect** the Stereo Out jack(s) to a mixer/amplifier (or headphones via an amp), and
   plug in USB power. The OLED runs its startup animation; on the serial console (115200)
   you'll see boot diagnostics.
2. The transport **starts running by default at 90 BPM**. Nothing plays yet because all
   steps start with their gates off.
3. The four voices boot with presets **Voice 1 = Square, Voice 2 = Bass, Voice 3 =
   Digital, Voice 4 = Percussion**.
4. **Select a voice** — press V1–V4 next to the faders.
5. **Program a pattern** — tap pads in the bank rows for that voice (§1.2). Lit pads are
   active steps; the LED playhead shows where you are. Touch the low bank (rows 1–2) for
   the pair's first voice, the high bank (rows 3–4) for its partner.
6. **Shape the sound** — turn the magnetic encoder (Velocity target by default; cycle
   targets with the Utility encoder button). To shape one step's envelope, long-press its
   pad and use faders 1–4 (Attack / Decay / Sustain / Release).
7. **Try polymeter** — hold a parameter button (e.g. Filter) and tap pad 5: the Filter
   track is now 5 steps long and cycles against the 16-step Gate track.
8. **Change key feel** — hold Shift and tap V3 to cycle through the 13 scales.
9. **Groove** — fader 1 sets tempo and fader 2 swing (in either mode-switch position);
   on **Utility**, button 4 cycles swing templates.
10. **Stop/start** — Utility button 1, or Shift + V1 from anywhere. Stopping opens the
    OLED **preset browser** ("Sound Buffet"); starting again resumes and closes it. A
    long-press of Play toggles the browser without stopping the transport.
11. **Edit the sound itself** — hold Shift + V4 and release to enter **Voice Editing mode**:
    transport stops, slider buttons pick a voice, button tiles navigate the parameter
    catalogue, and the encoder changes the selected base (§5, [`docs/voice-edit.md`](voice-edit.md)).

---

## 3. Sequencing concepts

### 3.1 One sequencer per voice

Each voice has its own completely independent `Sequencer` instance (`seq1`–`seq4`). All
four are advanced by the same uClock 16th-note tick (480 PPQN internal resolution, 120
ticks per 16th-note step, default tempo 90 BPM), but their patterns, lengths, and
parameters are entirely separate. This is what makes polymetry possible: Voice 1 can run a
16-step pattern while Voice 4 runs 13 steps, and they realign only every 208 steps.

### 3.2 Polymetric parameter tracks

Each voice's sequencer holds **nine independent parameter tracks**, one per automatable
parameter, each with its own step count (default 16, adjustable from the pads; the core
supports up to 64):

| # | Parameter | Range | What it does |
|---|---|---|---|
| 0 | **Note** | 0–21 scale steps | Scale-degree index for pitch |
| 1 | **Velocity** | 0–100 % | Voice amplitude |
| 2 | **Filter** | 0–100 % | Filter cutoff (mapped exponentially, ~20 Hz–20 kHz) |
| 3 | **Attack** | 0–1 s | Envelope attack time |
| 4 | **Decay** | 0–1 s | Envelope decay time |
| 5 | **Octave** | −2 / −1 / 0 / +1 / +2 | Quantized octave shift |
| 6 | **GateLength** | 0.1–100 % of a step | How long each note is held |
| 7 | **Gate** | on/off | Whether the step triggers at all |
| 8 | **Slide** | on/off | Portamento into that step (no envelope retrigger; pitch glides) |
| 9 | **Sustain** | 0–100 % | Envelope sustain level (ENV fader 3) |
| 10 | **Release** | 1 ms–10 s | Envelope release time (ENV fader 4) |

Velocity, Filter, Attack, Decay, Sustain and Release steps either hold their own value or
**follow the patch** (play the voice's preset/base value). Fresh and cleared steps follow
the patch.

Every track wraps on its own length (`step modulo trackLength`), so a 16-step Gate track
with an 8-step Filter track, a 5-step Velocity track, and a 3-step Octave track all run
simultaneously and evolve over a common cycle. On the LED matrix, the Note/Velocity/Filter
tracks' independent positions are shown as tinted overlays.

**The Gate track defines the pattern length.** The sequencer's master position is
`clockStep modulo Gate track length`, so the **Gate track's step count is the whole
pattern length for that voice** — make it 16 for a standard bar, 13 for polymetric
madness. Hold the Utility encoder button and tap a pad to set it (2–16 via pads).

Other track behaviors worth knowing:

- **Note edits are gate-protected**: pitch can't be written into a step whose Gate is off
  (from pads, sensor recording, or step edit).
- **Slide steps don't retrigger the envelope**; the pitch slews smoothly into the new note
  at the Slide Time set by the encoder. A gate-off step right after a slide step lets the
  note ring out instead of choking it.
- **Randomize** (Utility button 7 short press, or Shift + V2) applies musical heuristics:
  even steps have a 75 % gate chance, odd steps ~33 %, slides ~8 %, and velocity, filter
  and the envelope spread around each voice's patch values (attacks kept short enough to
  sound inside a short gate). **Long-press** Randomize (≥ 1 s) resets
  the selected voice's parameters instead.
- **Shift + Randomize tap** clears the selected voice completely: every stored step
  value, all gates and slides off, and all track lengths back to their 16-step
  defaults. The OLED confirms with `CLEARED` + voice number.
- **Shift + Randomize long-press** clears **all four voices** the same way — the whole
  project starts fresh with no values and no gates (presets, tempo and transport
  state are kept). The OLED confirms with `ALL CLEAR`.

### 3.3 Scales

Pitch is quantized to one of **13 built-in scales**, each a 48-step (4-octave) semitone
table; the Note parameter (0–21) indexes into it. Internal synthesis is voiced around C3.
Cycle scales with **Shift + V3** or Utility button 3:

| Index | Scale | Character |
|---|---|---|
| 0 | Ionian Major | Bright, standard major |
| 1 | Dorian | Jazzy minor with raised 6th |
| 2 | Phrygian | Dark, Spanish flat-2nd flavor |
| 3 | Lydian | Dreamy, raised 4th |
| 4 | Mixolydian | Bluesy rock major with flat 7th |
| 5 | Aeolian Minor | Natural minor, melancholic |
| 6 | Locrian | Tense, unstable |
| 7 | Pentatonic Minor | Blues/rock (padded to 48 steps) |
| 8 | Phrygian Dominant | Middle Eastern / flamenco |
| 9 | Lydian Dominant | Acoustic / overtone scale |
| 10 | Harmonic Minor | Dramatic classical minor |
| 11 | Wholetone | Symmetrical, impressionistic |
| 12 | Chromatic | All 12 semitones, 1:1 mapping |

The **Octave** parameter is quantized to five discrete positions mapped from hand distance:
- **-2 octaves**: sensor minimum (55 mm) to 90 mm (stored `0.00`)
- **-1 octave**: 91 mm to 280 mm (stored `0.25`)
- **0 octaves**: 281 mm to 425 mm (stored `0.50`)
- **+1 octave**: 426 mm to 550 mm (stored `0.75`)
- **+2 octaves**: 551 mm to sensor maximum 700 mm (stored `1.00`)

### 3.4 Shuffle / swing

Timing groove comes from **16 shuffle templates** (per-16th-note micro-timing offsets at
480 PPQN; positive = late, negative = early). Cycle with Utility button 4:

| # | Name | Feel |
|---|---|---|
| 0 | No Shuffle | Straight 16ths |
| 1 | Teeny Swing | Subtle humanized micro-swing |
| 2 | Lil' Swing (53%) | Classic light groove |
| 3 | Neg' Swing (53%) | Pushed / rushed upbeats |
| 4 | CornBread | Asymmetric organic groove |
| 5 | Swing (55%) | Medium standard swing |
| 6 | Swing (56%) | Moderate jazz/house swing |
| 7 | Swing (57%) | Pronounced dance swing |
| 8 | Swing (60%) | Triplet-feel swing |
| 9 | Big Swang (60%) | Heavy laid-back swing |
| 10 | Phatty Swang | Deep MPC-style swing |
| 11 | Big Swang (62%) | Extreme hard swing |
| 12 | Humanize 1 | Micro-timing drummer variation |
| 13 | Humanize 2 | Loose unquantized live feel |
| 14 | Hip-Hop | Boom-bap asymmetric late swing |
| 15 | Funk Groove | Syncopated funk pocket |

Utility **fader 2** adds continuous swing amount on top of the selected template.

---

## 4. Voices & presets

### 4.1 The DSP chain

Each voice runs a full synthesis chain at 48 kHz on the audio core:

```
 sequencer step values (pitch, velocity, envelope, gate, slide)
        |
        v
 SOURCE STAGE (one of five engines, chosen by the preset)
   - Oscillator bank: up to 3 oscillators — band-limited B-spline saw/pulse,
     band-limited hard-sync saw (master/slave pair), sine, triangle, naive
     saw/square, or raw white noise; per-osc detune
     (semitones) and harmony (scale steps)
   - Waveguide: Karplus-Strong plucked string (T60 tail, brightness, pick
     position/hardness, stiffness, two-string detune)
   - Noise-FX texture: noise + pitch-tracked Lorenz chaos growl through a
     prime-tap diffuser and a regenerative allpass swarm
   - Hypersaw: one rpdsp::Hypersaw (seven internal detuned saw voices;
     detune and mix driven by the re-purposed sequencer slots)
   - Recipe engine: fixed-state modular rpdsp synthesis patches with up to
     three mapped timbre controls (FM, phase distortion, DSF, formant synthesis,
     ring modulation, reversing sync, spectral and chaotic prisms)
        |
        v
 ADSR ENVELOPE (pre-filter VCA; attack/decay edited per step or live)
        |
        v
 OVERDRIVE waveshaper (per preset, optional; toggle in Settings)
        |
        v
 MAIN FILTER — ladder (Analog + Lead only) or state-variable SVF (all
 other filtered presets); multi-mode: LP24, LP12, BP24, BP12, HP24, HP12,
 with resonance (and ladder drive on the two ladder voices); cutoff
 tracks the envelope
        |
        v
 HIGH-PASS filter (per preset, tames lows)
        |
        v
 voice output level -> summed with the other 3 voices -> Stereo Out
```

**The delay effect was removed** (2026-09-11): the global delay line, its boot
parameters, and every control that drove it (Utility button 2, Shift + V4,
Utility fader 3, and the encoder's Delay Time / Delay Feedback targets) are
gone from the codebase, reclaiming ~338 KiB of RAM.

Filter **mode** (LP24 … HP12) and **resonance** are cycled/set from the OLED Settings
screen's voice-parameter page; envelope and overdrive can be switched off per voice there
too. On SVF voices the mode picks the response (LP→low-pass, BP→band-pass, HP→high-pass);
only **Analog** and **Lead** still run the true ladder filter.

### 4.2 The 29 presets

The sound bank contains 29 built-in presets (held as `constexpr` flash tables), all on one browser page in Settings mode. Preset *n* sits on pad *n*−1:

#### Pads 0–23: Presets 1–24

| # | Preset | Engine | Character | Timbre controls (Filter / Attack / Decay) |
|---|---|---|---|---|
| 1 | **Analog** | osc | Single band-limited hard-sync saw (master/slave pair) through 24 dB ladder filter | Cutoff / Attack / Decay (Velocity = Slave pitch) |
| 2 | **Digital** | osc | Dual band-limited square pair (near-unison detune), sharp 12 dB lowpass | Cutoff / Attack / Decay |
| 3 | **Bass** | osc | Sub-octave sine + triangle bass with subtle overdrive | Cutoff / Attack / Decay |
| 4 | **Lead** | osc | Dual-saw lead with a scale-harmony layer on the second oscillator | Cutoff / Attack / Decay |
| 5 | **Square** | osc | Narrow PWM pulse (20% width) with resonant bite; no sustain | Cutoff / Attack / Decay |
| 6 | **Pad** | osc | Atmospheric 3-oscillator chord pad (harmonies 0/+4/+9), slow attack & release | Cutoff / Attack / Decay |
| 7 | **Percussion** | osc | Fast-decaying noise-textured hit (no oscillators, pure noise burst) | Cutoff / Attack / Decay |
| 8 | **SubFunk** | osc | Sub-octave sine/triangle sub bass with warm overdrive grit | Cutoff / Attack / Decay |
| 9 | **RubberSub** | osc | Rubbery sub bass: sub-octave square under resonant BP honk, snappy drive | Cutoff / Attack / Decay |
| 10 | **WgPluck** | waveguide | Classic Karplus-Strong plucked string; bright burst, natural tail | Brightness / Pick hardness / T60 tail |
| 11 | **WgNylon** | waveguide | Dark, felt-soft nylon string; heavily damped, long sympathetic tail | Brightness / Pick hardness / T60 tail |
| 12 | **WgBell** | waveguide | Stiff dispersive waveguide; inharmonic bell/kalimba partials, quick tail | Brightness / Pick hardness / T60 tail |
| 13 | **WgShimmer** | waveguide | Wide-detuned (26¢) two-string course; slow chorusing sustain, pad-like tail | Brightness / Pick hardness / T60 tail |
| 14 | **Hypersaw** | hypersaw | Native seven-voice `rpdsp::Hypersaw` stack; wide detune range | Cutoff / Detune / Center-Side Mix |
| 15 | **NoiseStorm** | noise-FX | Noise + Lorenz chaos growl through prime-tap diffuser and allpass swarm | Swarm color / Swarm regen / Chaos level |
| 16 | **FMGlass** | recipe | 2-operator FM glass chime: carrier/modulator with feedback | Index / Ratio / Feedback |
| 17 | **FMBass** | recipe | Punchy FM bass with tight transient snap and modulated body | Index / Ratio / Feedback |
| 18 | **PhaseMorph** | recipe | Phase-distortion morphing oscillator sweeping between waveshapes | Morph / Depth / Drive |
| 19 | **Spectral** | recipe | Spectral harmonic oscillator stack with animated formants | Shift / Spread / Focus |
| 20 | **Prism** | recipe | Dispersive multi-partial prism cluster with crystalline timbre | Spread / Damping / Color |
| 21 | **ChaosPrism** | recipe | Chaotic non-linear prism texture with pitch-tracked divergence | Chaos / Spread / Edge |
| 22 | **VelvetKeys** | recipe | Soft electric keys: dual `osc_fbfm` operators at 2:1 ratio, warm release | Index / Ratio / Feedback |
| 23 | **CopperBass** | recipe | Rounded DSF bass: harmonic spacing with a sub sine from `osc_pdmorph` | Bright / Spacing / Sub |
| 24 | **ReedPipe** | recipe | Held acoustic reed tone: `osc_formant` bursts blended with sine fundamental | Formant / Bloom / Body |

#### Pads 24–28: Presets 25–29

| # | Preset | Engine | Character | Timbre controls (Filter / Attack / Decay) |
|---|---|---|---|---|
| 25 | **SilkPad** | recipe | Slow orchestral swell: two detuned `osc_pdmorph` voices, 1.25s release | Silk / Detune / Blend |
| 26 | **HollowBell** | recipe | Hollow metallic bell: dual `osc_pdmorph` ring-modulated at 2:1, zero sustain | Ratio / Edge / Ring |
| 27 | **SyncLead** | recipe | Melodic sync lead: `osc_revsync` blended with pitched `osc_pdmorph` body | Sync / Edge / Bite |
| 28 | **OrbitPluck** | recipe | Metallic pluck: sine-modulated `osc_tzfm` with clean fundamental body | Index / Ratio / Body |
| 29 | **AirChime** | recipe | Ethereal harmonic chime: `osc_prism` blended with octave sine, slow decay | Focus / Spread / OctMix |

#### Preset Selection in Settings Mode

Presets live in flash and are auditioned and applied per voice in the **preset browser** (long-press Play, or stop the transport to open Settings on the OLED):
- **Voice Selection**: Press the SliderModule **V1–V4** buttons to switch which voice is being edited. Pads never change the voice while Settings is open.
- **Applying Presets**: Touch **pads 0–30** (the lit pads on the LED matrix mirror; pads 0–28 hold today's 29 presets) to instantly assign that preset to the active voice. Pad 31 is unassigned. There are no pages.
- **Voice Parameters**: Pressing the encoder button toggles the Settings screen between the preset browser and the voice-parameter toggles (envelope, overdrive, filter mode, filter resonance).

---

## 5. Control reference

### Touch pads (MPR121 grid)

| Gesture | Result |
|---|---|
| Tap a pad | Toggle that step's gate on the pad's bank voice |
| Long-press a pad (~0.4 s) | Enter Step Edit mode for that step (encoder/faders/sensor edit it; OLED shows values) |
| Shift + pad | Clear that step (gate off, parameters to defaults) |
| Hold a parameter button + tap pad | Set that parameter track's length to the pad number |
| Pad press while Gate Length mode is held | Set the selected voice's Gate track length (2–16 steps) |
| Tap a pad while the preset browser is open | Apply that preset to the selected voice — pads 0–28 = presets 1–29 (pads 0–30 are preset slots); V1–V4 switch the target voice |

### Faders

| Fader | No step selected (both modes) | Step Edit = ENV mode |
|---|---|---|
| 1 | Tempo (45–200 BPM) | Step's Attack (strings: Pick) |
| 2 | Swing amount | Step's Decay (strings: T60) |
| 3 | Unassigned | Step's Sustain (strings: Position) |
| 4 | Gate length across active steps | Step's Release (strings: Stiffness) |

In ENV mode the fader position is the step's absolute value; Shift + move returns that lane
of the step to the patch value. See §1.3.

### Buttons — Param mode (mode switch LOW)

| Button | Action |
|---|---|
| Note / Velocity / Filter / Attack / Decay / Octave | Hold to arm real-time recording for that parameter (distance sensor / encoder); auto-selects it as the encoder target |
| Shift + tap a parameter | **Latches** the hold (no finger needed). One latch at a time: pressing another parameter moves the latch; tapping the latched one clears it |
| Slide | Toggles slide/portamento mode (clears conflicting edit modes) |
| Shift | Modifier for latches and voice-button chords |

### Buttons — Utility mode (mode switch HIGH)

| Button | Action |
|---|---|
| 1 Play / Stop | Start/stop the transport (and all 4 sequencers). Stopping opens the OLED Settings/preset browser; starting closes it. Long-press toggles Settings without stopping |
| 2 Save / Load | **Tap**: save everything to flash (the transport pauses ~0.5 s for the write, then resumes). **Long-press (≥ 0.4 s)**: reload the last saved session. OLED shows `SAVED` / `LOADED` / `LOAD ERR` |
| 3 Scale | Cycle forward through the 13 scales |
| 4 Swing | Cycle through the 16 shuffle templates |
| 5 Theme | Cycle the 10 LED matrix color themes |
| 6 Encoder target | Short press: cycle encoder target. Hold: enter Gate Sequence Length mode (pads set the Gate track length) |
| 7 Randomize | Short press (< 1 s): randomize the selected voice. Long press (≥ 1 s): reset it. **Shift + tap**: clear the selected voice's whole pattern (values, gates, track lengths). **Shift + long-press**: clear all four voices |
| 8 Shift | Modifier for transport/utility chords |

**What gets saved:** all four voices' patterns (every parameter lane, including
polymetric lengths), each voice's patch (preset + all Voice Edit adjustments),
tempo, master volume, scale, shuffle, theme, selected voice, and the recorded
encoder-modifier state. Edits are also saved automatically about a second after
you press Stop (only if something changed). Everything you program survives
power-off, and after a watchdog freeze the unit restores the live session by
itself — no power-cycle needed.

### Voice buttons (both modes)

| Gesture | Result |
|---|---|
| Tap V1–V4 | Select voice 1–4 |
| Shift + V1 | Play / Stop |
| Shift + V2 | Randomize selected voice (short-press behavior) |
| Shift + V3 | Cycle scale |
| Shift + V4 | Enter Voice Editing mode (hold both, then release) |

### Voice Editing mode (Shift + V4)

Hold **Shift**, press **V4**, then release both. All four sequencers stop and audio is
muted. Slider buttons 1–4 select the voice being edited; the button tile navigates the
parameter catalogue (group prev/next = buttons 1/2, parameter prev/next = buttons 3/4,
button 5 held = fine adjust, button 6 held 700 ms = reset to the loaded preset's value,
button 7 = control guide, button 8 = exit). The encoder changes the selected parameter's
base. Edits stay in RAM until a preset is loaded or power is lost. Full details:
[`docs/voice-edit.md`](voice-edit.md).

### Sensors & encoder

| Gesture | Result |
|---|---|
| Turn magnetic encoder | Adjust the active encoder target; slow = fine, fast = coarse (velocity-sensitive) |
| Utility button 6 | Change encoder target (Velocity → Filter → Attack → Decay → Note → Octave → Slide Time) |
| Hold Utility button 6 | Gate Sequence Length mode |
| Move hand over VL53L1X while a parameter is armed | Hands-free live recording of a relative modifier into that parameter's sequence at its playing step on the selected voice, continuously while held and heard at once (midpoint ≈ neutral) |
| Mode switch (GPIO 7) | Select Param (LOW) or Utility (HIGH) button set; shows a banner on flip |
| Shift + V4 (hold, release) | Enter Voice Editing mode (transport stops; see above) |

---

## 6. LED themes

Ten color themes for the 8x4 LED matrix, cycled with **Utility button 5**. Enum names are
from the firmware; display names are as documented in `docs/LEDMatrix.md`:

| # | Enum | Display name | Character |
|---|---|---|---|
| 0 | `DEFAULT` | Standard | Classic blue/green palette |
| 1 | `OCEANIC` | Oceanic | Deep ocean blues, cyan and teal |
| 2 | `VOLCANIC` | Volcanic | Intense reds, fiery oranges, warm ambers |
| 3 | `FOREST` | Forest | Earthy greens, moss, warm browns |
| 4 | `NEON` | Neon | Vibrant magenta, purple, electric cyan |
| 5 | `MODERN` | Modern | Refined muted tones, high legibility |
| 6 | `DARK_NOCTIS` | Dark Noctis | Low-light stealth, cool midnight blue |
| 7 | `DARK_EMBER` | Dark Ember | Low-light, warm glowing ember |
| 8 | `BLUE` | Blue Contrast | High-contrast monochrome blue |
| 9 | `GREEN` | Green Contrast | High-contrast monochrome green |

---

## 7. MIDI & connectivity

### USB MIDI was removed (2026-09-06)

Pico2Seq does **not** transmit or receive any MIDI. The USB port enumerates as a CDC serial
device (Adafruit TinyUSB stack) and carries power plus the 115200-baud diagnostics console
only. The dormant MIDI trackers and stub send paths have also been removed;
all four voices use their sequencer's note-duration lifecycle. There is no CC
output, no MIDI clock, and no MIDI-in feature.

### Clock & timing internals (for the curious)

uClock runs at 480 PPQN (120 ticks per 16th-note step); each 16th note advances all four
sequencers, ticks gate timers, and drives the internal note lifecycle. Default tempo
90 BPM, range 45–200 via Utility fader 1.

### Gate I/O

There is no hardware gate input or output in the current firmware (see §1.10), and with
USB MIDI gone there is no built-in way to gate external gear. The panel's "Gate Input"
label is a leftover from the hardware design **[unverified]**.

---

## 8. Firmware & developers

- **The firmware is an Arduino sketch** (`Pico2Seq.ino`), built and flashed with the
  Arduino IDE (or Arduino CLI) — there is no CMake firmware build. The CMake project only
  builds the host-side test suite.
- Board: **Raspberry Pi Pico 2 (RP2350)**, USB stack **Adafruit TinyUSB**.
- Required Arduino libraries (versions as verified in `README.md`):
  - Adafruit MPR121 1.2.1
  - Adafruit VL53L1X 3.1.2
  - Adafruit SH110X 2.1.15
  - Adafruit TinyUSB Library 3.7.7
  - FastLED 3.9.20
  - uClock **2.2.1 from the Arduino library manager** (stock `<uClock.h>`; its rp2040
    backend runs the timer ISR on core 0 — the control core. Upstream 2.3.0 changed the
    callback API; re-verify before upgrading.)
  - *(The MIDI Library is no longer used — USB MIDI was removed 2026-09-06.)*
- Clone with `git clone --recurse-submodules` (`src/rpdsp/` and `src/VelocityEncoder/` are
  submodules).
- A verified command-line compile path (Windows PowerShell staging script) is documented
  in `README.md` ("Building with Arduino CLI on Windows"); it compiles but does not
  upload.
- **On-hardware behavior can only be verified on a real Pico 2** — CLI builds prove
  compilation only.

For developers working on this repo, the host test suite (Catch2 v3 across 4 test executables, 315 tests total, no hardware needed):

```bash
cmake -B build_test -DCMAKE_BUILD_TYPE=Debug
cmake --build build_test --parallel
./build_test/tests/pico2seq_tests --reporter console     # or: ctest --test-dir build_test
./build_test/tests/pico2seq_tests "[sequencer]"          # run one tag/group
```

---

## 9. Troubleshooting & gotchas

**Behavioral gotchas (by design, verified in code):**

- **All four voices (0–3) use sequencer-owned note durations**; `VoiceSystem`
  holds voice IDs and control snapshots, not a second gate timer.
  Nothing is transmitted over MIDI. Internal voice
  indices are 0-based (0–3); the OLED shows `Voice: 0`–`Voice: 3` and `V0`–`V3` on edit
  screens, while the voice buttons and this manual say V1–V4.
- **The encoder edits per-voice bases or the step in edit.** Each voice stores its own
  base values in its patch; turning the encoder changes the selected voice's base, which
  every step that follows the patch plays. With a step selected for edit
  (`uiState.selectedStepForEdit >= 0`), it edits that step's value instead, and the faders
  edit that step's envelope.
- **Can't program a pitch into a step?** Note edits (sensor, encoder step edit) are
  rejected on gate-off steps. Toggle the step on first.
- **Lidar or fader seems to record nowhere?** Check the OLED: the ENV page (`S5` with
  Attack/Decay/Sustain/Release rows) means a step is in Step Edit, so recording goes to
  that step only. Long-press the same pad again (or tap
  any pad) to leave Step Edit and record into the playing steps again.
- **Pad does something unexpected** — check the context: a held parameter button turns pad
  presses into track-length setting; Gate Length mode turns them into Gate length; Shift
  turns them into clear-step. All pads are step pads; there is no pad "menu".
- **Stopping the transport opens the preset browser** on the OLED (a Play long-press
  toggles it without stopping). That is intentional; press Play to leave it.
- **Distance sensor dead?** It is used across 55–700 mm only; readings more than 40 mm
  outside that window count as no hand, and recording pauses. Bright sunlight or the LED
  matrix at full brightness can cause optical jitter. The `[DIAG C0]` serial line reports
  `lidar=<mm> st=<status>` every 2 s.
- **ToF recorded value stuck at one end** — the raw distance is rebased by 55 mm
  (`MIN_DISTANCE_HEIGHT_MM`) and normalized over the 645 mm span, so the nearest usable
  position (55 mm) records the bottom of the range and 700 mm the top. (Before 2026-09-19
  recordings were offsets from the base, and a base at 0 swallowed every low hand height.)
- **A voice goes silent on some steps** — check those steps' Attack and Decay on the ENV
  page. A voice with sustain 0 (Square, Percussion) ends at the attack peak, so a very
  short decay or an attack longer than the gate barely sounds. Shift + move the fader to
  return the lane to the patch value.
- **Fader "jumps" when it takes over** — after a mode flip, a voice change or entering /
  leaving Step Edit, a fader does nothing until it moves by about 1.5 % of its travel; the
  value then jumps to where the fader is.
- **No sound after editing?** Voice Editing mode leaves the transport stopped when you
  exit — press Play to resume.

**Hardware checks (from `docs/sensors.md`):**

- **MPR121 pads unresponsive** (I2C `0x5A`): check GP4/GP5 wiring and the address jumper;
  avoid touching pads during boot.
- **TMAG5273 not detected** (I2C `0x35`): check 3.3 V, pull-ups, and that the diametric
  magnet sits on-axis ~1–3 mm above the sensor. Erratic readings = off-axis magnet.
- **VL53L1X init fails** (I2C `0x29`): check bus wiring and the 50 ms stabilization delay.
- **OLED blank** (I2C `0x3C`): check `Wire` (GP4/GP5) shared bus.
- **Audio distortion/clicks**: the output is a mono mix duplicated to both I2S channels at
  48 kHz; excessive per-voice overdrive + preset output levels can clip the 16-bit
  converter.
- **LED matrix dim/flickering**: the 8x4 WS2812B matrix on GP1 wants a 5 V rail capable of
  ~1.5 A for full white; firmware caps brightness at 120/255.

**Documentation staleness notes (for readers cross-referencing other docs):**

- `docs/voice.md` oversells "scale injection": voices read the live global scale table at
  pitch time, so scale changes apply to subsequent notes on all voices regardless of any
  earlier injection — don't assume voices are frozen with an old scale.

---

## 10. Glossary

| Term | Meaning |
|---|---|
| **Base + modifier** | Parameters combine a per-voice base value (encoder/voice-edit) with a recorded relative modifier (lidar); midpoint is neutral — see [`docs/voice-edit.md`](voice-edit.md) |
| **Bank** | One of the two 16-pad halves of the touch grid; banks map to the selected voice pair |
| **Gate** | The on/off trigger state of a step; also the Gate parameter track whose length defines the voice's whole pattern length |
| **GateLength** | Fraction of a 16th-note step a note is held (0.1–100 %) |
| **Latch (Shift latch)** | Shift + parameter tap keeps the parameter "held" without finger contact; one latch at a time |
| **Pad-mirror** | The LED matrix displays exactly the touch grid's (band, step) positions |
| **Param / Utility mode** | The GP7 mode switch's two button function sets on the 8-button tile |
| **ParamId** | The eleven automatable per-step parameters (Note, Velocity, Filter, Attack, Decay, Octave, GateLength, Gate, Slide, Sustain, Release) |
| **ENV mode** | Step Edit's fader layout: faders 1–4 edit the selected step's Attack / Decay / Sustain / Release |
| **Follows the patch** | A step with no value of its own on an absolute lane; it plays the voice's preset/base value |
| **Polymetric / polymeter** | Parameter tracks of different lengths cycling against each other on the same voice |
| **PPQN** | Pulses per quarter note; the internal clock runs at 480 PPQN (no MIDI clock is transmitted) |
| **Preset** | One of 15 factory voice configurations (§4.2) |
| **Shuffle template** | One of 16 per-16th-note micro-timing groove tables |
| **Slide** | Per-step portamento flag: no envelope retrigger, pitch glides over the Slide Time |
| **Sound Buffet** | The OLED overview of all four voices' current presets, shown in the Settings screen |
| **Step Edit mode** | Long-press a pad to select that step; the encoder and sensor edit its parameters and the faders its envelope (ENV mode) |
| **Voice Editing mode** | Shift + V4: stop transport and edit any voice's sound parameters directly (encoder + button tiles) |
| **Waveguide engine** | Karplus-Strong physical modeling of a plucked string (presets 10–13) |

---

*Pico2Seq — 4 voices, 32 pads, infinite polymeter. Manual generated from firmware sources
and `docs/`; see `README.md`, `docs/architecture.md` and the subsystem docs for the
engineering detail behind every feature described here.*
