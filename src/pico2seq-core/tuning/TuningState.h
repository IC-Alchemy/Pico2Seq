#pragma once

#include "../scales/scales.h"
#include "Tuning.h"
#include "TuningScales.h"

// TuningState.h - the one global tuning, beside currentScale.
//
// Musical role: a single tuning, tonic and A4 shared by all four voices, so they can never
// drift out of tune with each other. Technical role: Core 0 owns and writes both objects
// (Tuning page, song load); voices sample tuningSelection on the control core through
// Voice::setTuningPointer() and only ever see the copied PitchWorld. Defined by
// src/app/AppState.cpp on the firmware and tests/unit/test_helpers.cpp on the host.
extern tuning::Selection tuningSelection; // 12-EDO, tonic C, A4 440 until a song says otherwise
extern tuning::Bank tuningBank;           // favourites and the A/B partner

// What the screens need to name a pitch: the active world, and whether the scale at this
// index holds tuning degrees (All Degrees) instead of 12-EDO semitone slots.
struct TuningView
{
  tuning::PitchWorld world;
  bool nativeScale;
};

inline TuningView currentTuningView(size_t scaleIndex) noexcept
{
  return TuningView{tuning::makeWorld(tuningSelection), scaleIsNative(scaleIndex)};
}

// Distinct notes per period of the playing scale, in the playing tuning: what the arpeggiator
// asks to choose its pad layout. A classic row counts in twelve semitone slots, a native row
// in the tuning's own degrees, so Maqam Rast on 24-EDO reads seven like Ionian does.
inline uint8_t currentScaleNotesPerOctave() noexcept
{
  const size_t index = currentScale < SCALES_COUNT ? currentScale : SCALES_COUNT - 1;
  return scaleNotesPerPeriod(scale[index], tuning::scalePeriodDegrees(tuningSelection.tuningId,
                                                                         static_cast<uint8_t>(index)));
}
