# Arpeggiator mode

Arpeggiator mode turns the whole performance surface into a chord arpeggiator:
the 32 touch pads become a scale-degree keyboard, the LED panel becomes the
chord map, the four faders set its range / gate / swing / tone, the dial sets
the rate, the distance sensor sets note dynamics, and the twelve buttons switch
patterns, latch the chord and drive the transport. Hold Shift to reveal rhythm
presets and four rhythm faders; Shift + dial changes tempo. It is a *mode*, not a second
sequencer: while it is on, the four step sequencers do not advance, and the pads
neither toggle steps nor open the Step Edit pages.

Everything the sequencer keeps — 4 voices, 29 presets, the scale table, the
session snapshot, transport, LED themes — is shared with it. Each voice still
sounds through its own patch, and Chord pattern spreads a chord over the voices.

## First minute

1. **Shift + hold Voice 4** enters ARP. Release Shift.
2. Touch three pads, for example the first, third and fifth in Major. Press
   **Latch** before lifting your fingers if you want the chord to keep going.
3. Tap **Play** (or Shift + Voice 1). Turn the dial for rate; try the six
   pattern buttons. Voice buttons select the patch you play through.
4. Hold **Shift**, then press the third pattern button for **Tresillo**.
   The strip shows three hits spaced across eight steps. Release Shift.
5. Hold Shift and move fader 3 to slide those hits around the beat. Move
   fader 4 to make the first hit stand out. Shift + dial adjusts tempo.

Touch a new chord after lifting all fingers to replace a latched chord.
**Shift + Latch restarts** the walk in either panel position. A short Play tap
stops without hiding the arp screen; **hold Play** opens/closes the sound browser.

## Entering and leaving

**Shift + hold Voice 4** (the fourth SliderModule button) for 400 ms toggles the
mode, in either direction. A **tap** on that same combination still opens the
Voice Editing mode, so the press defers its action until release or hold — the
same rule the Play button uses for its tap vs. hold. The OLED shows `ARP ON` /
`ARP OFF` with a subtitle for 800 ms, and the serial log prints `[ARP] mode on`
plus the current settings.

Both edges reset the performance state: chord, latch, octave walk and step
counter are dropped (settings are kept), the sequencers' note tracking is ended
and all four voices are gated off, so nothing bleeds across the switch.
Settings survive on purpose — they describe how the player wants the arp to
behave, not one performance.

## The pads are scale-degree positions

Pad index is a physical position on the 8x4 panel. For a seven-note scale, each
row is one octave: columns 0–6 are the seven scale degrees and column 7 is the
next octave's root. The first pad of the next row repeats the last pad of the
preceding row, so the panel shows four octave rows with intentional root
overlaps. For example, in Major, pad 0 is C, pads 1–6 are D–B, pad 7 is the
following C, and pad 8 is that same C. Other scale families retain the linear
32-position ladder until a layout rule is defined for them.

Touching a pad adds its degree to the chord; releasing it drops it again. There
is no step selection, no long-press-to-edit and no Shift+pad clearing in this
mode: multi-touch chord entry is the whole pad vocabulary.

**Latch** (Param panel bit 6, or Utility panel bit 5) keeps the notes of a chord
after the fingers leave. With Latch on and nothing held, the *next* press starts
a new chord: the classic latch gesture, so a latched chord is replaced just by
playing the next one. Turning Latch off lets the latched notes go and keeps
whatever is still under a finger.

## Controls

### Touch pads (32)

| Action | Effect |
|---|---|
| Touch a pad | Add that scale degree to the chord |
| Release | Drop it, unless Latch is holding it |
| Several pads | Chord; Chord pattern voices up to four of them at once |

### Param panel (mode switch LOW) — patterns

| Bit | Control | Effect |
|---|---|---|
| 0 | Up | Low to high, climbing the octave range after the whole chord |
| 1 | Down | High to low |
| 2 | Up-Dn | Up then down without repeating the turning points |
| 3 | Rnd | Uniform random over the chord-and-octave list |
| 4 | Order | As played (press order), also through the octave range |
| 5 | Chord | Up to four chord notes at once, one per voice; the range steps up once per chord |
| 6 | Latch | Toggle: hold the chord after release; Shift + tap restarts |
| 7 | Shift | Level, as everywhere else |

Patterns are a single-select group, not latchable holds: the arp stays in the
pattern you last pressed. Changing pattern keeps the walk's phase, so flipping
patterns mid-performance does not lose the beat.

### Shift + pattern buttons: rhythm starting points

The OLED numbers these six buttons **1 through 6**, from left to right in the
same order as Up, Down, Up-Dn, Rnd, Order, Chord. These choices change rhythm
without changing the note pattern, chord, rate, swing or accent.

| Button | Rhythm | Hits / steps | Try it with |
|---|---|---|---|
| 1 | All | 8 / 8 | The original continuous arpeggio |
| 2 | Pulse | 4 / 8 | Chord for repeated stabs |
| 3 | Tresillo | 3 / 8 | Up-Dn for a bouncing phrase |
| 4 | Five | 5 / 8 | Order for a broken melody |
| 5 | Orbit | 5 / 12 | Three held notes, then change the octave range |
| 6 | Seven | 7 / 16 | Random for sparse, shifting lines |

Hits are spaced as evenly as possible. Bright cells are hits; dots are rests.
The underline follows **every step**, including rests. The note walk keeps
moving through rests, so changing density can reveal different notes in the
same chord. Rhythm length is independent of chord size and octave range.
Edits keep the pending interval and gate intact. Restart explicitly when you
want the next clock pulse to begin again at step 1.

### Utility panel (mode switch HIGH)

Play/Stop, Session and Theme keep their positions because they mean the same
thing in both modes; the step-only slots become arp functions.

| Bit | Control | Effect |
|---|---|---|
| 0 | Play / Stop | Tap starts/stops while keeping the arp page; hold toggles the preset browser |
| 1 | Session | Tap saves, hold loads the flash session |
| 2 | Scale | Cycle the 13 scales the arp plays |
| 3 | Octaves | Cycle the octave range 1 -> 2 -> 3 -> 4 -> 1 |
| 4 | Theme | Cycle the 10 LED themes |
| 5 | Latch | Same toggle as the Param panel, so latching needs no strap change; Shift + tap re-syncs the walk to the chord root |
| 6 | Chord | Tap draws a random four-note chord (and engages Latch so it keeps playing); Shift + tap clears the chord |
| 7 | Shift | Level |

### Voice buttons (SliderModule, both modes)

A tap selects the voice the arp plays through — the same direct voice select as
the sequencer uses, so the arp inherits whatever patch that voice holds. Shift
+ tap gives the transport chords (Play/Stop, Randomize voice, Scale), and Shift
+ hold Voice 4 toggles the mode.

### Faders

| Fader | Control | Range |
|---|---|---|
| 1 | Octaves | Arp range 1..4 octaves, quantized from the fader position |
| 2 | Gate | Note length 5%..95% of the interval |
| 3 | Swing | 0..half an interval on every second gap |
| 4 | Filter | Per-note filter lane, composed with each voice's patch |

Hold **Shift** to use the second fader layer (in either panel position):

| Fader | Control | What it does |
|---|---|---|
| 1 | Hits | 0..Length hits, spaced evenly; zero intentionally makes silence |
| 2 | Length | 1..16 steps; reducing length also caps Hits and wraps Rotate |
| 3 | Rotate | Moves the rhythm right by 0..Length-1 steps |
| 4 | Accent | First rotated hit stays full; other hits soften from 1.00x to 0.25x |

Accent multiplies hand dynamics and patch velocity; it does not boost above the
patch's level. At zero accent all hits have equal weight. Moving a fader shows
its name and value for 1.4 seconds without hiding the rhythm strip or preset.
Gate displays milliseconds (a short-to-long range when swung), Swing shows the
long:short timing ratio, and Tone uses the patch's actual mapped unit.

Faders re-arm on voice/mode changes, both Shift edges, rhythm preset selection,
and octave-button changes. Move from the resting position to engage; no pickup
against a saved value is required.

### Encoder (TMAG5273)

Turn = **rate**: 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, one detent per
division. Slow turns accumulate until a detent is reached, and reversing
direction answers immediately instead of unwinding motion from the other way.
The TMAG5273 board has no switch on this rig, so the mode uses the dial only;
re-sync lives on Shift + Latch in either panel position.

**Shift + turn = tempo**, 45..200 BPM, in one-BPM detents. Motion left over from
Rate never carries into Tempo or back. The voice editor keeps ownership of the
dial when open.

### Distance sensor (VL53L1X)

**Dynamics.** Hand height inside the 55–700 mm window scales the velocity of
every note the arp starts, from a quarter of the patch's velocity with the hand
close to the sensor up to the full patch value with the hand raised. With no
hand in range nothing is attenuated, so a chord left alone sounds as the preset
was designed. The LED panel brightens each sounding note using its captured dynamics and
accent together. Moving the hand changes subsequent notes.

In this mode the sensor never records into steps — there are no steps to write.

### OLED

The 128x64 play page uses eight fixed rows, with no scrolling or auto paging:

| Row | Content |
|---|---|
| 1 | ARP, running/stopped, selected voice, rate, HOLD or LIVE |
| 2 | Persistent preset name in a bright band |
| 3 | Note pattern, octave range, scale |
| 4 | KEYS: scale-key names, or a touch-pads prompt; `+N` counts omitted keys |
| 5 | Hits/length, rhythm name (Custom after edits), tempo |
| 6 | Hit/rest strip and current-step underline |
| 7 | Gate duration in ms and swing ratio |
| 8 | Last composed pitch of the primary arp voice and Shift hint; Play help when stopped |

KEYS names describe the pad ladder. Last pitch is captured when the primary
arp voice receives a note, including its patch harmonies/detuning; Chord mode
also plays the other voices. `~` marks clipped long text. The stopped page has
no moving playhead. The preset browser still takes priority when opened.

Holding Shift reveals the six rhythm choices (Param panel), the Shift-fader
assignments, and the tempo-dial hint. The Utility side shows Restart/Clear
instead. On-screen button numbers are one-based; the hardware tables above
use zero-based bits. Control feedback replaces only the middle three rows.

## The LED panel is the chord map

The 8x4 WS2812B panel mirrors the touch pads, so each LED is one physical scale position. For seven-note scales the first/last column of each row and the row-boundary pads are the same octave-root relationship described above.

| State | Colour |
|---|---|
| Finger on the pad | The arp voice's gate-on hue for the current theme |
| Latched, finger off | The same hue at gate-off brightness |
| Sounding now | Hue pushed toward the theme's playhead accent — harder for higher octaves of the range — scaled by captured lidar dynamics and accent |
| Free | A quiet one-eighth shade of the selected voice hue; scale roots use a distinct half-strength shade |
| No chord held | The same root/free shades remain visible, so the scale ladder is ready for chord entry |

## How it is built

| Piece | File | Owns |
|---|---|---|
| Note engine | `src/pico2seq-core/arpeggiator/Arpeggiator.{h,cpp}` | Chord, latch, pattern walk, rate/gate/swing counters, dynamics. Portable C++, no Arduino, no allocation |
| Audio glue | `src/app/ArpPlayback.{h,cpp}` | Slot-to-voice mapping, `VoiceState` publishing, mode toggle |
| Mode state | `UIState::arp` (in `src/ui/UIState.h`) | The engine lives here because every surface already receives `UIState` |
| Clock hooks | `src/app/ClockService.cpp` | Step drain (skipped in this mode) and the PPQN tick that drives `arpTick()` |
| Pads | `src/ui/UIEventHandler.cpp` | Chord entry; releases always reach the chord so no note can strand |
| Buttons, faders, mode gesture | `src/ui/AlchemyControlBridge.{h,cpp}` | Both panels, the Shift+hold Voice 4 toggle |
| Fader assignment | `src/ui/ControlSurfaceLogic.{h,cpp}` | `FaderMap::arpAssignmentFor` |
| Encoder | `src/sensors/EncoderManager.cpp` | Rate turning |
| Lidar | `src/app/ControlIO.cpp` | Dynamics instead of step recording |
| LEDs | `src/LEDMatrix/LEDMatrixFeedback.cpp` | `renderArpPanel` |
| OLED | `src/OLED/oled.{h,cpp}` | `displayArpPage` |

### Timing

The arp is driven by uClock's 480 PPQN output-pulse ticks, the same counter
`processPendingGateTicks()` already drains for sequencer note-offs. One tick per
pulse, the engine decides when the next note is due, so every rate lines up with
the transport grid instead of drifting against it. Swing delays every second gap
and shortens its partner by the same amount, so a pair still lasts two intervals
and no drift accumulates.

The engine reports note starts and stops by *slot* (0..3) with a tick mask; it
never touches a voice. `ArpPlayback` maps slot 0 to the selected voice and slots
1..3 to the remaining voices in index order, so Chord pattern sounds on up to
four different patches, and it remembers which voice each slot went to until its
gate closes (the selected voice can move while a note is sounding).

Notes start from `MusicalValues::baseStep(patch)`: engine, envelope and levels
come from the voice's own patch, and the arp overrides only pitch, its octave of
range, the dynamics velocity and the filter fader. Envelope retrigger uses the
same `VoiceState::shouldRetrigger` event the step sequencer publishes, so no DSP
path is special-cased for the arp.

### Rules that keep it honest

- **A release always reaches the chord.** Settings can open while a finger is
  down (Play hold), and a swallowed release would leave that note in the chord
  forever. Modal states that swallow edges outright (the voice editor, and the
  wait-for-release window after it) call `Engine::releaseAllHeldPads()`, which
  drops held pads and keeps latched ones.
- **Entering the mode ends sequencer note tracking; leaving it restarts the
  sequencers** if the clock is still running, so a mode flip during playback
  cannot leave them silent.
- **A gate always stops before the next note on the same tick**, and the gate can
  never reach the interval, so a mono pattern retriggers from silence instead of
  gliding into the next note.
- **An empty chord leaves rests**, not a rhythm that stops and restarts out of
  time: the interval is scheduled whether or not a note was emitted.

## Not persisted

The mode and its settings are runtime state: the session snapshot is untouched,
so a power cycle boots into the step sequencer with the arp at its defaults
(Up, 1/16, 1 octave, 50% gate, no swing, filter 0.5, rhythm All 8/8, rotation 0, accent off). Session load/save in this
mode still saves and restores the *session* — voices, presets and patterns — it
just does not carry the arp.

## Checking it

`tests/unit/test_arpeggiator.cpp` drives the engine tick by tick: chord and latch
semantics, every pattern walk, exact note and gate ticks at 480 PPQN, swing
pairs, rate changes, encoder detents, dynamics, restart and both mode edges.
Rhythm tests cover every hit count and rotation through 16 steps, silent grids,
phase-preserving edits, swung Chord gates, captured accents, and bounded OLED
chord summaries. Gate readouts use the same quantized interval helpers as playback. The
pad-to-LED geometry is pinned against `ControlSurface::LedLayout`, so the two
surfaces cannot drift. `tests/unit/test_ui_transitions.cpp` pins what mode entry
clears, and `tests/unit/test_control_surface_logic.cpp` pins the fader set.

Host tests and a firmware compile prove the logic and that the sketch builds.
They cannot prove the sound: check a held chord against the transport, rate
changes while running, latch on/off, Chord pattern across four voices, the lidar
dynamics, and that leaving the mode returns the pads and the sequencer intact.
