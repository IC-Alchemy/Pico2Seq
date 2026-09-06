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
              │  scaleSemitone = scale[s][note+harm]   │     │  scaleSemitone = scale[s][note]         │
              │  midiNote = scaleSemitone + 48 + oct   │     │  midiNote = scaleSemitone + 36 + oct    │
              │             ▲                          │     │             ▲                           │
              │             │ C3 Base (MIDI 48)        │     │             │ C2 Base (MIDI 36)         │
              │                                        │     │                                         │
              │  frequencyLookupTable[midiNote]        │     │  midiNoteManager.noteOn()               │
              │  -> Oscillator Frequency in Hz         │     │  -> USB MIDI Note On (0-127 clamped)    │
              └────────────────────────────────────────┘     └─────────────────────────────────────────┘
```

### Key Architectural Principles

1. **Portability & Host Testability**:
   Like the sequencer core, `src/pico2seq-core/scales/` has zero Arduino or hardware dependencies. It is compiled directly into host unit test binaries (`tests/unit/test_scales.cpp`).
2. **Decoupled Synthesis Injection**:
   Synthesis components (such as `Voice`) do not read global scale variables directly. Instead, scale tables and active scale pointers are injected via `Voice::setScaleTable()` and `Voice::setCurrentScalePointer()`. Passing `nullptr` enables chromatic fallback, allowing unit tests to run without global state.
3. **Precomputed Unique-Degree Rank Cache**:
   `Voice::setScaleTable()` precomputes scale degree ranks (`scaleUniqueCounts`, `scaleIndexToRank`, `scaleUniqueIndexList`) outside the realtime path, enabling $O(1)$ indexed lookups for harmony and degree transposition during audio processing.
4. **Dual Pitch Base Offsets**:
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
3. **Scale Index Bounds**: `currentScale` is constrained to `[0, SCALES_COUNT - 1]` (`0..12`). Out-of-bounds pointers fall back to scale index 0.

---

## 6. `Voice::setScaleTable` Precomputed Rank Cache

### 6.1 Purpose & Decoupling

`Voice` decouples itself from global state by taking scale data via setter injection:

```cpp
void Voice::setScaleTable(const int (*table)[48], size_t scaleCount);
void Voice::setCurrentScalePointer(const uint8_t *currentScalePtr);
```

When `setScaleTable()` is called during voice initialization, it precomputes lookup structures in `Voice.h` to optimize realtime degree manipulation (e.g. harmony shifts, modal transposition).

### 6.2 Cache Data Structures (`Voice.h`)

```cpp
std::vector<uint8_t> scaleUniqueCounts;    // Size: scaleCount
std::vector<uint8_t> scaleIndexToRank;     // Size: scaleCount * 48
std::vector<uint8_t> scaleUniqueIndexList; // Size: scaleCount * 48 (padded)
```

- `scaleUniqueCounts[s]`: The total count of distinct pitch degrees in scale `s`.
- `scaleIndexToRank[s * 48 + i]`: Maps the original 48-step index `i` (`0..47`) to its unique degree rank `u` (`0..uniqueCount - 1`).
- `scaleUniqueIndexList[s * 48 + r]`: The original scale index (`0..47`) where the `r`-th unique pitch degree begins.

### 6.3 Precomputation Algorithm (`Voice.cpp`)

```cpp
void Voice::setScaleTable(const int (*table)[48], size_t scaleCount)
{
  scaleTable = table;
  scaleTableCount = scaleCount;
  baseFreqDirty_ = true;

  scaleUniqueCounts.clear();
  scaleIndexToRank.clear();
  scaleUniqueIndexList.clear();

  if (scaleTable == nullptr || scaleTableCount == 0) return;

  scaleUniqueCounts.resize(scaleCount);
  scaleIndexToRank.resize(scaleCount * 48);
  scaleUniqueIndexList.resize(scaleCount * 48);

  for (size_t s = 0; s < scaleCount; ++s)
  {
    const int *row = scaleTable[s];

    // 1. Identify step boundaries where the pitch value changes
    uint8_t uniquePos[48];
    uint8_t uniqueCount = 0;
    uniquePos[uniqueCount++] = 0; // First unique degree is always step 0

    for (int i = 1; i < static_cast<int>(SCALE_STEPS); ++i)
    {
      if (row[i] != row[i - 1])
      {
        uniquePos[uniqueCount++] = static_cast<uint8_t>(i);
      }
    }

    scaleUniqueCounts[s] = uniqueCount;

    // 2. Populate padded unique index list
    const size_t base = s * 48;
    for (uint8_t u = 0; u < uniqueCount; ++u)
    {
      scaleUniqueIndexList[base + u] = uniquePos[u];
    }
    for (uint8_t u = uniqueCount; u < 48; ++u)
    {
      scaleUniqueIndexList[base + u] = uniquePos[uniqueCount - 1]; // Pad remainder
    }

    // 3. Build index-to-rank mapping
    for (uint8_t u = 0; u < uniqueCount; ++u)
    {
      const uint8_t start = uniquePos[u];
      const uint8_t end = (u + 1 < uniqueCount) 
                            ? static_cast<uint8_t>(uniquePos[u + 1] - 1) 
                            : static_cast<uint8_t>(SCALE_STEPS - 1);
      for (uint8_t j = start; j <= end; ++j)
      {
        scaleIndexToRank[base + j] = u;
      }
    }
  }
}
```

### 6.4 Realtime Benefits

By precalculating these arrays on initialization:
- Degree stepping (e.g. transposing up by $N$ scale degrees regardless of scale step padding) is executed with simple array indexing.
- Eliminates loops and dynamic branching on Core 1 during real-time sample processing.
- Guaranteed deterministic $O(1)$ computation time per sample.

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