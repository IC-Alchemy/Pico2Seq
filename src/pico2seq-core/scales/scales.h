#ifndef SCALES_H
#define SCALES_H

#include <cstddef>
#include <cstdint>

// Scales: step-index -> pitch tables that quantize Note-lane values into melody.
// SCALES_COUNT scales x 48 steps; readers must clamp past a scale's top note. Portable C++.
//
// Two kinds of row share the table (which tunings offer which is tuning/TuningScales.h):
//   - classic rows (0-12 and 14-17) hold 12-EDO semitone slots. In a 12-note tuning the slot
//     is the degree; in 22 Shruti a slot is mapped to its shruti.
//   - native rows hold the active tuning's own degrees: All Degrees (13) for any tuning, and
//     the tuned rows (18 and up), each written for one tuning's degrees per period - a
//     Dorian mode has no meaning in 24 notes per octave, a maqam does.
// Saved songs store the scale by index: new scales may only be appended.
constexpr size_t CLASSIC_SCALES_COUNT = 18; // rows 0-17, the original thirteen plus All Degrees and four thaats
constexpr size_t SCALES_COUNT = 47;         // classic rows plus 29 tuned rows
constexpr size_t SCALE_STEPS  = 48;         // Number of step-to-pitch entries per scale

// Melody globals for UI/sequencer scale selection. Voices must NOT read these
// directly — inject tables via setters so DSP stays testable and decoupled.
extern int scale[SCALES_COUNT][SCALE_STEPS];       // Step -> semitone slot or tuning degree
extern const char* scaleNames[SCALES_COUNT];       // UI names, same order as tables
extern const char* scaleShortNames[SCALES_COUNT];  // At most 10 characters, for the OLED
extern uint8_t currentScale; // Selected scale index, 0..SCALES_COUNT-1

// Native rows hold tuning degrees, not semitone slots. Kept here, beside the tables, and
// injected into voices as a bit mask so they never read scale globals.
constexpr size_t SCALE_ALL_DEGREES = 13;
constexpr size_t SCALE_FIRST_TUNED = CLASSIC_SCALES_COUNT;
constexpr uint64_t NATIVE_SCALE_MASK =
    (uint64_t{1} << SCALE_ALL_DEGREES) |
    (((uint64_t{1} << (SCALES_COUNT - SCALE_FIRST_TUNED)) - 1) << SCALE_FIRST_TUNED);
static_assert(SCALES_COUNT <= 64, "NATIVE_SCALE_MASK is one bit per scale");
static_assert(SCALES_COUNT <= 255, "a scale index is one byte in saved songs");
constexpr bool scaleIsNative(size_t index, uint64_t mask = NATIVE_SCALE_MASK) noexcept
{
  return index < 64 && ((mask >> index) & 1u) != 0;
}

/**
 * Count the distinct notes of a scale row before it reaches `period` (one octave is 12
 * semitone slots; a native row's period is its tuning's degrees per period).
 * The arpeggiator uses this to select its physical-pad layout: seven-note
 * scales get one octave per 8-column row, while other scales keep the legacy
 * linear 32-degree ladder.
 */
inline uint8_t scaleNotesPerPeriod(const int *row, int period) noexcept
{
  if (!row || period <= 0)
    return 0;
  uint8_t count = 0;
  for (size_t i = 0; i < SCALE_STEPS && row[i] < period; ++i)
  {
    if (i == 0 || row[i] != row[i - 1])
      ++count;
  }
  return count;
}

// A classic row's notes per octave.
inline uint8_t scaleNotesPerOctave(const int *row) noexcept
{
  return scaleNotesPerPeriod(row, 12);
}

#endif // SCALES_H
