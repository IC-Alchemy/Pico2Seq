# Tuning system for Pico2Seq: design, plan and as-built notes

Status: implemented in the working tree (nothing committed). The portable core, the page logic, the scale sets, persistence format 4, the voice/OLED/LED/button wiring and the docs are written. On the host the new suites pass (105 new test cases: `test_tuning` 31, `test_tuning_scales` 19, `test_tuning_page` 55, plus updated scale, voice and persistence suites; UI suite 205 cases, all green) and the full run shows exactly the 26 failures it showed before any edit. The firmware-only files (bridge, UI events, encoder, buttons, OLED, LEDs, session, arp) pass a host `-fsyntax-only` check against the real Adafruit/uClock/OneButton headers with the hardware stubbed, but have not been built with `arduino-cli`/`-ffast-math`.

## 1. What this adds

Pico2Seq used to play one pitch world: twelve equal semitones, A440, with scales choosing which semitones a pattern may use. This adds an independent layer called a **tuning**, which decides what pitch each note sounds. A scale answers "which notes does this pattern use"; a tuning answers "how far apart are the notes". The same Note lane can be played in 12-tone equal temperament, 24 quarter-tones per octave, 5-limit just intonation, 22 shrutis, a historical well-temperament, a non-octave Bohlen-Pierce scale and more, with a movable tonic (Sa) and a reference pitch (A4, 415 to 466 Hz).

Decisions: the Tuning page opens with **Shift + Utility 3** (Utility 3 alone and Shift + Voice 3 still step scales); the scale list grows by **All Degrees**, four Hindustani thaats and 29 tuned scales; there is **one global tuning**, so voices can never drift apart. After the first implementation pass, two more decisions: the whole library lives on **one OLED page and one pad layout**, each family in its own LED colour, and **choosing a tuning switches to a scale that belongs to it**, because a Dorian mode means nothing in 24 notes per octave.

## 2. Design

### 2.1 Tuning versus scale

A tuning is an ascending table of cents for one period (normally an octave), so it works for any number of notes per period, and for periods that are not an octave. A scale row is one of two kinds. **Classic rows** (0-17, except All Degrees) hold 12-EDO semitone slots, exactly as before. **Native rows** (All Degrees, row 13, and the 29 tuned rows 18-46) hold the tuning's own degrees. `NATIVE_SCALE_MASK` says which is which and is handed to each voice as a bit mask, so voices never read scale globals.

For twelve-note octave tunings (12-EDO, the just and historical tunings, the twelve-svara tunings) a semitone slot is the degree, so every existing pattern, preset and song keeps its melody and simply takes on the new tuning's intervals. The 22 shrutis carry a 12-entry `chroma` map that puts each classic semitone on a chosen shruti (komal Re on 16/15, not on whichever shruti is nearest). The Octave lane always moves by true 1200-cent octaves, even in Bohlen-Pierce.

### 2.2 Pitch world

`Selection { tuningId, tonic 0..11, a4Hz }` is the one global setting. Core 0 turns it into a `PitchWorld { tuning*, rootHz, tonic, standard }` that each voice copies on its own control pass, so Core 1 only reads immutable flash tables and nothing needs a lock. Frequency is `rootHz * 2^(cents/1200)`, clamped to the range the old 128-entry MIDI table enforced. When the world is exactly the historical one (12-EDO, tonic C, A4 440) the voice keeps its old MIDI-table lookup, so **every existing song sounds bit-for-bit as before**. Tuning ids are permanent (`id = family * 32 + index`).

### 2.3 The library (29 tunings, 5 families)

The library is listed in id order, one tuning per step pad, so pad k is tuning k. It can never exceed 32 tunings.

| Family (LED colour) | Tunings |
|---|---|
| Equal (blue) | 12-EDO (Standard), 24-EDO Quarter-Tone, 19, 31, 22, 17, 15, 10, 7, 5 (Slendro), 41, 53-EDO |
| Just (amber) | 5-Limit (Ptolemy), 7-Limit, Pythagorean, Overtone 16-31, Undertone 32-17, Partch 43-Tone |
| Temperament (magenta) | 1/4-Comma Meantone, Werckmeister III, Kirnberger III, Vallotti |
| Indian (green) | 22 Shruti (Danielou), 12 Svara JI, Pythagorean Svara |
| Xeno (red) | Bohlen-Pierce 13, Carlos Alpha, Beta, Gamma |

The temperaments are generated at compile time from chains of fifths and the tests check them against published values; just tunings are written as exact ratios and checked against `log2`.

### 2.4 Scales per tuning

Each tuning offers a short list of scales (`tuning/TuningScales.cpp`). The twelve-note tunings keep the 17 classic modes and thaats (All Degrees is left out there: in twelve notes it is Chromatic). 24-EDO offers Maqam Rast, Bayati, Hijaz and Saba; 19, 31, 22, 41 and 53-EDO each offer a Major, Minor and Pentatonic built from their own steps; 17-EDO a Major and Minor; 15-EDO a Heptatonic and Pentatonic; 10-EDO a Pentatonic; Overtone, Partch and Bohlen-Pierce have scales drawn from their own notes; 22 Shruti offers the ten thaats; the Svara tunings offer the thaats first; 7-EDO, 5-EDO, Undertone and the Carlos scales play All Degrees. Every set either includes All Degrees or (for the twelve-note tunings) Chromatic, so every note of a tuning stays reachable.

When the tuning changes, the scale becomes: the scale you have, if the new tuning offers it; else the one you last used there (working memory, not saved); else the tuning's first. Saved songs store the scale by index, so scales may only be appended, and a loaded scale the loaded tuning does not offer is replaced by the tuning's first.

### 2.5 Names on the OLED

Every pitch the firmware prints goes through one function that follows the tuning's naming scheme: **Western** (nearest 12-EDO note plus cents, `E3-14c`), **Quarter** (a `+` for a quarter-tone above, `C+3`), **Sargam** (relative to the movable Sa, `Sa`, `re`, `Ga`), **Numeric** (`octave:degree`, `3:07`). The status screen alternates the scale name and the tuning (with tonic and a non-standard reference) while a non-standard tuning plays.

## 3. Controls: how the whole surface is used

The page follows the Reverb page's pattern: one deliberate chord to open it, the whole gesture including releases consumed so nothing leaks into normal actions, and Shift to leave. The page does not stop the transport.

**Open:** hold Shift in Utility mode, press button 3. There is exactly one page:

| Control | Does |
|---|---|
| Step pads (32) | The whole library, one pad per tuning in family colour. Tap chooses the tuning (and its scale) |
| Encoder | Steps through the library in order, wrapping |
| Buttons 1-6 | Choose scale 1-6 of the playing tuning |
| Button 7 | A/B swap with the previous tuning; each tuning keeps its own scale |
| Voice buttons 1-4 | Hot favourites: tap recalls, hold (0.6 s) stores the playing tuning, hold again to clear |
| Fader 1 | Tonic (Sa), C to B |
| Fader 2 | A4, 415-466 Hz in half-hertz steps with detents at 415, 432, 440, 442 |
| Fader 3 | Scale, spread over the playing tuning's own list |
| Fader 4 | Unassigned |
| Shift | Exit |

A fresh unit holds 12-EDO, 24-EDO, 5-Limit JI and 22 Shruti in the four favourites. Favourites and the A/B partner are saved with the song.

**OLED:** header with position and family (`TUNING 13/29 JUST`), the tuning before, the playing tuning on a highlight bar (with `*n` when it is hot favourite n) and the tuning after, the scale line, the tonic and A4, and a bottom line that confirms the last gesture for 1.5 s (`24-EDO > Maqam Rast`, `Saved to Hot 2`) and otherwise alternates the tuning's facts with a key reminder. **LEDs:** all tunings at once, in their family hue; the playing tuning breathes, the A/B partner blinks, hot favourites are pale, and there are no fader bars.

**Elsewhere:** Utility 3 and Shift + V3 step only through the playing tuning's scales.

## 4. Engine integration

Voices hold a `PitchWorld` and a native-scale mask, both copied from the control update on the voice's own pass; the pitch cache is invalidated through the same mechanism the scale already uses, so retuning takes effect on the next note without a click or an allocation. Core 0 writes the tuning and the scale separately; a one-pass mismatch on the voice side is tolerated and documented in the bridge. Nothing in this touches the sequencer; the arpeggiator counts notes per period from the scale row and the tuning's period.

## 5. Persistence

Project format 4 appends a 12-byte `TuningSnapshot` (tuning id, tonic, A4 in tenths of a hertz, A/B partner, four favourites) to format 3. Payload 12,460 B, frame 12,472 B; `RETAINED_VERSION` 4. Older files load through `upgradeFromV3()` into 12-EDO, C, 440, default favourites. `validateTuning()` refuses rather than clamps bad ids, tonic or A4. On load the stored scale is coerced against the loaded tuning.

## 6. Implementation order (as done)

1. Portable core: `src/pico2seq-core/tuning/Tuning.*`, `TuningLibrary.cpp`, `TuningScales.*`; tests.
2. Scales: 47 rows, names, short names, native mask in `scales.*`.
3. Page logic: `src/ui/TuningPageControls.h` (chord, tap/hold tracking) and `TuningPageLogic.h` (pad mapping, favourites, A/B, faders, every OLED string); tests.
4. Voices: `PitchWorld` in `Voice`/`VoiceManager`, the standard-world legacy path, tuning-aware note names in `MusicalValues`.
5. Persistence: format 4, capture/apply in `Session`/`AppState`.
6. UI wiring: `UIState`, `UITransitions`, `AlchemyControlBridge`, `UIEventHandler`, `EncoderManager`, `ButtonHandlers`.
7. Screens: `displayTuningPage`, status and value lines in `oled.cpp`, family-hue pad layout in `LEDMatrixFeedback.cpp`.
8. Docs: `docs/tuning.md` (new), manual §3.3/§3.8, scales, persistence, firmware-structure and oled updated.
9. Verification: baseline run before any edit, full run after (same 26 failures), host syntax check of the firmware-only files. Nothing committed.

## 7. Things to know

Shift + Utility 3 used to cycle the scale and now opens the Tuning page; Utility 3 alone and Shift + V3 still cycle, now within the playing tuning's scales (in 12-EDO that is the 17 twelve-note scales, without All Degrees).

Fixed on request: the Ionian row's step 38 was 66 (F#) and is now 65 (F), with a regression test that checks Ionian repeats at the octave across all 48 steps. The same check found older slips of the same kind that are left alone for now, because some sit inside the Note range songs use: Aeolian step 36 (61, expected 62), Locrian steps 11, 18 and 21 (19, 31 and 35, expected 18, 30 and 36) and Pentatonic Minor steps 23, 26 and 27 (29, 32 and 32, expected 27, 31 and 31).

The 26 failing host test cases (golden full-project round trip and others) fail identically with and without this work.

Not yet done: a real `arduino-cli` build with `-ffast-math`, and a hardware check of the OLED layout and LED colours.
