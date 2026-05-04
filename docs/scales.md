# Scales Module Documentation

A scale is a map from gesture to pitch.

The `src/scales` folder contains the musical scale tables for Pico2Seq. The sequencer stores step values. The scale module turns those values into semitone offsets, then MIDI notes.

## Data Shape

Constants:

- `SCALES_COUNT = 13`: number of available scales.
- `SCALE_STEPS = 48`: number of entries per scale.

Globals:

- `scale[13][48]`: semitone table for every scale.
- `scaleNames[13]`: display names.
- `currentScale`: selected scale index, `0..12`.

## Available Scales

Diatonic scales:

- Ionian Major `(0)`: major scale, `1-2-3-4-5-6-7`.
- Dorian `(1)`: minor with raised 6th, `1-2-b3-4-5-6-b7`.
- Phrygian `(2)`: minor with lowered 2nd, `1-b2-b3-4-5-b6-b7`.
- Lydian `(3)`: major with raised 4th, `1-2-3-#4-5-6-7`.
- Mixolydian `(4)`: major with lowered 7th, `1-2-3-4-5-6-b7`.
- Aeolian Minor `(5)`: natural minor, `1-2-b3-4-5-b6-b7`.
- Locrian `(6)`: diminished mode, `1-b2-b3-4-b5-b6-b7`.

Other scales:

- Pentatonic Minor `(7)`: five-note minor pentatonic, `1-b3-4-5-b7`.
- Phrygian Dominant `(8)`: `1-b2-3-4-5-b6-b7`.
- Lydian Dominant `(9)`: `1-2-3-#4-5-6-b7`.
- Harmonic Minor `(10)`: `1-2-b3-4-5-b6-7`.
- Wholetone `(11)`: whole-step movement, `1-2-3-4-5-6`.
- Chromatic `(12)`: all 12 semitones, `1-b2-2-b3-3-4-b5-5-b6-6-b7`.

## Semitone Table Format

Each scale is stored as 48 semitone values. The table reaches from `0` to `72` semitones. The last entries can clamp to the top value.

Example, Ionian Major:

```cpp
{0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24, 26,
 28, 29, 31, 33, 35, 36, 38, 40, 41, 43, 45, 47, 48, 50, 52, 53,
 55, 57, 59, 60, 62, 64, 66, 67, 69, 71, 72, 72, 72, 72, 72, 72}
```

## MIDI Note Conversion

The base is C2, MIDI note `36`.

```cpp
// Convert step value to MIDI note
int midiNote = scale[currentScale][stepValue] + 36;  // C2 base
// stepValue: 0-47 (from sequencer)
// Result: MIDI notes 36-108 (C2 to C8)
```

Facts to preserve:

- Step value input range is `0..47`.
- Scale index range is `0..12`.
- Base note is C2, MIDI `36`.
- Resulting documented range is C2 to C8, MIDI `36..108`.

## Basic Access

```cpp
// Get current scale name
const char* currentName = scaleNames[currentScale];

// Get semitone value for step 12 in current scale
int semitones = scale[currentScale][12];

// Convert to MIDI note
int midiNote = semitones + 36;  // C2 base
```

## Scale Selection

```cpp
// Change to Dorian scale
currentScale = 1;

// Change to Pentatonic Minor
currentScale = 7;

// Cycle through scales
currentScale = (currentScale + 1) % SCALES_COUNT;
```

## Sequencer Integration

```cpp
// In sequencer step processing
uint8_t stepValue = getCurrentStepValue();
int semitones = scale[currentScale][stepValue];
int midiNote = semitones + 36;
sendMidiNoteOn(midiNote, velocity, channel);
```

## Musical Notes

The firmware does not judge the scale. It just keeps the table fast and predictable.

- Ionian, Aeolian, Mixolydian, and Pentatonic fit common pop and rock use.
- Dorian, Lydian, and Lydian Dominant are useful for jazz-leaning material.
- Ionian, Aeolian, and Harmonic Minor cover common classical tonal colors.
- Phrygian and Phrygian Dominant carry stronger regional color.
- Lydian and Wholetone are useful for ambient or floating patterns.
- Chromatic gives every semitone and leaves the musical choice to the player.

## Architecture Notes

- The scale tables are precomputed for constant-time lookup.
- No dynamic allocation is used.
- Scale data is static: `13 x 48 x 2 bytes = 1248 bytes`, plus names.
- UI code displays `scaleNames[currentScale]`.
- Sequencer code uses `scale[currentScale][stepValue]`.
- MIDI code receives the final note number after adding the C2 offset.

## File Structure

```text
src/scales/
|-- scales.cpp          # Scale definitions and data arrays
`-- scales.h            # Interface definitions and constants
```

## Adding a Scale

Add the semitone table:

```cpp
// Example: New custom scale
{0, 3, 5, 7, 10, 12, 15, 17, 19, 22, 24, 27, 29, 31, 34, 36,
 39, 41, 43, 46, 48, 51, 53, 55, 58, 60, 63, 65, 67, 70, 72, 72,
 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72}
```

Add the name:

```cpp
const char* scaleNames[SCALES_COUNT] = {
    // ... existing names ...
    "Custom Scale"
};
```

Update the count:

```cpp
constexpr size_t SCALES_COUNT = 14;  // Increment count
```

Then verify:

- All 48 steps return valid semitone values.
- `currentScale` remains in range.
- Final MIDI notes remain within `0..127`.
- The scale name appears correctly in the UI.
