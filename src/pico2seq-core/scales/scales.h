#ifndef SCALES_H
#define SCALES_H

#include <cstddef>
#include <cstdint>

// Scales: step-index -> semitone tables that quantize Note-lane values into melody.
// 13 scales x 48 steps; readers must clamp past a scale's top note. Portable C++.
constexpr size_t SCALES_COUNT = 13;   // Number of distinct scale definitions
constexpr size_t SCALE_STEPS  = 48;   // Number of step-to-semitone entries per scale

// Melody globals for UI/sequencer scale selection. Voices must NOT read these
// directly — inject tables via setters so DSP stays testable and decoupled.
extern int scale[SCALES_COUNT][SCALE_STEPS]; // Step -> semitone offset per scale
extern const char* scaleNames[SCALES_COUNT]; // UI names, same order as tables
extern uint8_t currentScale; // Selected scale index, 0..SCALES_COUNT-1

/**
 * Count the distinct pitch classes before the first octave in a scale row.
 * The arpeggiator uses this to select its physical-pad layout: seven-note
 * scales get one octave per 8-column row, while other scales keep the legacy
 * linear 32-degree ladder.
 */
inline uint8_t scaleNotesPerOctave(const int *row) noexcept
{
  if (!row)
    return 0;
  bool seen[12] = {};
  uint8_t count = 0;
  for (uint8_t i = 0; i < 12; ++i)
  {
    const int pitchClass = row[i] % 12;
    if (row[i] < 12 && !seen[pitchClass])
    {
      seen[pitchClass] = true;
      ++count;
    }
  }
  return count;
}

#endif // SCALES_H
