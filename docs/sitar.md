# Sitar Explorer — the `rpdsp::SitarStringVoice` example

Sitar Explorer is the worked example for [`src/rpdsp/src/rpdsp/sitar.h`](../src/rpdsp/src/rpdsp/sitar.h):
a performance mode that turns the whole Pico2Seq panel into a sitar and exposes
**every one of the model's thirteen setters, its pluck, its meend slide and its
ringing bank** — on the touch pads, the LED matrix, the faders, the knob, the
OLED, the distance sensor and the serial console. It is deliberately a *mode*
and not a preset: a preset can only reach the six lanes a voice layout carries,
while the point here is to hear all of `sitar.h` and decide which parts belong
in a permanent voice.

> **Status of the audio:** the DSP model is host-tested (output, decay, meend,
damping, drone independence, lane response — see *Verification* below), and the
firmware compiles into a flashable image. Nobody has listened to it on hardware
yet.

---

## 1. What the model is

`rpdsp::SitarStringVoice<Capacity>` is a plucked-string course with three
sitar-specific parts: a **jawari** bridge contact that buzzes hardest right after
a hard pluck and cleans up as the string decays, a **taraf** bank of five
sympathetic resonators tuned to the played note (Sa, Pa, Sa′, and their upper
octaves) that keep singing after the string itself dies, and a two-resonator
**body**. Two strings ring per course, detuned by a few cents.

Sitar Explorer runs **four courses** at once, because one course cannot do two
jobs — plucking a shared course would retune the note still ringing on it:

| Course | `SitarStringVoice` capacity | Pitch |
|---|---|---|
| Melody string | 2048 | the raga frets, Sa = C3 (130.8 Hz) up to ~C5 |
| Chikari Pa | 1024 | 196 Hz |
| Chikari Sa | 1024 | 262 Hz |
| Kharaj (bass) | 2048 | 65 Hz |

The four courses join the **master bus** through `VoiceManager`'s auxiliary
instrument hook, so the master fader, the master delay and the compressor treat
the sitar exactly like a voice.

---

## 2. Entering and leaving

| Gesture | Result |
|---|---|
| **Shift + Utility button 5** (the theme button), `Util` strap position | Enter/leave Sitar Explorer. It is an ordinary toggle: the same chord goes both ways. |
| **Shift + button 7** inside the mode | Leave as well (a deliberate two-finger exit, so randomize can never drop you out) |
| **`S` on the serial console** (115200 baud) | Toggle the mode without touching the panel — useful on the bench |

Entering drops any open settings page, step edit, slide mode and parameter
holds; the transport keeps its state, because the transport is what drives the
mode's drone. Leaving does **not** damp the strings: a sitar rings on after the
player lifts their hands. On entry the console prints the mode's legend plus
every lane's current value.

---

## 3. The panel, zone by zone

```
        col:   1    2    3    4    5    6    7    8
 row 1:  | Pa | Sa |jod |bass|jhala|tanp|shim|palm|   right hand (0-7)
 row 2:  |String|Jawari|Taraf|Body|Meend|rand|deflt|legend| explore (8-15)
 row 3:  |Sa  |Re  |Ga  |Ma  |Pa  |Dha |Ni  |Sa' |   fingerboard (16-23)
 row 4:  |Re' |Ga' |Ma' |Pa' |Dha'|Ni' |Sa''|Re''|   fingerboard (24-31)
```

### 3.1 Fingerboard — pads 16–31

The two lower rows are the neck: 16 frets of the current raga, Sa at the bottom
left corner and rising left-to-right, row by row. Each fret plucks the **melody
string** at that pitch.

| Gesture | What a sitar does | What the model does |
|---|---|---|
| Touch a fret | Mizrab stroke on a stopped string | `pluck(hz, force)` on the melody course |
| **Slide your finger across frets without lifting** | **Meend** — the ringing string bends to the new fret | `slideTo(hz)`: the loop delay slews over `Meend time`, no re-pluck, no delay reset |
| Lift your finger | The note keeps ringing at the fret you left it on | nothing is sent — the tail is the model's own T60 |

Sliding *within* a row walks the raga; dropping to the next row down is an
octave meend. Sliding is the fastest way to hear `Meend time` change: set it to
0.06 s and the bend snaps, to 0.35 s and it is a slow pull.

### 3.2 Right hand — pads 0–7

| Pad | Name | What it does |
|---|---|---|
| 1 | **Chikari Pa** | Strikes the Pa drone string (196 Hz) |
| 2 | **Chikari Sa** | Strikes the Sa′ drone string (262 Hz) |
| 3 | **Jod** | Both chikari strings in one stroke |
| 4 | **Kharaj** | The bass string, two octaves under Sa |
| 5 | **Jhala** | Cycles the clocked stroke pattern: off → 1/8 → 1/16 → jhala |
| 6 | **Tanpura** | Toggles the clocked drone pulse (one bass stroke per bar) |
| 7 | **Shimmer** | A whisper on the melody string (`amplitude 0.06`) — the taraf bank answers, the string is not retriggered |
| 8 | **Palm** | Damps every course (`reset()`) |

The jhala patterns are driven by the **transport's sixteenth notes**, so the
tempo fader (Shift + fader 1) is the drone's speed and Play/Stop is its switch.
The stroke pads flash on the LED matrix as the clock plays them, with no finger
involved.

### 3.3 Exploration row — pads 8–15

| Pad | Name | What it does |
|---|---|---|
| 9 | **String** | Jumps the lane cursor to `Ring (T60)` |
| 10 | **Jawari** | …to `Jawari buzz` |
| 11 | **Taraf** | …to `Taraf level` |
| 12 | **Body** | …to `Body level` |
| 13 | **Meend** | …to `Meend time` |
| 14 | **Randomize** | Reshuffles all thirteen lanes *inside their musical ranges* |
| 15 | **Defaults** | Restores the model's own defaults (what `sitar.h` initialises to) |
| 16 | **Legend** | Prints the whole lane table with current values to the console |

### 3.4 Buttons

| Button | Plain | With Shift |
|---|---|---|
| 1 | **Jod up** — pluck the bottom fret, then meend to the top one | — |
| 2 | **Jod down** — meend back down to the tonic | — |
| 3 | **Raga next** | Raga previous |
| 4 | **Jhala** cycle | **Tanpura** on/off |
| 5 | Lane cursor **forward** (all 13, wrapping) | — |
| 6 | Lane cursor **back** | **Defaults** |
| 7 | **Randomize** | **Leave the mode** |
| 7 held ~0.7 s | **Defaults** (the voice editor's hold-to-reset gesture) | — |
| 8 | Shift | — |

### 3.5 Faders

The four faders become the sitar's stage macros. The master volume keeps its
fader — a performance mode should never hide it — and every other lane has a
Shift alternate:

| Fader | Plain | With Shift |
|---|---|---|
| 1 | **Master volume** | **Tempo** (the jhala drone's speed, 45–200 BPM) |
| 2 | **Jawari buzz** | **Bridge contact** |
| 3 | **Taraf level** | **Taraf ring** |
| 4 | **The focused lane** (whatever the knob is on) | **Ring (T60)** |

The faders use the same deadband and engage rules as every other mode: a lane
only moves once you move a fader, so entering the mode never snaps a parameter
to wherever the sliders were parked.

### 3.6 Knob (TMAG5273)

The knob edits the **focused lane**, with the driver's velocity curve — a slow
turn is fine adjustment, a fast spin crosses the range. Buttons 5/6 and the
exploration pads move the focus. The focused lane is also what fader 4 edits, and
it is what the OLED names, so every lane is reachable and audible three ways.

### 3.7 Distance sensor

The hand over the sensor is the **pluck force**: near the panel is a soft brush
(`0.3`), high above it a hard mizrab stroke (`1.0`), and no hand at all plays a
firm default (`0.85`). Because `sitar.h` scales the jawari knee with pluck
amplitude, a hard stroke buzzes harder — the sensor is a tone control as much as
a dynamics control.

### 3.8 OLED

The mode's page outranks every other view while it is on:

```
SITAR Yaman (Kalyan)      <- raga and its thaat
Jawari 7/13               <- group and lane position
0.45                      <- the focused lane's value, in musical units
Jawari buzz               <- the focused lane's name
jhala 1/8 tanpura on      <- the clocked drone's state
Sa|=======---             <- finger's fret name + string-level meter
```

### 3.9 LED matrix (8x4)

- **Fingerboard (16–31)** — the raga as a colour map: one hue per degree, with
  the **vadi** and **samvadi** (the notes the raga rests on) held brighter, so
  the panel shows where home is even in silence. A struck fret flashes to a
  warm white; a *held* fret stays white; the whole fingerboard takes a warm wash
  from the taraf bloom (the audio thread's published envelope), so the lights
  breathe with the tail rather than with the pluck.
- **Right hand (0–7)** — ember flashes on the strokes, the same colour for
  strokes the clock plays with no finger involved; the jhala and tanpura pads
  stay lit while they are armed.
- **Exploration row (8–15)** — the pad of the group the cursor is in glows;
  randomize and defaults flash when they fire.

The sitar does not use the ten LED themes: it has its own palette (saffron
fretting, ember drone, ivory accents), so the theme button is free to be the
mode's entry chord.

### 3.10 Serial console

Everything the mode decides is announced at 115200 baud: raga changes (with vadi
and samvadi), jhala and tanpura state, lane changes, randomize/defaults, and on
entry the full legend:

```
[SITAR] rpdsp::SitarStringVoice - every lane of sitar.h, on the panel
  pads 16-31 fingerboard (raga frets; slide across them = meend)
  ...
   1  Ring (T60)       5.00 s       setDecayTimeSeconds     How long the string rings
   2  Brightness        0.92        setBrightness           Loop damping: dark to brilliant
  ...
```

---

## 4. The ragas

The fingerboard is not a Western scale. Each of the four ragas carries its
**aroh** (the ascending degrees above Sa), its **vadi** (home note), its
**samvadi** (answering note) and its **thaat** (parent scale). Sa is C3, and the
frets run two octaves up from it.

| Voice button | Raga | Thaat | Aroh (semitones above Sa) | Vadi / Samvadi |
|---|---|---|---|---|
| V1 | **Yaman** | Kalyan | 0 2 4 **6** 7 9 11 12 (teevra Ma) | Ga / Ni |
| V2 | **Bhairav** | Bhairav | 0 1 4 5 7 8 11 12 (komal Re, komal Dha) | Dha / Re |
| V3 | **Bhairavi** | Bhairavi | 0 1 3 5 7 8 10 12 (komal Re, Ga, Dha, Ni) | Ma / Sa |
| V4 | **Khamaj** | Khamaj | 0 2 4 5 7 9 10 12 (komal Ni, Re omitted in ascent) | Ni / Ga |

Ragas are per-mode data, not entries in `src/pico2seq-core/scales/`: the
sequencer's scale table is a step→semitone ladder for twelve-tone melodies, while
a raga carries aroh, vadi/samvadi and a tonic. Keeping them separate means
adding a raga cannot change what the sequencer plays.

---

## 5. The thirteen lanes of `sitar.h`

This is the exploration map — the same table the console prints and the encoder
walks. Ranges and defaults are the model's own; *musical range* is the slice the
randomize pad stays inside.

| # | Lane | `sitar.h` setter | Range | Default | Musical range | What you hear |
|---|---|---|---|---|---|---|
| 1 | Ring (T60) | `setDecayTimeSeconds` | 0.05–10 s | 5.0 s | 2–8 s | How long the string rings |
| 2 | Brightness | `setBrightness` | 0–1 | 0.92 | 0.62–0.99 | Loop damping: dark to brilliant |
| 3 | Mizrab point | `setPickPosition` | 0.02–0.5 | 0.12 | 0.05–0.24 | Where the pick meets the string (bridge→middle) |
| 4 | Mizrab hardness | `setPickHardness` | 0–1 | 0.9 | 0.45–1 | Soft felt → hard wire pick |
| 5 | Stiffness | `setStiffness` | 0–1 | 0.15 | 0–0.35 | Upper-partial dispersion |
| 6 | Coupling | `setDetuneCents` | 0–30 ct | 3 ct | 0–8 ct | Spread between the two strings of the course |
| 7 | Jawari buzz | `setJawari` | 0–1 | 0.45 | 0.15–0.9 | Bridge buzz the string can touch |
| 8 | Bridge contact | `setJawariThreshold` | 0–1 | 0.3 | 0.12–0.5 | How easily the string touches the bridge |
| 9 | Taraf level | `setTarafAmount` | 0–1 | 0.35 | 0.1–0.6 | Sympathetic strings in the mix |
| 10 | Taraf ring | `setTarafDecaySeconds` | 0.05–12 s | 4.0 s | 1.5–8 s | How long the taraf keeps singing |
| 11 | Body level | `setBodyAmount` | 0–1 | 0.25 | 0.08–0.5 | Acoustic body in the mix |
| 12 | Body tone | `setBodyFrequency` | 50–500 Hz | 130 Hz | 90–200 Hz | Low body resonance |
| 13 | Meend time | `setSlideTimeSeconds` | 0.005–2 s | 0.12 s | 0.06–0.35 s | Bend time of a slide |

Three lanes travel exponentially under the faders/knob (ring times, body tone)
and the other ten linearly, so the bottom of the travel is never a dead zone.

---

## 6. How it is built

| Piece | File | Role |
|---|---|---|
| Raga tables | `src/sitar/SitarRagas.h` | Aroh, vadi/samvadi, drone pitches, fret→pitch helpers (constexpr, host-tested) |
| Lane table | `src/sitar/SitarParameters.{h,cpp}` | Name, unit, group, range, default, musical range, travel mapping, value text |
| Gesture policy | `src/sitar/SitarControls.{h,cpp}` | `Sitar::Controls` — pads, buttons, knob, jhala/tanpura, flashes. Lives in `UIState` like the voice editor's controls |
| Audio course set | `src/sitar/SitarInstrument.{h,cpp}` | Four `SitarStringVoice` courses, the event queue, the lane atomics, the published levels |
| Panel glue | `src/sitar/SitarPerformance.{h,cpp}` | Hardware edges → `Controls` decisions → `Instrument` publications; LEDs, legend, status |
| Bus hook | `src/voice/VoiceManager.{h,cpp}` | `setAuxiliaryInstrument()` — an extra mono source summed before the master delay/gain/compressor |
| Mode state | `src/ui/UIState.h`, `src/ui/UITransitions.h` | `uiState.sitar` plus `enterSitar` / `exitSitar` (the only place mode state is mutated) |
| Entry chord | `src/ui/AlchemyControlBridge.cpp` | Shift + Utility 5, and the bridge hands the tiles/faders to the mode while it is on |
| Pads, clock | `src/ui/UIEventHandler.cpp`, `src/app/ClockService.cpp` | Pad events and the sixteenth-note jhala hook |
| LEDs, OLED, knob, sensor | `src/app/ControlIO.cpp`, `src/OLED/oled.cpp` | `renderLeds`, `displaySitarPage`, focused-lane nudging, pluck force |

Core discipline is unchanged by the mode: Core 0 owns all of it (pads, tiles,
knob, sensor, lights, screen, console), Core 1 only ever calls
`Instrument::renderAdd()` — which drains a bounded number of queued gestures,
applies the lanes that moved, sums four courses and publishes a decaying level.
Pressing a fret costs one queue push; the audio thread never touches `UIState`
and Core 0 never touches DSP state. The courses are static storage (50,880 bytes),
not stack — Core 1's stack is 2 KiB.

### Build requirement

Sitar Explorer needs the `rpdsp` revision that contains `sitar.h`. In this
branch that is the **`sitar-voice` branch of RPDSP (`b554c8d`, "feat: add
SitarStringVoice physical model")**; pin it in the parent repository with

```bash
git -C src/rpdsp fetch origin sitar-voice
git -C src/rpdsp checkout b554c8d
git add src/rpdsp          # record the pointer, then commit
```

`scripts/build_pico2seq.ps1` refuses to build while a submodule's checked-out
commit differs from the recorded pointer (it reports `+` in
`git submodule status`), which is exactly the "sitar.h is not in the project"
state — record the pointer and the script is happy again. A fresh clone at the
older pointer has no `sitar.h` and will not compile this branch.

---

## 7. Verification

What has been checked, and at what level:

- **Host tests** — `tests/unit/test_sitar.cpp` (15 cases, 848 assertions) runs
  under `pico2seq_tests` and `pico2seq_voice_tests`: raga tables, lane table
  (including the proof that the table's defaults reproduce `sitar.h`'s own
  defaults), gesture policy (meend vs. pluck, strokes, group jumps, randomize
  inside musical ranges, hold-to-defaults, jhala/tanpura counts per bar), the
  audio courses (a pluck sounds and decays, a slide does not re-excite the
  string, drone courses ring independently, damping silences, lane changes
  change the tone, a full queue drops instead of stalling), and the master-bus
  hook (identical sitar on two buses, one at a quarter master volume, is clearly
  quieter).
- **Firmware build** — `arduino-cli compile` at **225 MHz**
  (`flash=4194304_65536,opt=Optimize3,usbstack=tinyusb`, `-ffast-math`,
  `PICO2SEQ_AUDIO_IN_RAM=1`): exit 0, 399,880 bytes of flash (9%) and 176,472
  bytes of globals (33%, of which 50,880 are the four courses). Artifacts:
  `Pico2Seq.ino.uf2`, `.elf`, `.bin`, `.map`.
- **Not verified** — no board has been flashed and nothing has been listened to.
  Panel feel (finger slides on the MPR121 pads, the sensor as pluck force, the
  LED palette on real WS2812s, the OLED page's spacing) still needs a rig.

---

## 8. Where a permanent sitar voice would go next

If the sitar should stop being a mode and become one of the four voices, the
precedent is the waveguide engine: `ENGINE_SITAR` in `VoiceConfig.h`,
`SitarStringVoice<2048>` as a `Voice` member, a parameter layout in
`VoiceParameters.h` (with `velocityToAmplitude = false`, so velocity drives the
excitation rather than the tail), presets in `src/voice/presets/StringPresets.h`,
scalars through `PatchCodec`/`ProjectSnapshot`, and the silent-span exclusion in
`Voice::canSkipSilentSpan_()` (the taraf tail outlives the strings). The mode's
lane table and raga tables carry over as-is; the gesture policy would shrink to
the lanes a layout can hold.
