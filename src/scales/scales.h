#ifndef SCALES_H
#define SCALES_H

#include <Arduino.h>

// Centralized scale size constants — use these everywhere instead of hard-coded literals.
// This makes it safe to change the number of scales or the number of steps per scale.
constexpr size_t SCALES_COUNT = 13;   // Number of distinct scale definitions
constexpr size_t SCALE_STEPS  = 48;   // Number of step-to-semitone entries per scale

// Global scale data used across the application.
// NOTE: Synthesis components (e.g., Voice) should NOT access these globals directly.
//       Instead, inject references via setters (e.g., Voice::setScaleTable and
//       Voice::setCurrentScalePointer) to keep modules testable and decoupled.
//       These globals remain for UI/Sequencer modules that manage scale selection.
extern int scale[SCALES_COUNT][SCALE_STEPS];          // Scale tables: dynamic dimensions
extern const char* scaleNames[SCALES_COUNT];          // Human-readable scale names
extern uint8_t currentScale;      // Currently selected scale index (0..SCALES_COUNT-1)

#endif // SCALES_H
