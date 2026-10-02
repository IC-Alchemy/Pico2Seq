#include "Tuning.h"

#include <cmath>
#include <cstdio>

// Pitch maths, note names and the favourites bookkeeping (see Tuning.h). Everything is
// allocation-free; the only functions the audio core calls are degreeCents(),
// slotToDegree(), rowValueToDegree() and frequencyHz(), which read flash tables only.
namespace tuning
{
namespace
{
constexpr const char *kWesternNames[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                           "F#", "G",  "G#", "A",  "A#", "B"};
// Komal (flat) svaras are lower case; tivra Ma carries a sharp.
constexpr const char *kSargamNames[12] = {"Sa",  "re", "Re", "ga",  "Ga",  "Ma",
                                          "Ma#", "Pa", "dha", "Dha", "ni", "Ni"};
// Octave number printed for the scale root at octave offset 0 (MIDI 48 is C3).
constexpr int kRootOctave = 3;

int roundToInt(float x) noexcept { return static_cast<int>(std::floor(x + 0.5f)); }

// Position of a degree within its period as a svara: which of the 12 svaras it is, and
// how many shrutis above that svara. False when the tuning has no svara layout.
bool svaraOf(const Tuning &t, int indexInPeriod, int &svara, int &above) noexcept
{
  if (t.chroma)
  {
    svara = 0;
    for (int pc = 0; pc < 12; ++pc)
      if (t.chroma[pc] <= indexInPeriod)
        svara = pc;
    above = indexInPeriod - t.chroma[svara];
    return true;
  }
  if (t.degrees == 12 && t.periodCents == 1200.0f)
  {
    svara = indexInPeriod;
    above = 0;
    return true;
  }
  return false;
}
} // namespace

int floorDiv(int value, int divisor) noexcept
{
  int q = value / divisor;
  if ((value % divisor != 0) && (value < 0))
    --q;
  return q;
}

float degreeCents(const Tuning &tuning, int degree) noexcept
{
  const int n = tuning.degrees;
  const int period = floorDiv(degree, n);
  return tuning.cents[degree - period * n] + tuning.periodCents * static_cast<float>(period);
}

int slotToDegree(const Tuning &tuning, int slot) noexcept
{
  const int n = tuning.degrees;
  // A twelve-note tuning is the semitone ladder itself, whatever its cents.
  if (n == 12 && tuning.periodCents == 1200.0f)
    return slot;
  if (tuning.chroma)
  {
    const int octave = floorDiv(slot, 12);
    return octave * n + tuning.chroma[slot - octave * 12];
  }
  // Nearest degree to slot * 100 c, ties to the lower degree. A candidate must win by
  // kTieCents to displace the incumbent, so an exact tie (17-EDO's 600 c sits midway
  // between two steps) resolves the same way whatever the float rounding or -ffast-math
  // reordering does to the last bit.
  constexpr float kTieCents = 0.01f;
  const float target = 100.0f * static_cast<float>(slot);
  const float period = tuning.periodCents;
  const int k = static_cast<int>(std::floor(target / period));
  const float base = period * static_cast<float>(k);
  int best = k * n;
  float bestDistance = std::fabs(target - (base + tuning.cents[0]));
  for (int i = 1; i < n; ++i)
  {
    const float distance = std::fabs(target - (base + tuning.cents[i]));
    if (distance < bestDistance - kTieCents)
    {
      bestDistance = distance;
      best = k * n + i;
    }
  }
  if (std::fabs(target - (base + period)) < bestDistance - kTieCents)
    best = (k + 1) * n; // the first degree of the next period is the closer neighbour
  return best;
}

float rootHz(const Selection &selection) noexcept
{
  const int tonic = selection.tonic < 12 ? selection.tonic : 11;
  return selection.a4Hz *
         std::exp2(static_cast<float>(kRootMidiNote + tonic - kA4MidiNote) * (1.0f / 12.0f));
}

bool isStandard(const Selection &selection) noexcept
{
  return selection.tuningId == kDefaultTuningId && selection.tonic == 0 &&
         selection.a4Hz == kDefaultA4Hz;
}

PitchWorld makeWorld(const Selection &selection) noexcept
{
  PitchWorld world;
  world.tuning = &resolve(selection.tuningId);
  world.tonic = selection.tonic < 12 ? selection.tonic : 11;
  world.rootHz = rootHz(selection);
  world.standard = isStandard(selection);
  return world;
}

int rowValueToDegree(const PitchWorld &world, int rowValue, bool nativeScale) noexcept
{
  if (nativeScale)
    return rowValue;
  return slotToDegree(world.tuning ? *world.tuning : resolve(kDefaultTuningId), rowValue);
}

float frequencyHz(const PitchWorld &world, int degree, int octaveSemitones) noexcept
{
  const Tuning &t = world.tuning ? *world.tuning : resolve(kDefaultTuningId);
  const float cents = degreeCents(t, degree) + 100.0f * static_cast<float>(octaveSemitones);
  float hz = world.rootHz * std::exp2(cents * (1.0f / 1200.0f));
  if (hz < kMinHz)
    hz = kMinHz;
  else if (hz > kMaxHz)
    hz = kMaxHz;
  return hz;
}

void noteName(const PitchWorld &world, int degree, int octaveSemitones, char *out,
              size_t size, float extraCents) noexcept
{
  if (!out || size == 0)
    return;
  const Tuning &t = world.tuning ? *world.tuning : resolve(kDefaultTuningId);
  const float cents = degreeCents(t, degree) + 100.0f * static_cast<float>(octaveSemitones);
  const float soundingCents = cents + extraCents; // Western and Quarter show the detune
  const int octaveShift = floorDiv(octaveSemitones, 12);

  switch (t.naming)
  {
  case Naming::Western:
  {
    // Nearest 12-EDO note of the sounding pitch, with the leftover in cents.
    const float midi = static_cast<float>(kRootMidiNote + world.tonic) + soundingCents * 0.01f;
    const int nearest = roundToInt(midi);
    const int deviation = roundToInt((midi - static_cast<float>(nearest)) * 100.0f);
    const int pc = nearest - 12 * floorDiv(nearest, 12);
    const int octave = floorDiv(nearest, 12) - 1;
    if (deviation != 0)
      std::snprintf(out, size, "%s%d%+dc", kWesternNames[pc], octave, deviation);
    else
      std::snprintf(out, size, "%s%d", kWesternNames[pc], octave);
    return;
  }
  case Naming::Quarter:
  {
    // Half-semitone grid: an odd cell is a quarter-tone above the semitone below it.
    const float midi = static_cast<float>(kRootMidiNote + world.tonic) + soundingCents * 0.01f;
    const int cell = roundToInt(midi * 2.0f);
    const int deviation = roundToInt((midi * 2.0f - static_cast<float>(cell)) * 50.0f);
    const int semitone = floorDiv(cell, 2);
    const bool quarter = (cell - 2 * semitone) != 0;
    const int pc = semitone - 12 * floorDiv(semitone, 12);
    const int octave = floorDiv(semitone, 12) - 1;
    if (deviation != 0)
      std::snprintf(out, size, "%s%s%d%+dc", kWesternNames[pc], quarter ? "+" : "", octave,
                    deviation);
    else
      std::snprintf(out, size, "%s%s%d", kWesternNames[pc], quarter ? "+" : "", octave);
    return;
  }
  case Naming::Sargam:
  {
    const int period = floorDiv(degree, t.degrees);
    const int indexInPeriod = degree - period * t.degrees;
    int svara = 0;
    int above = 0;
    if (svaraOf(t, indexInPeriod, svara, above))
    {
      const int octave = kRootOctave + period + octaveShift;
      if (above == 0)
        std::snprintf(out, size, "%s%d", kSargamNames[svara], octave);
      else if (above == 1)
        std::snprintf(out, size, "%s+%d", kSargamNames[svara], octave);
      else
        std::snprintf(out, size, "%s+%d.%d", kSargamNames[svara], above, octave);
      return;
    }
    break; // no svara layout: fall through to the numeric form
  }
  case Naming::Numeric:
    break;
  }
  const int period = floorDiv(degree, t.degrees);
  const int indexInPeriod = degree - period * t.degrees;
  std::snprintf(out, size, "%d:%02d", kRootOctave + period + octaveShift, indexInPeriod);
}

const char *tonicName(uint8_t tonic) noexcept
{
  return kWesternNames[tonic < 12 ? tonic : 11];
}

void formatA4(float a4Hz, char *out, size_t size) noexcept
{
  if (!out || size == 0)
    return;
  // Integer maths on purpose: the embedded printf may be built without float support.
  float hz = a4Hz;
  if (hz < 0.0f)
    hz = 0.0f;
  else if (hz > 9999.9f)
    hz = 9999.9f;
  const int tenths = roundToInt(hz * 10.0f);
  std::snprintf(out, size, "%d.%dHz", tenths / 10, tenths % 10);
}

// --- Organisation -------------------------------------------------------------------------

void defaultBank(Bank &bank) noexcept
{
  for (uint8_t i = 0; i < kFavoriteSlots; ++i)
    bank.favorites[i] = kEmptySlot;
  bank.favorites[0] = 0;   // 12-EDO
  bank.favorites[1] = 1;   // 24-EDO Quarter-Tone
  bank.favorites[2] = 32;  // 5-Limit JI
  bank.favorites[3] = 96;  // 22 Shruti
  bank.previousId = kDefaultTuningId;
  for (uint8_t i = 0; i < kMaxLibrary; ++i)
    bank.lastScale[i] = kNoScale;
}

bool storeFavorite(Bank &bank, uint8_t slot, uint8_t tuningId) noexcept
{
  if (slot >= kFavoriteSlots || !find(tuningId))
    return false;
  bank.favorites[slot] = tuningId;
  return true;
}

bool clearFavorite(Bank &bank, uint8_t slot) noexcept
{
  if (slot >= kFavoriteSlots)
    return false;
  bank.favorites[slot] = kEmptySlot;
  return true;
}

uint8_t favoriteAt(const Bank &bank, uint8_t slot) noexcept
{
  if (slot >= kFavoriteSlots)
    return kEmptySlot;
  return bank.favorites[slot];
}

bool bankValid(const Bank &bank) noexcept
{
  for (uint8_t i = 0; i < kFavoriteSlots; ++i)
    if (bank.favorites[i] != kEmptySlot && !find(bank.favorites[i]))
      return false;
  return find(bank.previousId) != nullptr;
}

bool applyTuning(Selection &selection, Bank &bank, uint8_t tuningId) noexcept
{
  if (!find(tuningId) || tuningId == selection.tuningId)
    return false;
  bank.previousId = selection.tuningId;
  selection.tuningId = tuningId;
  return true;
}

bool swapWithPrevious(Selection &selection, Bank &bank) noexcept
{
  if (!find(bank.previousId) || bank.previousId == selection.tuningId)
    return false;
  const uint8_t current = selection.tuningId;
  selection.tuningId = bank.previousId;
  bank.previousId = current;
  return true;
}

} // namespace tuning
