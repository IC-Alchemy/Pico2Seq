# Scales Module Documentation

## Overview

The `src/scales` folder contains the musical scale system for the PicoMudrasSequencer. This module provides comprehensive scale definitions, note-to-MIDI conversion tables, and scale selection management for the step sequencer.

## Key Components

### 1. Scale Data Structure

**Constants:**
- `SCALES_COUNT = 13` - Total number of available scales
- `SCALE_STEPS = 48` - Number of semitone entries per scale (4 octaves)

**Global Variables:**
- `scale[13][48]` - 2D array of semitone values for each scale
- `scaleNames[13]` - Human-readable names for each scale
- `currentScale` - Currently selected scale index (0-12)

### 2. Available Scales

**Diatonic Scales:**
- **Ionian Major** (0): Standard major scale (1-2-3-4-5-6-7)
- **Dorian** (1): Minor with raised 6th (1-2-b3-4-5-6-b7)
- **Phrygian** (2): Minor with lowered 2nd (1-b2-b3-4-5-b6-b7)
- **Lydian** (3): Major with raised 4th (1-2-3-#4-5-6-7)
- **Mixolydian** (4): Major with lowered 7th (1-2-3-4-5-6-b7)
- **Aeolian Minor** (5): Natural minor (1-2-b3-4-5-b6-b7)
- **Locrian** (6): Diminished scale (1-b2-b3-4-b5-b6-b7)

**Exotic Scales:**
- **Pentatonic Minor** (7): 5-note minor pentatonic (1-b3-4-5-b7)
- **Phrygian Dominant** (8): Spanish/Phrygian dominant (1-b2-3-4-5-b6-b7)
- **Lydian Dominant** (9): Acoustic scale (1-2-3-#4-5-6-b7)
- **Harmonic Minor** (10): Harmonic minor (1-2-b3-4-5-b6-7)

**Special Scales:**
- **Wholetone** (11): All whole steps (1-2-3-4-5-6)
- **Chromatic** (12): All 12 semitones (1-b2-2-b3-3-4-b5-5-b6-6-b7)

## Scale Data Format

### Semitone Mapping

Each scale is defined as an array of 48 semitone values:
- Range: 0-72 semitones (6 octaves total)
- Resolution: 1 semitone per step
- Coverage: 4 octaves of musical range

**Example - Ionian Major:**
```cpp
{0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24, 26,
 28, 29, 31, 33, 35, 36, 38, 40, 41, 43, 45, 47, 48, 50, 52, 53,
 55, 57, 59, 60, 62, 64, 66, 67, 69, 71, 72, 72, 72, 72, 72, 72}
```

### Note Calculation

**MIDI Note Conversion:**
```cpp
// Convert step value to MIDI note
int midiNote = scale[currentScale][stepValue] + 36;  // C2 base
// stepValue: 0-47 (from sequencer)
// Result: MIDI notes 36-108 (C2 to C8)
```

**Octave Range:**
- Base octave: C2 (MIDI note 36)
- Available range: C2 to C8 (6 octaves)
- Extended range: Fills remaining steps with C8

## Usage Examples

### Basic Scale Access

```cpp
// Get current scale name
const char* currentName = scaleNames[currentScale];

// Get semitone value for step 12 in current scale
int semitones = scale[currentScale][12];

// Convert to MIDI note
int midiNote = semitones + 36;  // C2 base
```

### Scale Selection

```cpp
// Change to Dorian scale
currentScale = 1;

// Change to Pentatonic Minor
currentScale = 7;

// Cycle through scales
currentScale = (currentScale + 1) % SCALES_COUNT;
```

### Integration with Sequencer

```cpp
// In sequencer step processing
uint8_t stepValue = getCurrentStepValue();
int semitones = scale[currentScale][stepValue];
int midiNote = semitones + 36;
sendMidiNoteOn(midiNote, velocity, channel);
```

## Scale Characteristics

### Modal Relationships

**Major Scale Modes:**
- Ionian (Major): Bright, stable
- Dorian: Jazzy, minor with raised 6th
- Phrygian: Spanish, lowered 2nd
- Lydian: Dreamy, raised 4th
- Mixolydian: Bluesy, lowered 7th
- Aeolian: Natural minor, melancholic
- Locrian: Unstable, diminished

**Advanced Scales:**
- Phrygian Dominant: Middle Eastern, Spanish flamenco
- Lydian Dominant: Modern jazz, acoustic guitar
- Harmonic Minor: Classical, dramatic
- Pentatonic Minor: Blues, folk, rock

### Musical Applications

**Genre Suitability:**
- **Pop/Rock**: Ionian, Aeolian, Mixolydian, Pentatonic
- **Jazz**: Dorian, Lydian, Lydian Dominant
- **Classical**: Ionian, Aeolian, Harmonic Minor
- **World Music**: Phrygian, Phrygian Dominant
- **Ambient**: Lydian, Wholetone

**Creative Uses:**
- Chromatic: Experimental, atonal compositions
- Wholetone: Dreamy, impressionistic textures
- Pentatonic: Melodic simplicity, accessibility

## Architecture Notes

### Design Principles

1. **Centralized Constants**: All scale dimensions defined as constants
2. **Modular Access**: Global variables for UI/sequencer, dependency injection for synthesis
3. **Performance Optimized**: Pre-calculated semitone arrays for fast lookup
4. **Extensible Design**: Easy addition of new scales

### Memory Considerations

**Static Arrays:**
- Scale data: 13 scales × 48 steps × 2 bytes = 1248 bytes
- Scale names: 13 pointers + string storage
- Total footprint: Minimal for embedded system

**Runtime Performance:**
- O(1) lookup time for any scale/step combination
- No dynamic memory allocation
- Suitable for real-time audio applications

## Integration Points

- **Sequencer Module**: Uses scale tables for note conversion
- **Voice Module**: Receives scale references via dependency injection
- **UI Module**: Displays current scale name and handles scale selection
- **MIDI Module**: Converts scale steps to MIDI note numbers

## File Structure

```
src/scales/
├── scales.cpp          # Scale definitions and data arrays
└── scales.h            # Interface definitions and constants
```

## Development Guidelines

### Adding New Scales

1. **Define Scale Intervals:**
   ```cpp
   // Example: New custom scale
   {0, 3, 5, 7, 10, 12, 15, 17, 19, 22, 24, 27, 29, 31, 34, 36,
    39, 41, 43, 46, 48, 51, 53, 55, 58, 60, 63, 65, 67, 70, 72, 72,
    72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72}
   ```

2. **Add Scale Name:**
   ```cpp
   const char* scaleNames[SCALES_COUNT] = {
       // ... existing names ...
       "Custom Scale"
   };
   ```

3. **Update Constants:**
   ```cpp
   constexpr size_t SCALES_COUNT = 14;  // Increment count
   ```

### Best Practices

- **Test Scale Ranges**: Verify all 48 steps produce valid musical intervals
- **Consider Musicality**: Ensure scales have reasonable voice leading
- **Document Intent**: Add comments explaining scale character and use cases
- **Maintain Consistency**: Follow existing formatting and naming conventions

## Troubleshooting

- **Invalid Notes**: Check step value bounds (0-47)
- **Scale Index Errors**: Verify currentScale is within valid range (0-12)
- **MIDI Range Issues**: Ensure final MIDI notes are within 0-127 range
- **Memory Issues**: Confirm sufficient RAM for scale data arrays