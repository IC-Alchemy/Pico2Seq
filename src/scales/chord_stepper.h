#ifndef CHORD_STEPPER_H
#define CHORD_STEPPER_H

#include <Arduino.h>
#include "scales.h"

// Lightweight chord-change scheduler that advances a chord index
// based on step counts. Default: change every 64 steps across 8 chords.
// Easily reconfigured at runtime for other rhythms/patterns.
namespace ChordStepper {

struct Config {
  uint16_t stepsPerChange = 64;   // Fallback default when no pattern provided
  uint8_t  progressionLength = CHORD_PROGRESSION_LENGTH; // Columns in chordProgression row
  const uint16_t* pattern = nullptr; // Optional variable-interval pattern
  uint8_t  patternLength = 0;     // Number of entries in pattern
};

// Configure fixed steps-per-change (e.g., 64)
void setStepsPerChange(uint16_t steps);

// Set chord progression length (wrap modulus), default 8
void setProgressionLength(uint8_t length);

// Provide a pattern of successive step intervals between chord changes,
// e.g., {64, 32, 96} to create evolving rhythms. Pass nullptr to disable.
void setPattern(const uint16_t* pattern, uint8_t length);

// Reset internal state and schedule next change after stepsPerChange/pattern[0]
void reset(uint32_t currentStep = 0);

// Call on each step (e.g., 16th-note callback) with global step counter.
// Will advance chord index when the scheduled step is reached.
void tick(uint32_t currentStep);

// Read current chord index [0..progressionLength-1]
uint8_t getIndex();

} // namespace ChordStepper

#endif // CHORD_STEPPER_H