# Tunings

A **scale** says which notes a pattern uses. A **tuning** says how far apart those notes are.
The same Note lane can sound in 12-tone equal temperament, in 5-limit just intonation, in 24
quarter-tones per octave, on the 22 shrutis of Hindustani music, in a historical temperament,
or in a scale that does not repeat at the octave. There is **one global tuning** for the whole
unit (all four voices), with a movable **tonic** (Sa) and a **reference pitch** (A4).

A fresh unit, and every song saved before this feature existed, plays **12-EDO, tonic C, A4 =
440 Hz**. That case keeps the old lookup table, so nothing that already sounded has moved.

## 1. The library (29 tunings, five families)

The tunings are listed in pad order, one per step pad, grouped by family. The ids are part
of the saved-song format: they never change and are never reused.

| Pad | Id | Tuning | Notes per period | Family (LED colour) |
|---|---|---|---|---|
| 1 | 0 | 12-EDO (Standard) | 12 | Equal (blue) |
| 2 | 1 | 24-EDO Quarter-Tone | 24 | Equal |
| 3 | 2 | 19-EDO | 19 | Equal |
| 4 | 3 | 31-EDO | 31 | Equal |
| 5 | 4 | 22-EDO | 22 | Equal |
| 6 | 5 | 17-EDO | 17 | Equal |
| 7 | 6 | 15-EDO | 15 | Equal |
| 8 | 7 | 10-EDO | 10 | Equal |
| 9 | 8 | 7-EDO | 7 | Equal |
| 10 | 9 | 5-EDO (Slendro) | 5 | Equal |
| 11 | 10 | 41-EDO | 41 | Equal |
| 12 | 11 | 53-EDO | 53 | Equal |
| 13 | 32 | 5-Limit JI (Ptolemy) | 12 | Just (amber) |
| 14 | 33 | 7-Limit JI | 12 | Just |
| 15 | 34 | Pythagorean | 12 | Just |
| 16 | 35 | Overtone 16-31 | 16 | Just |
| 17 | 36 | Undertone 32-17 | 16 | Just |
| 18 | 37 | Partch 43-Tone | 43 | Just |
| 19 | 64 | 1/4-Comma Meantone | 12 | Temperament (magenta) |
| 20 | 65 | Werckmeister III | 12 | Temperament |
| 21 | 66 | Kirnberger III | 12 | Temperament |
| 22 | 67 | Vallotti | 12 | Temperament |
| 23 | 96 | 22 Shruti (Danielou) | 22 | Indian (green) |
| 24 | 97 | 12 Svara JI | 12 | Indian |
| 25 | 98 | Pythagorean Svara | 12 | Indian |
| 26 | 128 | Bohlen-Pierce 13 | 13 per tritave | Xeno (red) |
| 27 | 129 | Carlos Alpha | steps of 78.0 cents | Xeno |
| 28 | 130 | Carlos Beta | steps of 63.8 cents | Xeno |
| 29 | 131 | Carlos Gamma | steps of 35.1 cents | Xeno |

Pads 30-32 are free for future tunings. Ids are `family * 32 + index`, so a new tuning joins
its family's free numbers without touching saved songs. The library can never hold more than
32, because every tuning owns one pad.

How a pitch is named on the OLED depends on the tuning: the 12-note tunings print the nearest
Western note with its offset in cents (`E3-14c`), 24-EDO adds a `+` for a quarter-tone above
the note, the Indian tunings print sargam (`Sa`, `Re`, `Ga`...) relative to the tonic, and the
rest print `octave:degree`.

The **Octave** lane always moves by true 1200-cent octaves, even in Bohlen-Pierce, where the
period of the scale is a tritave; the scale itself repeats at the tuning's own period.

## 2. Scales follow the tuning

A Dorian mode means nothing in 24 notes per octave, so **every tuning carries its own short
list of scales**, and choosing a tuning automatically switches to a scale that belongs to it:

1. the scale you are playing, if the new tuning offers it (A/B-swapping between two
   twelve-note tunings keeps your mode);
2. else the scale you last used in that tuning (this session only);
3. else the tuning's first scale.

The OLED confirms it: `Applied 24-EDO > Rast`. The sets:

| Tuning | Scales offered |
|---|---|
| The twelve-note tunings of the West (12-EDO, all JI, Pythagorean, the temperaments) | Major, Dorian, Phrygian, Lydian, Mixolydian, Minor, Locrian, Minor Pentatonic, Phrygian Dominant, Lydian Dominant, Harmonic Minor, Wholetone, Chromatic, Bhairav, Marwa, Poorvi, Todi (All Degrees is left out: in twelve notes it is Chromatic) |
| 24-EDO | Maqam Rast, Bayati, Hijaz, Saba, All Degrees |
| 19-EDO, 31-EDO, 22-EDO, 41-EDO, 53-EDO | Major, Minor and Pentatonic built from the tuning's own fifth, then All Degrees |
| 17-EDO | Major, Minor, All Degrees |
| 15-EDO | Heptatonic, Pentatonic, All Degrees |
| 10-EDO | Pentatonic, All Degrees |
| 7-EDO, 5-EDO, Undertone, Carlos Alpha/Beta/Gamma | All Degrees |
| Overtone 16-31 | Overtone Heptatonic, Overtone Pentatonic, All Degrees |
| Partch 43-Tone | Partch Major, Partch Minor, All Degrees |
| Bohlen-Pierce 13 | Bohlen-Pierce Lambda, All Degrees |
| 22 Shruti | The ten thaats (Bhairav, Marwa, Poorvi, Todi, Bilawal/Major, Kafi/Dorian, Bhairavi/Phrygian, Kalyan/Lydian, Khamaj/Mixolydian, Asavari/Minor), then All Degrees |
| 12 Svara JI, Pythagorean Svara | The four thaats first, then the rest of the twelve-note set |

**All Degrees** is the tuning itself: every note of the period in order, so the Note lane walks
all 24 quarter-tones, all 31 steps of 31-EDO, and so on. The tuned rows (Maqam Hijaz, 31-EDO
Major...) are written in the tuning's own degrees: a row climbs a period in the notes of that
scale and keeps going for up to six periods, then holds its top note.

**Utility button 3** and **Shift + V3** still step the scale, but only through the playing
tuning's list, so in 12-EDO the twelve-note list is the one that cycles.

## 3. The Tuning page

**Open it:** hold **Shift** (button 8) in Utility mode, then press **button 3**, then release
everything. Playback continues. **Shift** leaves. (Before this feature, Shift + Utility 3 cycled
the scale; that gesture now opens the page. Utility 3 alone and Shift + V3 still cycle.)

There is exactly **one page**, and it holds everything:

| Control | On the Tuning page |
|---|---|
| **Step pads** (32) | One pad per tuning, in library order. **Tap a pad** to choose that tuning (and its scale) |
| **Encoder** | Steps through the whole library, wrapping |
| **Buttons 1-6** | Choose scale slots 1-6 of the tuning that is playing |
| **Button 7** | A/B swap between the current and the previous tuning. Each keeps its own scale |
| **Voice buttons 1-4** | The four **hot favourites**. **Tap** recalls; **hold ~0.6 s** stores the playing tuning there; hold a slot that already holds the playing tuning to clear it |
| **Fader 1** | Tonic (Sa), C to B in semitones |
| **Fader 2** | A4 reference, 415-466 Hz in 0.5 Hz steps, with detents at 415, 432, 440 and 442 |
| **Fader 3** | Scale, spread over the playing tuning's own list |
| **Shift** | Exit |

The faders use the same movement pickup as the other pages: they act only once you move them
after the page opens. A fresh unit holds 12-EDO, 24-EDO, 5-Limit JI and 22 Shruti in the four
hot favourites.

### OLED

```
TUNING 13/29 JUST            <- position in the library, family
---------------------------
  Pythagorean
 [7-Limit JI        *2]      <- playing (inverted); *n = it is hot favourite n
  Overtone 16-31
Scale 3/17 Lydian            <- fader 3; ">" while it is the last fader touched
Tonic C   A4 440.0Hz         <- faders 1 and 2
Applied 7-Limit JI           <- confirmations for 1.5 s, else alternating:
                                tuning detail / "1-6 Scl 7 A/B 8 Exit"
```

The three list rows show the tuning before, the playing tuning (inverted bar) and the next. A
confirmation names what just happened: `Applied 24-EDO > Rast`, `Saved to Hot 2`, `Cleared Hot 2`,
`Hot 3 is empty`, `Only 4 scales here`. The status screen always shows the tuning and scale
names too.

### LEDs

All 32 pads show the library at once, each in the colour of its family: **blue** Equal,
**amber** Just, **magenta** Temperament, **green** Indian, **red** Xeno. The playing tuning
breathes, the previous one (the A/B partner) blinks, and pads that are a hot favourite are
pale. Pads past the last tuning stay dark. The faders have no LED bars on this page.

## 4. Saving

The tuning, tonic, A4, the A/B partner and the four hot favourites are part of the project
(persistence format 4, see [persistence.md](persistence.md)). The scale is saved with the
project as before, by index, and on load it is checked against the loaded tuning: a scale that
tuning does not offer is replaced by the tuning's first. Projects saved before this feature load
as 12-EDO, tonic C, A4 = 440.

The scale remembered per tuning (rule 2 above) is working memory: it starts empty at every boot.

## 5. For developers

| File | Role |
|---|---|
| `src/pico2seq-core/tuning/Tuning.h/.cpp` | Tuning data model, pitch maths, note names, the `Selection` and `Bank` and their rules |
| `src/pico2seq-core/tuning/TuningLibrary.cpp` | The 29 tunings as flash tables (cents, ratios, sargam slot maps) |
| `src/pico2seq-core/tuning/TuningScales.h/.cpp` | Which scales each tuning offers; automatic scale switching |
| `src/pico2seq-core/tuning/TuningState.h` | Device-only globals (selection, bank) and the read-only view other modules use |
| `src/pico2seq-core/scales/scales.*` | The 47 scale rows, names, short names, the native-scale mask |
| `src/ui/TuningPageControls.h` | Gesture policy (open chord, taps and holds), pure edge logic |
| `src/ui/TuningPageLogic.h` | What each gesture does: pad taps, favourites, A/B, faders, text for the OLED |
| `src/ui/AlchemyControlBridge.cpp`, `UIEventHandler.cpp`, `EncoderManager.cpp` | Routing of buttons, pads, faders and the encoder to the page |
| `src/OLED/oled.cpp`, `src/LEDMatrix/LEDMatrixFeedback.cpp` | The page's display |
| `src/voice/Voice.*`, `VoiceManager.cpp` | Turn a changed selection into a `PitchWorld` for the audio thread |

Design rules worth keeping:

- **One model.** A tuning is an ascending table of cents for one period plus an optional
  12-entry map from classic semitone slots to degrees (only 22 Shruti needs it, so komal Re
  lands on 16/15 rather than on whichever shruti is nearest). A scale row is either a
  row of 12-EDO semitone slots (classic rows 0-17, except All Degrees) or a row of the tuning's
  own degrees (a "native" row: All Degrees and the 29 tuned rows). `NATIVE_SCALE_MASK` says which.
- **Saved songs store the scale by index, so scales may only be appended** (and a tuning's scale
  list may only grow).
- **Threading.** Core 0 chooses the `Selection`; voices read an immutable `PitchWorld` on their
  own control pass. A momentary one-pass mismatch between a new tuning and a not-yet-updated
  scale is tolerated (documented in `AlchemyControlBridge.cpp`). No locks, no allocation.
- **Firmware builds with `-ffast-math`**, so no `std::isfinite` anywhere near tuning values;
  `TuningPageLogic.h` tests the float's bit pattern instead.
- **Adding a tuning:** add a table and an entry in `TuningLibrary.cpp` with the next id of its
  family, add it to `kEntries` in `TuningScales.cpp` if it is not twelve-note, add a pad
  colour if it is a new family. The tests check the library is sorted, ids are unique, every
  non-twelve-note tuning has a scale set, and the page fits 32 pads.
- **Adding a scale:** append a row to `scales.cpp` (classic or native), append a name and a
  short name (10 characters at most), raise `SCALES_COUNT`, extend the enum and the sets in
  `TuningScales.cpp`.

Tests: `tests/unit/test_tuning.cpp` (model, pitch maths, names, bank), `test_tuning_scales.cpp`
(scale sets, automatic switching), `test_tuning_page.cpp` (the gestures and every text the page
can show), plus the scale, voice and persistence suites.
