# Scales Module Documentation

## 1. Overview & Architecture

The `src/pico2seq-core/scales/` module defines the musical tuning system for the Pico2Seq synthesizer. It provides 13 scale definitions spanning 4 octaves (48 steps), mapping sequencer step indices to semitone offsets for internal audio synthesis and external MIDI generation.

```
                              ┌─────────────────────────────────────────────────────────┐
                              │                 Scale Table Definition                  │
                              │           scale[SCALES_COUNT][SCALE_STEPS]              │
                              │                  (13 scales x 48 steps)                 │
                              └────────────────────────────┬────────────────────────────┘
                                                           │
                                   ┌───────────────────────┴───────────────────────┐
                                   │                                               │
                                   ▼                                               ▼
              ┌────────────────────────────────────────┐     ┌─────────────────────────────────────────┐
              │      Internal Audio Synthesis          │     │          External MIDI Output           │
              │         (src/voice/Voice.cpp)          │     │        (Pico2Seq.ino / MidiManager)     │
              │                                        │     │                                         │
              │  scaleTable[s][note+harm]  (injected)  │     │  scaleSemitone = scale[s][note]         │
              │  midiNote = semitone + 48 + oct        │     │  midiNote = scaleSemitone + 36 + oct    │
              │             ▲                          │     │             ▲                           │
              │             │ C3 Base (MIDI 48)        │     │             │ C2 Base (MIDI 36)         │
              │  (note+harm clamped 0..47, note 0..127)│     │                                         │
              │  frequencyLookupTable[midiNote]        │     │  midiNoteManager.noteOn()               │
              │  -> Oscillator Frequency in Hz         │     │  -> Internal MIDI note (0-127 clamped)    │
              └────────────────────────────────────────┘     └─────────────────────────────────────────┘
```

### Key Architectural Principles

1. **Portability & Host Testability**:
   Like the sequencer core, `src/pico2seq-core/scales/` has zero Arduino or hardware dependencies. It is compiled directly into host unit test binaries (`tests/unit/test_scales.cpp`).
2. **Decoupled Synthesis Injection**:
   Synthesis components (such as `Voice`) do not read global scale variables directly. Instead, scale tables and active scale pointers are injected via `Voice::setScaleTable()` and `Voice::setCurrentScalePointer()`. Passing `nullptr` enables chromatic fallback, allowing unit tests to run without global state. Injection does no preprocessing: `setScaleTable()` stores the pointer and marks the cached base frequency dirty (the former precomputed unique-degree rank caches were write-only and were removed 2026-09-05).
3. **Dual Pitch Base Offsets**:
   - **Internal Audio Synthesis**: Centered at **C3** (MIDI note 48, base +48).
   - **External MIDI Output**: Centered at **C2** (MIDI note 36, base +36).

---

## 2. Scale Constants & Global Definitions

All scale constants and arrays are declared in `src/pico2seq-core/scales/scales.h`:

```cpp
// Centralized scale size constants
constexpr size_t SCALES_COUNT = 13;   // Number of distinct scale definitions
constexpr size_t SCALE_STEPS  = 48;   // Number of step-to-semitone entries per scale

// Global scale data
extern int scale[SCALES_COUNT][SCALE_STEPS]; // 2D semitone lookup tables
extern const char* scaleNames[SCALES_COUNT]; // Human-readable scale names
extern uint8_t currentScale;                 // Active scale index (0..SCALES_COUNT-1)
```

### Memory Footprint

- **Scale Array**: $13 \times 48 \times 4\text{ bytes} = 2,496\text{ bytes}$ (statically allocated in RAM/Flash).
- **Scale Names**: 13 string pointers with minimal text overhead.
- **Lookup Time**: $O(1)$ constant-time lookup for all scale and step combinations.

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
| **1** | **Dorian** | `1 - 2 - b3 - 4 - 5 - 6 - b7` | `0, 2, 3, 5, 7, 8, 10, 12` | Jazzy minor with raised 6th |
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

## 4. Dual Pitch Offset Architecture

A critical architectural distinction exists between how scale semitones are converted for **internal audio synthesis** versus **external MIDI output**:

### 4.1 Internal Audio Synthesis: C3 Base (+48)

In `src/voice/Voice.cpp` (`calculateNoteFrequency`):

```cpp
inline float Voice::calculateNoteFrequency(float note, int8_t octaveOffset, int harmony) noexcept
{
  // Keep note+harmony inside the 48-step scale row even with extreme values.
  int noteWithHarmony = static_cast<int>(note) + harmony;
  if (noteWithHarmony < 0)
    noteWithHarmony = 0;
  if (noteWithHarmony >= static_cast<int>(SCALE_STEPS))
    noteWithHarmony = static_cast<int>(SCALE_STEPS) - 1;

  // Single lookup path: the injected table when present, otherwise chromatic
  // mapping (each scale step is one semitone above C3).
  int scaleSemitone;
  if (scaleTable != nullptr && scaleTableCount > 0)
    scaleSemitone = scaleTable[effectiveScaleIndex_()][noteWithHarmony];
  else
    scaleSemitone = noteWithHarmony;

  // Map to MIDI centered at 48 (C3) and saturate so the octave offset can
  // never index past the 128-entry frequency lookup table.
  int midiNote = scaleSemitone + 48 + static_cast<int>(octaveOffset);
  if (midiNote < 0)
    midiNote = 0;
  if (midiNote > 127)
    midiNote = 127;

  return frequencyLookupTable[midiNote];
}
```

- **Base Pitch**: **C3** (MIDI note 48 = 130.81 Hz).
- **Pitch Range**:
  - Minimum step (0 semitones, -12 octave offset): MIDI note 36 (C2 = 65.41 Hz).
  - Nominal root (0 semitones, 0 octave offset): MIDI note 48 (C3 = 130.81 Hz).
  - Extreme values saturate: an out-of-range MIDI note (e.g. 72 semitones with +12 octave offset = 132) is clamped to 0..127 before the lookup-table read, so the synthesis path can never index out of bounds.
- **Rationale**: Internal oscillator waveforms and ladder filter character are voiced to sound full and punchy centered in the C3 octave.

### 4.2 External MIDI Output: C2 Base (+36)

In `Pico2Seq.ino` (`updateVoiceMIDI` / `onStepCallback`) and `src/midi/MidiManager.cpp`:

```cpp
uint8_t noteIndex = static_cast<uint8_t>(std::max(0.0f, std::min(state.noteIndex, static_cast<float>(SCALE_STEPS - 1))));
int midiNote = scale[currentScale][noteIndex] + 36 + static_cast<int>(state.octaveOffset);

// Clamp MIDI note to valid range (0-127)
int clampedMidiNote = std::max(0, std::min(midiNote, 127));

midiNoteManager.noteOn(voiceId, static_cast<int8_t>(clampedMidiNote),
                       static_cast<uint8_t>(state.velocityLevel * 127), 1, state.gateLengthTicks);
```

- **Base Pitch**: **C2** (MIDI note 36 = 65.41 Hz).
- **Pitch Range**: MIDI notes 24 (C1) to 108 (C8).
- **Rationale**: External synthesizers, samplers, and DAWs expect step sequencer output in standard bass/lead registers starting at C2.

---

## 5. Octave Offset Mapping & Clamping Rules

### 5.1 Octave Mapping Function (`mapFloatToOctaveOffset`)

In `src/pico2seq-core/sequencer/Sequencer.cpp`, the continuous float value stored in `ParamId::Octave` (`0.0f` to `1.0f`) is quantized into discrete semitone offsets:

```cpp
constexpr float OCTAVE_LOW_THRESHOLD = 0.15f;  // Below this: transpose down 1 octave
constexpr float OCTAVE_HIGH_THRESHOLD = 0.40f; // Above this: transpose up 1 octave

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

### 5.2 Bounds Clamping

To prevent out-of-bounds memory access and undefined behavior:
1. **Step Index Bounds**: `state.noteIndex` is clamped to `[0, SCALE_STEPS - 1]` (`0..47`).
2. **MIDI Note Clamping**: The final MIDI note number is clamped to `[0, 127]`.
3. **Scale Index Bounds**: a missing `currentScale` pointer (or no injected table) selects row 0 of the injected table; an out-of-range queued scale index is clamped to the **last** row (`effectiveScaleIndex_()`), never read out of bounds.

---

## 6. Scale Injection in `Voice` (`setScaleTable` / `setCurrentScalePointer`)

### 6.1 Purpose & Decoupling

`Voice` decouples itself from global state by taking scale data via setter injection:

```cpp
void Voice::setScaleTable(const int (*table)[48], size_t scaleCount);
void Voice::setCurrentScalePointer(const uint8_t *currentScalePtr);
```

Injection does **no preprocessing**: `setScaleTable()` only stores the table
pointer and row count and marks the cached base frequency dirty
(`baseFreqDirty_ = true`). The former precomputed unique-degree rank caches
(`scaleUniqueCounts` / `scaleIndexToRank` / `scaleUniqueIndexList`) were
write-only and were removed on 2026-09-05. `VoiceManager::addVoice` injects the
`scale[]` globals at setup time; host tests pass `nullptr` to both setters to
exercise the chromatic fallback path.

### 6.2 Runtime Lookup Behavior (`Voice.cpp`)

- **Single lookup path**: `calculateNoteFrequency()` reads the **injected**
  table via `scaleTable[effectiveScaleIndex_()][noteIndex + harmony]`. With no
  table injected (`nullptr`), it falls back to **chromatic mapping** (scale
  step = semitone above C3).
- **Clamping**: `noteIndex + harmony` is clamped to `0..47` and the resulting
  MIDI note is saturated to `0..127` before the lookup-table read (see section
  4.1).
- **Live scale switches**: the control core samples `currentScalePtr_` in
  setters / `flushControlUpdates()` and queues the index through `Voice`'s
  bounded control queue — the audio thread never reads the UI global. The
  effective scale row is part of the pitch snapshot
  (`PitchSnapshot::scaleIndex`), so a queued scale change marks the static base
  frequency dirty and the next pitch recompute picks it up (repeated notes
  repitch too).
- Because there is no per-scale preprocessing, lookups stay a single array
  index per sample; degree/harmony transposition indexes the row directly and
  relies on the clamps above for safety.

---

## 7. Developer Guidelines: Adding New Scales

To add a new musical scale to Pico2Seq:

1. **Update Constants in `scales.h`**:
   ```cpp
   constexpr size_t SCALES_COUNT = 14; // Increment scale count
   ```
2. **Add Human-Readable Name in `scales.cpp`**:
   ```cpp
   const char* scaleNames[SCALES_COUNT] = {
       // ... existing scales ...
       "Custom Scale Name"
   };
   ```
3. **Define 48-Step Semitone Table in `scale[][]`**:
   Ensure the array contains exactly 48 ascending integers covering 4 octaves (0 to 72 semitones):
   ```cpp
   {0, 2, 4, 7, 9, 12, 14, 16, 19, 21, 24, 26, 28, 31, 33, 36,
    38, 40, 43, 45, 48, 50, 52, 55, 57, 60, 62, 64, 67, 69, 72, 72,
    72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72}
   ```
4. **Run Unit Tests**:
   Execute `ctest` or run `pico2seq_tests "[scales]"` to ensure the new table satisfies monotonicity and range constraints.