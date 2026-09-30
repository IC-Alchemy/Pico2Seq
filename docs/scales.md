# Scales Module Documentation

## 1. Overview & Architecture

The `src/pico2seq-core/scales/` module defines the musical tuning system for the Pico2Seq synthesizer. It provides 47 scale definitions spanning up to 6 periods (48 steps), mapping sequencer step indices to semitone offsets (classic rows 0-17) or to tuning degrees (All Degrees and the 29 tuned rows 18-46) for internal audio synthesis. Which degrees sound at which pitch is the job of the tuning layer, see [tuning.md](tuning.md). Optional MIDI callbacks remain in the portable sequencer, but the firmware has no MIDI transport.

```text
Scale tables -> Voice scale-degree/pitch lookup -> oscillator frequency
```

The former parallel C2-base conversion for the dormant firmware MIDI tracker
has been removed; it is not part of the audio pitch path.

### Key Architectural Principles

1. **Portability & Host Testability**:
   Like the sequencer core, `src/pico2seq-core/scales/` has zero Arduino or hardware dependencies. It is compiled directly into host unit test binaries (`tests/unit/test_scales.cpp`).
2. **Decoupled Synthesis Injection**:
   Synthesis components (such as `Voice`) do not read global scale variables directly. Instead, scale tables and active scale pointers are injected via `Voice::setScaleTable()` and `Voice::setCurrentScalePointer()`. Passing `nullptr` enables chromatic fallback, allowing unit tests to run without global state. `setScaleTable()` only stores the pointer and marks the base frequency dirty; the former unique-degree rank caches were write-only and were removed 2026-09-05 (see [voice.md](voice.md)).
3. **Audio Pitch Base**:
   Internal audio synthesis is centered at **C3** (MIDI note 48, base +48).
   MIDI note numbers here describe pitch; they do not imply MIDI transmission.

---

## 2. Scale Constants & Global Definitions

All scale constants and arrays are declared in `src/pico2seq-core/scales/scales.h`:

```cpp
// Centralized scale size constants
constexpr size_t CLASSIC_SCALES_COUNT = 18; // rows 0-17: 12-EDO semitone slots (13 = All Degrees is native)
constexpr size_t SCALES_COUNT = 47;   // Number of distinct scale definitions (18 classic + 29 tuned)
constexpr size_t SCALE_STEPS  = 48;   // Number of step-to-pitch entries per scale
constexpr size_t SCALE_ALL_DEGREES = 13;
constexpr uint64_t NATIVE_SCALE_MASK = /* bit 13 and bits 18..46 */;  // rows written in tuning degrees

// Global scale data
extern int scale[SCALES_COUNT][SCALE_STEPS]; // 2D semitone lookup tables
extern const char* scaleNames[SCALES_COUNT]; // Human-readable scale names
extern uint8_t currentScale;                 // Active scale index (0..SCALES_COUNT-1)
```

### Memory Footprint

- **Scale Array**: $47 \times 48 \times 4\text{ bytes} = 9,024\text{ bytes}$ (statically allocated in RAM/Flash).
- **Scale Names**: 47 full-name and 47 short-name (at most 10 characters, for the OLED) string pointers.
- **Lookup Time**: $O(1)$ constant-time lookup for all scale and step combinations.
- **Arpeggiator layout hint**: `scaleNotesPerOctave()` (`scales.h`) counts the distinct pitch classes in a scale row before the first octave; the arpeggiator uses it to give seven-note scales one octave per 8-column pad row, while other scales keep the linear 32-degree ladder.

---

## 3. Scale Definitions

The 13 scales encompass diatonic modes, exotic scales, whole-tone, and chromatic tunings. Each table contains 48 integer semitone values spanning 4 full octaves (0 to 72 semitones relative to root):

```cpp
const char* scaleNames[SCALES_COUNT] = {
    "Ionian Major",      // 0
    "Dorian",            // 1
    "Phrygian",          // 2
    "Lydian",            // 3
    "Mixolydian",        // 4
    "Aeolian Minor",     // 5
    "Locrian",           // 6
    "Pentatonic Minor",  // 7
    "Phrygian Dominant", // 8
    "Lydian Dominant",   // 9
    "Harmonic Minor",    // 10
    "Wholetone",         // 11
    "Chromatic"          // 12
};
```

### Complete Scale Reference

| Index | Scale Name | Scale Degrees & Formula | First Octave Semitone Sequence | Musical Character |
|---|---|---|---|---|
| **0** | **Ionian Major** | `1 - 2 - 3 - 4 - 5 - 6 - 7` | `0, 2, 4, 5, 7, 9, 11, 12` | Bright, resolute, standard major |
| **1** | **Dorian** | `1 - 2 - b3 - 4 - 5 - 6 - b7` | `0, 2, 3, 5, 7, 9, 10, 12` | Jazzy minor with raised 6th |
| **2** | **Phrygian** | `1 - b2 - b3 - 4 - 5 - b6 - b7` | `0, 1, 3, 5, 7, 8, 10, 12` | Dark, Spanish flavor with lowered 2nd |
| **3** | **Lydian** | `1 - 2 - 3 - #4 - 5 - 6 - 7` | `0, 2, 4, 6, 7, 9, 11, 12` | Dreamy, mystical with raised 4th |
| **4** | **Mixolydian** | `1 - 2 - 3 - 4 - 5 - 6 - b7` | `0, 2, 4, 5, 7, 9, 10, 12` | Bluesy, classic rock major with flat 7th |
| **5** | **Aeolian Minor** | `1 - 2 - b3 - 4 - 5 - b6 - b7` | `0, 2, 3, 5, 7, 8, 10, 12` | Natural minor, melancholic |
| **6** | **Locrian** | `1 - b2 - b3 - 4 - b5 - b6 - b7`| `0, 1, 3, 5, 6, 8, 10, 12` | Tense, diminished, unstable |
| **7** | **Pentatonic Minor** | `1 - b3 - 4 - 5 - b7` (padded) | `0, 0, 3, 3, 5, 5, 7, 7, 10, 10, 12, 12` | Blues/rock; duplicate steps pad grid |
| **8** | **Phrygian Dominant** | `1 - b2 - 3 - 4 - 5 - b6 - b7` | `0, 1, 4, 5, 7, 8, 10, 12` | Middle Eastern / Flamenco (5th mode of Harmonic Minor) |
| **9** | **Lydian Dominant** | `1 - 2 - 3 - #4 - 5 - 6 - b7` | `0, 2, 4, 6, 7, 9, 10, 12` | Acoustic / Overtone scale (4th mode of Melodic Minor) |
| **10**| **Harmonic Minor** | `1 - 2 - b3 - 4 - 5 - b6 - 7` | `0, 2, 3, 5, 7, 8, 11, 12` | Dramatic classical minor with leading tone |
| **11**| **Wholetone** | `1 - 2 - 3 - #4 - #5 - #6` | `0, 2, 4, 6, 8, 10, 12` | Impressionistic, symmetrical whole steps |
| **12**| **Chromatic** | All 12 semitones | `0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11` | Linear 1:1 semitone mapping (0..47) |

> **Note on Pentatonic Minor Padding**: Because the 5-note pentatonic scale has fewer degrees than 7-note diatonic modes, adjacent step indices in `scale[7]` duplicate pitch values (e.g. `{0, 0, 3, 3, 5, 5, ...}`) to preserve smooth tactile response across the 48-step range.

---

### Scales added with the tuning system (rows 13-46)

Saved songs store the scale by index, so rows may only be appended. The classic rows 0-12 are
above; the rest, all written for the tuning shown:

| Rows | Scales | Written in |
|---|---|---|
| 13 | All Degrees (every degree of the active tuning, in order; in 12-EDO it equals Chromatic) | degrees of the active tuning |
| 14-17 | Bhairav, Marwa, Poorvi and Todi thaats | 12-EDO semitone slots (22 Shruti maps them to its own shrutis) |
| 18-21 | Maqam Rast, Bayati, Hijaz, Saba | 24-EDO |
| 22-24, 25-27, 28-30 | Major, Minor, Pentatonic for 19-EDO, 31-EDO, 22-EDO | that EDO |
| 31-32 | 17-EDO Major, Minor | 17-EDO |
| 33-34 | 15-EDO Heptatonic, Pentatonic | 15-EDO |
| 35 | 10-EDO Pentatonic | 10-EDO |
| 36-38, 39-41 | Major, Minor, Pentatonic for 41-EDO, 53-EDO | that EDO |
| 42-43 | Overtone Heptatonic, Pentatonic | Overtone 16-31 |
| 44-45 | Partch Major, Partch Minor | Partch 43-Tone |
| 46 | Bohlen-Pierce Lambda | Bohlen-Pierce 13 |

A native row is `min(P * (i // k) + p[i % k], 6 * P)` for a scale of `k` notes `p[]` in a
period of `P` degrees: it climbs one period per `k` steps and holds its top note after six
periods. `Voice::setScaleTable(table, count, NATIVE_SCALE_MASK)` tells the voice which rows
are native; `scaleNotesPerPeriod(row, period)` counts a row's notes for the arpeggiator and
`tuning::scalePeriodDegrees` supplies the period.
Which tuning offers which scales is `tuning/TuningScales.cpp`, described in [tuning.md](tuning.md).

---

## 4. Dual Pitch Offset Architecture

Scale semitones are converted to oscillator frequencies in the audio voice.
The removed firmware MIDI tracker no longer has a parallel pitch path.

### 4.1 Internal Audio Synthesis: C3 Base (+48)

In `src/voice/Voice.cpp` (`calculateNoteFrequency`):

```cpp
inline float Voice::calculateNoteFrequency(float note, int8_t octaveOffset, int harmony) noexcept
{
  const int noteIndex = note;
  uint8_t scaleIndex = 0;
  if (currentScalePtr)
    scaleIndex = *currentScalePtr;

  int noteWithHarmony = noteIndex + harmony;

  // Lookup semitone directly from scale table
  int scaleSemitone = scale[scaleIndex][noteWithHarmony];

  // Map to MIDI centered at 48 (C3) and apply octave offset
  int midiNote = scaleSemitone + 48 + static_cast<int>(octaveOffset);

  return frequencyLookupTable[midiNote];
}
```

- **Base Pitch**: **C3** (MIDI note 48 = 130.81 Hz).
- **Pitch Range**:
  - Minimum step (0 semitones, -12 octave offset): MIDI note 36 (C2 = 65.41 Hz).
  - Nominal root (0 semitones, 0 octave offset): MIDI note 48 (C3 = 130.81 Hz).
  - High step (72 semitones, +12 octave offset): MIDI note 132 — beyond the table's 128 entries; sequencer parameter ranges keep the computed note inside the table (there is no runtime clamp on this synthesis path).
- **Rationale**: Internal oscillator waveforms and ladder filter character are voiced to sound full and punchy centered in the C3 octave.

### 4.2 Removed Firmware MIDI Conversion

The former C2-base (+36) conversion in step playback served only a removed,
firmware-only MIDI conversion path. That path is gone. Audio pitch is unchanged.
See [MIDI status](midi.md) for the
separate optional hooks retained in the portable sequencer.

---

## 5. Octave Offset Mapping & Clamping Rules

### 5.1 Octave Mapping Function (`mapFloatToOctaveOffset`)

In `src/pico2seq-core/sequencer/Sequencer.cpp`, the portable core's fallback quantizes the continuous float value stored in `ParamId::Octave` (`0.0f` to `1.0f`) into discrete semitone offsets:

```cpp
constexpr float OCTAVE_LOW_THRESHOLD = 1.0f / 3.0f;  // Below this: down an octave (-12)
constexpr float OCTAVE_HIGH_THRESHOLD = 2.0f / 3.0f; // Above this: up an octave (+12)

int8_t mapFloatToOctaveOffset(float octaveValue)
{
    if (octaveValue < OCTAVE_LOW_THRESHOLD)
    {
        return -12; // -1 Octave (-12 semitones)
    }
    else if (octaveValue > OCTAVE_HIGH_THRESHOLD)
    {
        return 12;  // +1 Octave (+12 semitones)
    }
    else
    {
        return 0;   // Nominal (0 semitones)
    }
}
```

This three-zone fallback is not what the firmware plays: the sequencer's octave mapper is injected at setup (`src/app/VoiceSetup.cpp` passes `VoiceEdit::mapOctave` to `Sequencer::setPlaybackTransform()`; `Sequencer.cpp` decodes each step through the injected mapper when present). `VoiceEdit::mapOctave` (`src/voice/VoiceEditParameters.cpp:1066-1069`) quantizes the lane into **five** zones spanning −2..+2 octaves — nearest zone edge at hand heights of roughly 136/297/458/619 mm across the 55–700 mm recording window (zone boundaries at stored 0.125/0.375/0.625/0.875).

### 5.2 Bounds Clamping

To prevent out-of-bounds memory access and undefined behavior:
1. **Step Index Bounds**: `state.noteIndex` is clamped to `[0, SCALE_STEPS - 1]` (`0..47`).
2. **MIDI Note Clamping**: The final MIDI note number is clamped to `[0, 127]`.
3. **Scale Index Bounds**: `currentScale` is constrained to `[0, SCALES_COUNT - 1]` (`0..12`). Out-of-bounds pointers fall back to scale index 0.

---

## 6. Developer Guidelines: Adding New Scales

To add a new musical scale to Pico2Seq:

1. **Update Constants in `scales.h`**:
   ```cpp
   constexpr size_t SCALES_COUNT = 48; // Increment scale count (and keep the tail of the static_asserts true)
   ```
   Append only: saved songs store the scale by index. A tuned (native) row also has to fall in
   `NATIVE_SCALE_MASK` and be added to the set of the tuning it belongs to in
   `tuning/TuningScales.cpp` (the enum there has a `static_assert` tying it to this file).
2. **Add Human-Readable Name in `scales.cpp`**:
   ```cpp
   const char* scaleNames[SCALES_COUNT] = {
       // ... existing scales ...
       "Custom Scale Name"
   };
   ```
   and a `scaleShortNames[]` entry of at most 10 characters.
3. **Define 48-Step Semitone Table in `scale[][]`**:
   Ensure the array contains exactly 48 ascending integers covering 4 octaves (0 to 72 semitones):
   ```cpp
   {0, 2, 4, 7, 9, 12, 14, 16, 19, 21, 24, 26, 28, 31, 33, 36,
    38, 40, 43, 45, 48, 50, 52, 55, 57, 60, 62, 64, 67, 69, 72, 72,
    72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72}
   ```
4. **Run Unit Tests**:
   Execute `ctest` or run `pico2seq_tests "[scales]"` to ensure the new table satisfies monotonicity and range constraints.