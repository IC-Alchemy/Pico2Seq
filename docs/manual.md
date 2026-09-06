# Pico2Seq — User Manual

Pico2Seq is a 4-voice polyphonic step sequencer and synthesizer built around a Raspberry
Pi Pico 2 (RP2350) in a laser-cut wooden chassis. Each of its four synthesizer voices is
driven by its own independent step sequencer, and every sound parameter (pitch, velocity,
filter, envelopes, octave, gate, slide) can run on its own step length, so patterns drift
against each other polymetrically instead of staying locked in lockstep. The playing
surface is a 32-pad capacitive touch grid backed by a mirrored LED matrix, with four
analog faders, an 8-button function set, a magnetic encoder joystick, a hands-free laser
distance sensor, an OLED display, and USB MIDI — all on one panel.

> This manual was compiled from the firmware source and documentation in this repository
> (2026-09-03). The code is authoritative; anything that could not be verified against the
> code is explicitly marked **[unverified]**. Voice numbering: the panel and docs use
> **Voice 1–4**; the internal firmware and some OLED screens use 0-based indices
> **0–3** for the same voices.

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
- **Long-press a pad** (about 0.4 s) — enters **Step Edit mode** for that step: the OLED
  shows the step's parameter values, and the magnetic encoder / faders then edit that
  specific step.
- **Hold a parameter button + touch pads** — real-time/step parameter entry (see
  §5). Note *pitch* can only be written into steps whose gate is ON.
- **Hold a parameter button + tap a pad** — sets that parameter track's **length** to the
  pad number (pad 5 = 5 steps). This is how you make polymetric tracks (§3.2).
- **Shift + pad** — clears that step (gate off, all parameters back to defaults).
- **While Gate Length mode is active** (hold the Utility-mode encoder button) — a pad sets
  the selected voice's **Gate track length** (2–16 steps) instead of toggling a step.

### 1.3 Faders (slider slots)

The panel carries five vertical slider slots; the firmware exposes **four fader channels**
on the SliderModule tile (12-bit resolution). **[unverified: the fifth slot's function —
the firmware only maps four faders.]**

**Param mode** (mode switch toward Param) — faders edit the selected voice, live, and
record into the armed step while a step is in Step Edit:

| Fader | Controls |
|---|---|
| 1 | Filter cutoff |
| 2 | Attack time |
| 3 | Decay time |
| 4 | Velocity |

**Utility mode** (mode switch toward Utility):

| Fader | Controls |
|---|---|
| 1 | Master tempo (uClock BPM, 45–200) |
| 2 | Swing amount (continuous shuffle depth) |
| 3 | Delay feedback mix (0 – 0.91) |
| 4 | Gate length across the selected voice's active steps |

### 1.4 Voice buttons (V1–V4)

Next to the faders: four buttons for **direct voice selection** in both modes.

- **Tap V1–V4** — selects the voice all voice-scoped controls (encoder, distance sensor,
  faders in Param mode) act on.
- **Shift chords** (hold the Shift button, then press a voice button):
  - **Shift + V1** — Play / Stop transport
  - **Shift + V2** — Randomize the selected voice
  - **Shift + V3** — Cycle musical scale
  - **Shift + V4** — Toggle the delay effect on/off

### 1.5 The 8-button function set & the mode switch

Eight buttons (ButtonModule8) change meaning with the **mode switch** on GPIO 7:
**down/LOW = Param mode**, **up/HIGH = Utility mode**. Bit 7 is **Shift** in both modes.

| # | Param mode | Utility mode |
|---|---|---|
| 1 | Note | Play / Stop |
| 2 | Velocity | Delay on/off |
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

**Velocity → Filter → Attack → Decay → Note → Delay Time → Delay Feedback → Slide Time →
(back to Velocity)**

- Voice targets (Velocity/Filter/Attack/Decay/Note) act as offsets on the selected voice.
  Note: at step time these base offsets are applied to **all four voices** (per-voice
  `encoderBaseValues` in `EncoderManager`) — see §9.
- Delay Time / Delay Feedback edit the global master delay.
- Slide Time sets the portamento glide time.
- While the encoder is controlling a parameter, the OLED status screen shows
  `ENC: <parameter> <value>`.

**Hold** the Utility-mode encoder button (about a second) to enter **Gate Sequence Length
mode**: the LEDs show a blinking band on the selected voice's rows, and touching pads 1–16
sets that voice's Gate track length (2–16 steps). Release the button to exit.

### 1.7 VL53L1X distance sensor

A laser time-of-flight sensor (effective range **74–1400 mm**) above the panel. It is the
**hands-free performance controller**: hold (or Shift+latch) a parameter button, then move
your hand over the sensor, and the distance is written live into the armed parameter of the
currently playing step on the **selected voice** — e.g. sweep Filter over a pattern without
touching anything. Pitch recording only lands on steps whose gate is ON. In Step Edit mode
the sensor edits the selected step instead.

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
   edit: parameter name, voice (`V0`–`V3`, 0-based) and step (`S1`–`S16`) indicators, the
   formatted value (Hz for Filter, seconds for Attack/Decay, % for Velocity/GateLength,
   `-1/0/+1` for Octave, `ON/OFF` for Gate/Slide) and a progress bar.
5. **Status screen** (default) — scale name, shuffle template name, selected voice (shown
   0-based as `Voice: 0`–`Voice: 3`), and a beat-synchronized playhead dot row.

Transient confirmations (`DELAY ON`, `DELAY OFF`, `RANDOMIZED` + voice) also appear here,
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
| **USB** (Pico 2) | Power + USB MIDI (see §7) + serial diagnostics console at 115200 baud. |
| **Bottom-edge jack row** | Mounting/connector positions for the wired peripherals (I2S DAC, I2C buses, tile bus). **[unverified: exact per-hole assignments are a build-time wiring matter, see `README.md` wiring table.]** |

There are **no hardware gate outputs** in the current firmware: the old GPIO gate/clock
pins were reassigned to the I2S audio output. "Gate" now means the internal gate timing
(and, for voices 1–2, USB MIDI note on/off).

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
6. **Shape the sound** — make sure the mode switch is on **Param**, then use faders 1–4
   (Filter / Attack / Decay / Velocity) or turn the magnetic encoder (Velocity target by
   default).
7. **Try polymeter** — hold a parameter button (e.g. Filter) and tap pad 5: the Filter
   track is now 5 steps long and cycles against the 16-step Gate track.
8. **Change key feel** — hold Shift and tap V3 to cycle through the 13 scales.
9. **Groove** — flip the mode switch to **Utility**: fader 1 sets tempo, button 4 cycles
   swing templates, button 2 toggles the delay.
10. **Stop/start** — Utility button 1, or Shift + V1 from anywhere. Stopping opens the
    OLED **preset browser** ("Sound Buffet"); starting again resumes and closes it. A
    long-press of Play toggles the browser without stopping the transport.

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
| 5 | **Octave** | −1 / 0 / +1 | Quantized octave shift |
| 6 | **GateLength** | 0.1–100 % of a step | How long each note is held |
| 7 | **Gate** | on/off | Whether the step triggers at all |
| 8 | **Slide** | on/off | Portamento into that step (no envelope retrigger; pitch glides) |

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
  even steps have a 75 % gate chance, odd steps ~33 %, slides ~8 %, short attacks and
  medium decays weighted, filter swept 20–95 %. **Long-press** Randomize (≥ 1 s) resets
  the selected voice's parameters instead.

### 3.3 Scales

Pitch is quantized to one of **13 built-in scales**, each a 48-step (4-octave) semitone
table; the Note parameter (0–21) indexes into it. Internal synthesis is voiced around C3;
USB MIDI notes out are centered at C2 (an octave lower) so external gear sits in a
standard register. Cycle scales with **Shift + V3** or Utility button 3:

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

The **Octave** parameter is quantized to three positions: values below 0.15 transpose down
an octave, above 0.40 up an octave, in between is nominal.

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
 SOURCE STAGE (one of four engines, chosen by the preset)
   - Oscillator bank: up to 3 oscillators — band-limited B-spline saw/pulse,
     sine, triangle, naive saw/square, or raw white noise; per-osc detune
     (semitones) and harmony (scale steps)
   - Waveguide: Karplus-Strong plucked string (T60 tail, brightness, pick
     position/hardness, stiffness, two-string detune)
   - Noise-FX texture: noise + pitch-tracked Lorenz chaos growl through a
     prime-tap diffuser and a regenerative allpass swarm
   - Hypersaw: one rpdsp::Hypersaw (seven internal detuned saw voices;
     detune and mix driven by the re-purposed sequencer slots)
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

A **global delay effect** is on by default (boot time 667 ms, feedback 0.45); toggle it
with Utility button 2 or Shift + V4, set its feedback with Utility fader 3 and its
time/feedback with the encoder's Delay Time / Delay Feedback targets.
(Currently compiled out: `PICO2SEQ_ENABLE_DELAY_EFFECT = 0` in `src/FeatureConfig.h`
removes the effect and all of the controls above to reclaim ~338 KiB of RAM. Set the
switch to 1 and rebuild to bring them back.)

Filter **mode** (LP24 … HP12) and **resonance** are cycled/set from the OLED Settings
screen's voice-parameter page; envelope and overdrive can be switched off per voice there
too. On SVF voices the mode picks the response (LP→low-pass, BP→band-pass, HP→high-pass);
only **Analog** and **Lead** still run the true ladder filter.

### 4.2 The 15 presets

| # | Preset | Character |
|---|---|---|
| 1 | **Analog** | Triple-saw classic subtractive synth through a warm 24 dB ladder filter |
| 2 | **Digital** | Square + triangle hybrid (osc 2 up an octave), sharp 12 dB lowpass |
| 3 | **Bass** | Deep sub-octave detuned sine/triangle bass |
| 4 | **Lead** | Dual-saw lead with a scale-harmony layer on the second oscillator |
| 5 | **Square** | Narrow PWM pulse (20 % width) with resonant bite; no sustain |
| 6 | **Pad** | Atmospheric 3-oscillator chord pad (harmonies 0/+4/+9), slow attack & release |
| 7 | **Percussion** | Fast-decaying noise-textured hit (no oscillators, pure noise burst) |
| 8 | **SubFunk** | Sub-octave sine/triangle sub bass with warm overdrive grit |
| 9 | **RubberSub** | Rubbery sub bass: sub-octave square grind under a resonant band-pass "honk", harder drive on transients |
| 10 | **WgPluck** | Classic Karplus-Strong plucked string; bright burst, short natural tail |
| 11 | **WgNylon** | Dark, felt-soft nylon string; heavily damped, gentle pick, long sympathetic tail |
| 12 | **WgBell** | Stiff dispersive waveguide; inharmonic bell/kalimba partials, hard pick, quick tail |
| 13 | **WgShimmer** | Wide-detuned (26 ¢) two-string course; slow chorusing sustain, very long pad-like tail |
| 14 | **Hypersaw** | Native seven-voice `rpdsp::Hypersaw` stack — one engine voice, wide detune range, no overdrive |
| 15 | **NoiseStorm** | Noise-texture engine: pitch-tracked Lorenz chaos growl through a prime-tap diffuser and regenerative allpass swarm, pinged by a resonant lowpass |

Presets 1–9 are oscillator-engine sounds; 10–13 are waveguide strings; 14 is the seven-voice
hypersaw stack; 15 is the noise-FX texture engine. Presets live in flash and are changed
per voice from the **preset browser** (long-press Play, or stop the transport to open
Settings on the OLED).
In the browser, touch **pads 8–22** — exactly the pads lit on the LED mirror — to apply
presets 1–15 to the selected voice; tap **pads 0–3** (or the V1–V4 buttons) to switch the
target voice without leaving the browser. The encoder button toggles the Settings screen
between the preset browser and the voice-parameter toggles.

---

## 5. Control reference

### Touch pads (MPR121 grid)

| Gesture | Result |
|---|---|
| Tap a pad | Toggle that step's gate on the pad's bank voice |
| Long-press a pad (~0.4 s) | Enter Step Edit mode for that step (encoder/faders/sensor edit it; OLED shows values) |
| Shift + pad | Clear that step (gate off, parameters to defaults) |
| Hold a parameter button + tap pad | Set that parameter track's length to the pad number |
| Hold (or Shift+latch) a parameter button + touch pads during playback | Record live values into the armed parameter of the current step (Note only on gate-on steps) |
| Pad press while Gate Length mode is held | Set the selected voice's Gate track length (2–16 steps) |
| Tap a pad while the preset browser is open | Apply that preset to the selected voice — pads 8–22 = presets 1–15; pads 0–3 switch the target voice |

### Faders

| Fader | Param mode | Utility mode |
|---|---|---|
| 1 | Filter cutoff (selected voice) | Tempo (45–200 BPM) |
| 2 | Attack time | Swing amount |
| 3 | Decay time | Delay feedback (0–0.91) |
| 4 | Velocity | Gate length across active steps |

With a step in Step Edit and the matching parameter button armed, moving a fader writes
the value into that step.

### Buttons — Param mode (mode switch LOW)

| Button | Action |
|---|---|
| Note / Velocity / Filter / Attack / Decay / Octave | Hold to arm real-time recording for that parameter (distance sensor / encoder / faders); auto-selects it as the encoder target |
| Shift + tap a parameter | **Latches** the hold (no finger needed). One latch at a time: pressing another parameter moves the latch; tapping the latched one clears it |
| Slide | Toggles slide/portamento mode (clears conflicting edit modes) |
| Shift | Modifier for latches and voice-button chords |

### Buttons — Utility mode (mode switch HIGH)

| Button | Action |
|---|---|
| 1 Play / Stop | Start/stop the transport (and all 4 sequencers). Stopping opens the OLED Settings/preset browser; starting closes it. Long-press toggles Settings without stopping |
| 2 Delay | Toggle the master delay; also sets the encoder target to Delay Time |
| 3 Scale | Cycle forward through the 13 scales |
| 4 Swing | Cycle through the 16 shuffle templates |
| 5 Theme | Cycle the 10 LED matrix color themes |
| 6 Encoder target | Short press: cycle encoder target. Hold: enter Gate Sequence Length mode (pads set the Gate track length) |
| 7 Randomize | Short press (< 1 s): randomize the selected voice. Long press (≥ 1 s): reset it |
| 8 Shift | Modifier for transport/utility chords |

### Voice buttons (both modes)

| Gesture | Result |
|---|---|
| Tap V1–V4 | Select voice 1–4 |
| Shift + V1 | Play / Stop |
| Shift + V2 | Randomize selected voice (short-press behavior) |
| Shift + V3 | Cycle scale |
| Shift + V4 | Toggle delay |

### Sensors & encoder

| Gesture | Result |
|---|---|
| Turn magnetic encoder | Adjust the active encoder target; slow = fine, fast = coarse (velocity-sensitive) |
| Utility button 6 | Change encoder target (Velocity → Filter → Attack → Decay → Note → Delay Time → Delay Feedback → Slide Time) |
| Hold Utility button 6 | Gate Sequence Length mode |
| Move hand over VL53L1X while a parameter is armed | Hands-free live recording of that parameter into the current step of the selected voice |
| Mode switch (GPIO 7) | Select Param (LOW) or Utility (HIGH) button set; shows a banner on flip |

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

### USB MIDI

The Pico 2 enumerates as a USB MIDI class device (Adafruit TinyUSB stack). All MIDI runs
on Core 0 (the control core), so it never disturbs the audio synthesis on Core 1.

- **MIDI out — notes**: voices **1 and 2 only** (internal indices 0 and 1) transmit
  monophonic note on/off on **channel 1**, gate-length accurate and synced to the
  sequencer. Voices 3 and 4 are internal-audio only — they never emit MIDI notes.
- **MIDI out — CC** (channel 1), with 10 ms rate limiting and change detection:

| Parameter | Voice 1 | Voice 2 |
|---|---|---|
| Octave offset | CC 71 | CC 75 |
| Decay time | CC 72 | CC 76 |
| Attack time | CC 73 | CC 77 |
| Filter cutoff | CC 74 | CC 78 |

- **MIDI clock out**: Pico2Seq acts as a **master clock**, sending realtime Clock (24
  PPQN), Start and Stop messages from the uClock transport.
- **MIDI in**: the USB MIDI read loop runs on Core 0. **[unverified: no user-facing MIDI-in
  feature (note/CC mapping into the sequencer) is documented; treat MIDI-in as
  infrastructure only.]**

### Clock & timing internals (for the curious)

uClock runs at 480 PPQN (120 ticks per 16th-note step); each 16th note advances all four
sequencers, ticks gate timers, and drives MIDI note lifecycles. Default tempo 90 BPM,
range 45–200 via Utility fader 1.

### Gate I/O

There is no hardware gate input or output in the current firmware (see §1.10). To gate
external gear, use voices 1–2 over USB MIDI into a MIDI-to-CV/gate converter. The panel's
"Gate Input" label is a leftover from the hardware design **[unverified]**.

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
  - uClock is **not** installed from the library manager — the firmware compiles a
    vendored uClock 2.2.1 fork from `src/vendor/uClock/` (alarm-pool patch; 2.3.0 removed
    `setOnSync24`, which this firmware uses)
  - MIDI Library 5.0.2
- Clone with `git clone --recurse-submodules` (`src/rpdsp/` and `src/VelocityEncoder/` are
  submodules).
- A verified command-line compile path (Windows PowerShell staging script) is documented
  in `README.md` ("Building with Arduino CLI on Windows"); it compiles but does not
  upload.
- **On-hardware behavior can only be verified on a real Pico 2** — CLI builds prove
  compilation only.

For developers working on this repo, the host test suite (Catch2 v3, no hardware needed):

```bash
cmake -B build_test -DCMAKE_BUILD_TYPE=Debug
cmake --build build_test --parallel
./build_test/tests/pico2seq_tests --reporter console     # or: ctest --test-dir build_test
./build_test/tests/pico2seq_tests "[sequencer]"          # run one tag/group
```

---

## 9. Troubleshooting & gotchas

**Behavioral gotchas (by design, verified in code):**

- **Voices 3 and 4 are audio-only.** No USB MIDI notes, no gate timers — only voices 1–2
  talk MIDI. Internal voice indices are 0-based (0–3); the OLED shows `Voice: 0`–`Voice: 3`
  and `V0`–`V3` on edit screens, while the voice buttons and this manual say V1–V4.
- **Encoder base offsets apply to all four voices** at step time (`applyEncoderBaseValues`
  runs per voice in `processSequencerStep()`, backed by the per-voice `encoderBaseValues[4]`
  array in `EncoderManager`).
- **Can't program a pitch into a step?** Note edits are rejected on gate-off steps. Toggle
  the step on first.
- **Pad does something unexpected** — check the context: a held parameter button turns pad
  presses into track-length setting; Gate Length mode turns them into Gate length; Shift
  turns them into clear-step. All pads are step pads; there is no pad "menu".
- **Stopping the transport opens the preset browser** on the OLED (a Play long-press
  toggles it without stopping). That is intentional; press Play to leave it.
- **Distance sensor dead?** It reads 74–1400 mm only; closer or farther returns an invalid
  reading (shown as -1 internally) and does nothing. Bright sunlight or the LED matrix at
  full brightness can cause optical jitter.
- **ToF recorded value stuck at one end** — the raw distance is normalized as
  `mm − 74`, so the nearest usable position (74 mm) maps to 0.
- **Delay feedback runaway** — feedback is capped at 0.91; if things howl, tap Utility
  button 2 (or Shift + V4) off and re-set fader 3.
- **Fader "jumps" after a mode flip** — on the first sample after a mode change the fader
  re-sends its position, so the parameter snaps to where the fader physically is. Move the
  fader through its travel to re-take the parameter.

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
- Some older sub-READMEs say "Mudras Sequencer"/"PicoMudrasSequencer" — that's pre-rename
  history; the product is **Pico2Seq**.

---

## 10. Glossary

| Term | Meaning |
|---|---|
| **Bank** | One of the two 16-pad halves of the touch grid; banks map to the selected voice pair |
| **Gate** | The on/off trigger state of a step; also the Gate parameter track whose length defines the voice's whole pattern length |
| **GateLength** | Fraction of a 16th-note step a note is held (0.1–100 %) |
| **Latch (Shift latch)** | Shift + parameter tap keeps the parameter "held" without finger contact; one latch at a time |
| **Pad-mirror** | The LED matrix displays exactly the touch grid's (band, step) positions |
| **Param / Utility mode** | The GP7 mode switch's two button function sets on the 8-button tile |
| **ParamId** | The nine automatable per-step parameters (Note, Velocity, Filter, Attack, Decay, Octave, GateLength, Gate, Slide) |
| **Polymetric / polymeter** | Parameter tracks of different lengths cycling against each other on the same voice |
| **PPQN** | Pulses per quarter note; the internal clock runs at 480 PPQN, MIDI clock out at 24 PPQN |
| **Preset** | One of 15 factory voice configurations (§4.2) |
| **Shuffle template** | One of 16 per-16th-note micro-timing groove tables |
| **Slide** | Per-step portamento flag: no envelope retrigger, pitch glides over the Slide Time |
| **Sound Buffet** | The OLED overview of all four voices' current presets, shown in the Settings screen |
| **Step Edit mode** | Long-press a pad to select that step; encoder/faders/sensor then edit its parameters |
| **Waveguide engine** | Karplus-Strong physical modeling of a plucked string (presets 10–13) |

---

*Pico2Seq — 4 voices, 32 pads, infinite polymeter. Manual generated from firmware sources
and `docs/`; see `README.md`, `docs/architecture.md` and the subsystem docs for the
engineering detail behind every feature described here.*
