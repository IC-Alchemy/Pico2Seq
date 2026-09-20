# Arpeggiator mode

Arpeggiator mode turns the whole performance surface into a chord arpeggiator:
the 32 touch pads become a 32-degree scale keyboard, the LED panel becomes the
chord map, the four faders set its range / length / swing / tone, the dial sets
the rate, the distance sensor sets note dynamics, and the twelve buttons switch
patterns, latch the chord and drive the transport. It is a *mode*, not a second
sequencer: while it is on, the four step sequencers do not advance, and the pads
neither toggle steps nor open the Step Edit pages.

Everything the sequencer keeps — 4 voices, 29 presets, the scale table, the
session snapshot, transport, LED themes — is shared with it. Each voice still
sounds through its own patch, and Chord pattern spreads a chord over the voices.

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

## The pads are a 32-degree ladder

Pad index = scale-table index. Pad 0 is the scale root (MIDI 48 through the
voices' own mapping), pad 31 is 31 scale steps above it, and the walk reads left
to right, top to bottom like text. One pad layout therefore plays a major scale
in Ionian, a whole-tone run in Wholetone and semitones in Chromatic — the arp
always plays the scale the Scale button selects.

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
| 6 | Latch | Toggle: hold the chord after release |
| 7 | Shift | Level, as everywhere else |

Patterns are a single-select group, not latchable holds: the arp stays in the
pattern you last pressed. Changing pattern keeps the walk's phase, so flipping
patterns mid-performance does not lose the beat.

### Utility panel (mode switch HIGH)

Play/Stop, Session and Theme keep their positions because they mean the same
thing in both modes; the step-only slots become arp functions.

| Bit | Control | Effect |
|---|---|---|
| 0 | Play / Stop | Same as the sequencer: tap runs/stopped+Settings, hold toggles the preset browser |
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

The faders are disarmed whenever the selected voice changes or the mode flips,
so a resting fader cannot snap an arp setting.

### Encoder (TMAG5273)

Turn = **rate**: 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, one detent per
division. Slow turns accumulate until a detent is reached, and reversing
direction answers immediately instead of unwinding motion from the other way.
The TMAG5273 board has no switch on this rig, so the mode uses the dial only;
re-sync lives on the Utility panel instead (Shift + bit 5).

### Distance sensor (VL53L1X)

**Dynamics.** Hand height inside the 55–700 mm window scales the velocity of
every note the arp starts, from a quarter of the patch's velocity with the hand
close to the sensor up to the full patch value with the hand raised. With no
hand in range nothing is attenuated, so a chord left alone sounds as the preset
was designed. The OLED draws the same value as a bar next to the raw millimetre
reading, and the LED panel brightens the sounding note with it.

In this mode the sensor never records into steps — there are no steps to write.

### OLED

| Line | Content |
|---|---|
| 1 | `ARP`, the arp voice (`V1`..`V4`), and the current rate |
| 2 | Pattern name, octave range, `LATCH` when engaged |
| 3 | The chord as note names, in the order the walk visits it |
| 4 | `S<n>` notes played since the last restart, then the note(s) sounding now |
| 5 | Lidar dynamics bar and the raw distance, or `no hand` |

The preset browser still outranks it (Play hold), so patches stay reachable
while the arp plays.

## The LED panel is the chord map

The 8x4 WS2812B panel mirrors the touch pads, so each LED is one scale degree:

| State | Colour |
|---|---|
| Finger on the pad | The arp voice's gate-on hue for the current theme |
| Latched, finger off | The same hue at gate-off brightness |
| Sounding now | Hue pushed toward the theme's playhead accent — harder for higher octaves of the range — scaled by the lidar dynamics |
| Free | Off, except a dim breathing marker on the scale's roots so the ladder stays navigable |
| No chord held | The whole panel breathes |

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
(Up, 1/16, 1 octave, 50% gate, no swing, filter 0.5). Session load/save in this
mode still saves and restores the *session* — voices, presets and patterns — it
just does not carry the arp.

## Checking it

`tests/unit/test_arpeggiator.cpp` drives the engine tick by tick: chord and latch
semantics, every pattern walk, exact note and gate ticks at 480 PPQN, swing
pairs, rate changes, encoder detents, dynamics, restart and both mode edges. The
pad-to-LED geometry is pinned against `ControlSurface::LedLayout`, so the two
surfaces cannot drift. `tests/unit/test_ui_transitions.cpp` pins what mode entry
clears, and `tests/unit/test_control_surface_logic.cpp` pins the fader set.

Host tests and a firmware compile prove the logic and that the sketch builds.
They cannot prove the sound: check a held chord against the transport, rate
changes while running, latch on/off, Chord pattern across four voices, the lidar
dynamics, and that leaving the mode returns the pads and the sequencer intact.
