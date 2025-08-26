#ifndef SCALES_H
#define SCALES_H

#include <Arduino.h>

// Dimensions for scale tables and chord progressions.
// Do not change these without updating associated data in scales.cpp.

// Array dimension constants
constexpr size_t SCALES_COUNT = 10;              // Number of available scales
constexpr size_t SCALE_STEPS = 47;               // Number of steps per scale (across multiple octaves)
constexpr size_t NUM_CHORD_PROGRESSIONS = 9;     // Number of chord progression patterns
constexpr size_t CHORD_PROGRESSION_LENGTH = 8;   // Number of steps per chord progression

// Global scale data used across the application.
// Synthesis code should not access these directly; inject references instead
// (e.g., Voice::setScaleTable, Voice::setCurrentScalePointer).
extern int scaleTable[SCALES_COUNT][SCALE_STEPS];     // step -> semitone offset for each scale
extern const char* scaleNames[SCALES_COUNT];          // human-readable names per scale
extern uint8_t currentScale;                          // selected scale index [0..SCALES_COUNT-1]

// Chord progression table expressed in 1-based scale degrees.
// Voice.cpp subtracts 1 at use-site to convert to 0-based indices.
extern const short chordProgression[NUM_CHORD_PROGRESSIONS][CHORD_PROGRESSION_LENGTH];

#endif // SCALES_H
