#pragma once

#include <cstddef>
#include <cstdint>

#include "../scales/scales.h"
#include "Tuning.h"

// TuningScales.h - which scales make sense in which tuning.
//
// Musical role: a scale says WHICH notes a pattern uses, and most of the classic ones only mean
// something in 12 notes per octave: Dorian has no place in 24-EDO. So every tuning carries its
// own short list of scales. The twelve-note tunings (12-EDO, the just and historical ones) keep
// the modes you know; 22 Shruti offers the ten Hindustani thaats; 24-EDO offers the Arabic
// maqams Rast, Bayati, Hijaz and Saba; 19, 31, 22, 17, 41 and 53-EDO each get a diatonic, a minor
// and a pentatonic scale built from their own steps; the harmonic series, Partch and
// Bohlen-Pierce have scales drawn from their own notes; everything else plays All Degrees.
// Choosing a tuning switches to a scale of the new tuning automatically: the scale you have if
// the tuning offers it, else the one you last used there, else the tuning's first.
//
// Technical role: pure functions over flash tables, no globals, no allocation. Scale numbers are
// the rows of scales/scales.h, which are persisted, so a set may grow but never drop or
// renumber a scale. Core 0 only.
namespace tuning {

constexpr size_t kMaxSetScales = 24; // longest set; buttons 1-6 reach the first six

struct ScaleSet
{
  const uint8_t *scales; // scale numbers in display order; scales[0] is the tuning's default
  uint8_t count;         // never zero
};

// The scales a tuning offers. An unknown id gets the twelve-note set.
ScaleSet scaleSet(uint8_t tuningId) noexcept;
bool scaleAvailable(uint8_t tuningId, uint8_t scaleIndex) noexcept;
// The first scale of the set: what a tuning sounds like before you choose.
uint8_t defaultScale(uint8_t tuningId) noexcept;
// Position of a scale in the set (0-based), or -1 when the tuning does not offer it.
int scaleSlot(uint8_t tuningId, uint8_t scaleIndex) noexcept;
// The scale in a slot, clamped to the set.
uint8_t scaleAtSlot(uint8_t tuningId, int slot) noexcept;
// Step through the set and wrap. A scale the tuning does not offer enters at the first scale
// going forward and the last going back.
uint8_t stepScale(uint8_t tuningId, uint8_t scaleIndex, int steps) noexcept;
// The scale itself when the tuning offers it, else the tuning's default. Used on song load.
uint8_t coerceScale(uint8_t tuningId, uint8_t scaleIndex) noexcept;

// Degrees per period the rows of a scale are written in: 12 for a classic row, the tuning's own
// count for All Degrees, and the fixed count a tuned row was built for. Used to count notes per
// octave for the arpeggiator.
int scalePeriodDegrees(uint8_t tuningId, uint8_t scaleIndex) noexcept;

// Choose the scale to play after the tuning changes to `tuningId`: the current scale when the
// new tuning offers it, else the one remembered for that tuning in the bank, else its default.
uint8_t scaleForTuning(const Bank &bank, uint8_t tuningId, uint8_t currentScaleIndex) noexcept;

// applyTuning() and swapWithPrevious() plus the scale that goes with the new tuning: the scale
// being left is remembered for the tuning being left, and `scaleIndex` becomes the scale of the
// new tuning. False (nothing changed) under the same conditions as the plain versions.
bool applyTuningWithScale(Selection &selection, Bank &bank, uint8_t tuningId,
                          uint8_t &scaleIndex) noexcept;
bool swapWithPreviousWithScale(Selection &selection, Bank &bank, uint8_t &scaleIndex) noexcept;

} // namespace tuning
