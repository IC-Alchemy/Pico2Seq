#include "scales.h"

/*
================================================================================
Scale and Harmony Tables

Representation:
- Each scale row maps step indices (0..SCALE_STEPS-1) to semitone offsets from
  the root, ascending across multiple octaves. Example (Ionian): 0,2,4,5,7,9,11,
  then +12 to repeat pattern in higher octaves.

Progressions in Degree Language:
- chordProgression uses 1-based degree numbers portable across parent scales.
  Voice.cpp interprets degrees in the currently selected scale and handles
  0-based indexing.

Notes:
- Only the first 10 scale rows and names are intentionally populated. The
  remaining entries (up to SCALES_COUNT) are left default-initialized to
  preserve the project’s current behavior.
================================================================================
*/

// Human-readable scale names. Only first 10 entries are defined by design.
const char* scaleNames[SCALES_COUNT] = {
    "Major",
    "Dorian", 
    "Lydian",
    "Mixolydian",
    "Natural Minor",
    "Harmonic Minor",
    "Phrygian Dominant",
    "Lydian Dominant",
    "Whole Tone",
    "Chromatic"
    // Remaining entries default to nullptr
};

// Semitone offsets across multiple octaves for each scale.
int scaleTable[SCALES_COUNT][SCALE_STEPS] = {    
    // Ionian (Major): 1-2-3-4-5-6-7
    {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24, 26,
     28, 29, 31, 33, 35, 36, 38, 40, 41, 43, 45, 47, 48, 50, 52, 53,
     55, 57, 59, 60, 62, 64, 66, 67, 69, 71, 72, 72, 72, 72, 72, 72},

    // Dorian: 1-2-b3-4-5-6-b7
    {0, 2, 3, 5, 7, 8, 10, 12, 14, 15, 17, 19, 20, 22, 24, 26,
     27, 29, 31, 32, 34, 36, 38, 39, 41, 43, 44, 46, 48, 50, 51, 53,
     55, 56, 58, 60, 62, 63, 65, 67, 68, 70, 72, 72, 72, 72, 72, 72},

    // Lydian: 1-2-3-#4-5-6-7
    {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26,
     28, 30, 31, 33, 35, 36, 38, 40, 42, 43, 45, 47, 48, 50, 52, 54,
     55, 57, 59, 60, 62, 64, 66, 67, 69, 71, 72, 72, 72, 72, 72, 72},

    // Mixolydian: 1-2-3-4-5-6-b7
    {0, 2, 4, 5, 7, 9, 10, 12, 14, 16, 17, 19, 21, 22, 24, 26,
     28, 29, 31, 33, 34, 36, 38, 40, 41, 43, 45, 46, 48, 50, 52, 53,
     55, 57, 58, 60, 62, 64, 65, 67, 69, 70, 72, 72, 72, 72, 72, 72},

    // Aeolian (Natural Minor): 1-2-b3-4-5-b6-b7
    {0, 2, 3, 5, 7, 8, 10, 12, 14, 15, 17, 19, 20, 22, 24, 26,
     27, 29, 31, 32, 34, 36, 38, 39, 41, 43, 44, 46, 48, 50, 51, 53,
     55, 56, 58, 60, 61, 63, 65, 67, 68, 70, 72, 72, 72, 72, 72, 72},

    // Harmonic Minor: 1-2-b3-4-5-b6-7
    {0, 2, 3, 5, 7, 8, 11, 12, 14, 15, 17, 19, 20, 23, 24, 26,
     27, 29, 31, 32, 35, 36, 38, 39, 41, 43, 44, 47, 48, 50, 51, 53,
     55, 56, 59, 60, 62, 63, 65, 67, 68, 71, 72, 72, 72, 72, 72, 72},

    // Phrygian Dominant: 1-b2-3-4-5-b6-b7
    {0, 1, 4, 5, 7, 8, 10, 12, 13, 16, 17, 19, 20, 22, 24, 25,
     28, 29, 31, 32, 34, 36, 37, 40, 41, 43, 44, 46, 48, 49, 52, 53,
     55, 56, 58, 60, 61, 64, 65, 67, 68, 70, 72, 72, 72, 72, 72, 72},

    // Lydian Dominant: 1-2-3-#4-5-6-b7
    {0, 2, 4, 6, 7, 9, 10, 12, 14, 16, 18, 19, 21, 22, 24, 26,
     28, 30, 31, 33, 34, 36, 38, 40, 42, 43, 45, 46, 48, 50, 52, 54,
     55, 57, 58, 60, 62, 64, 66, 67, 69, 70, 72, 72, 72, 72, 72, 72},

    // Whole Tone (symmetric by whole steps)
    {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30,
     32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62,
     64, 66, 68, 70, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72},

    // Chromatic (every semitone)
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
     32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47}
    // Remaining rows default-initialized
};

// Portable chord progression table (1-based degrees).
const short chordProgression[NUM_CHORD_PROGRESSIONS][CHORD_PROGRESSION_LENGTH] = {
    {1, 2, 3, 4, 5, 6, 7, 5},
    {1, 1, 4, 4, 2, 2, 5, 5},
    {1, 1, 1, 1, 4, 4, 4, 4},
    {1, 1, 1, 1, 4, 4, 5, 5},
    {1, 1, 4, 4, 1, 1, 5, 5},
    {1, 1, 6, 4, 2, 5, 1, 5},
    {1, 1, 4, 4, 6, 2, 5, 5},
    {1, 1, 4, 4, 1, 1, 4, 5},
    {1, 1, 1, 1, 5, 5, 5, 5}
};