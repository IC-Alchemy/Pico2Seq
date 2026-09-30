#include "TuningScales.h"

namespace tuning
{
namespace
{
// Scale numbers by name, in the order of scales/scales.cpp. The tuned rows follow the classic
// rows 0-17; the static_assert at the end of the list ties the two files together.
enum Scale : uint8_t
{
  kIonian = 0, kDorian, kPhrygian, kLydian, kMixolydian, kAeolian, kLocrian,
  kPentatonicMinor, kPhrygianDominant, kLydianDominant, kHarmonicMinor, kWholeTone, kChromatic,
  kAllDegrees, kBhairav, kMarwa, kPoorvi, kTodi,
  // 24-EDO
  kRast, kBayati, kHijaz, kSaba,
  // Equal divisions with a diatonic generated from their own fifth
  k19Major, k19Minor, k19Penta,
  k31Major, k31Minor, k31Penta,
  k22Major, k22Minor, k22Penta,
  k17Major, k17Minor,
  k15Hepta, k15Penta,
  k10Penta,
  k41Major, k41Minor, k41Penta,
  k53Major, k53Minor, k53Penta,
  // Harmonic series, Partch, Bohlen-Pierce
  kOvertoneHepta, kOvertonePenta,
  kPartchMajor, kPartchMinor,
  kBohlenPierceLambda,
};
static_assert(kBohlenPierceLambda == SCALES_COUNT - 1, "scale numbers follow scales/scales.cpp");
static_assert(kTodi == CLASSIC_SCALES_COUNT - 1, "the classic rows end at Todi");

// Twelve notes per octave: the modes and scales everyone knows, then the four thaats.
// All Degrees is left out here because in twelve notes it is the same as Chromatic.
constexpr uint8_t kTwelveNote[] = {kIonian, kDorian, kPhrygian, kLydian, kMixolydian, kAeolian,
                                   kLocrian, kPentatonicMinor, kPhrygianDominant,
                                   kLydianDominant, kHarmonicMinor, kWholeTone, kChromatic,
                                   kBhairav, kMarwa, kPoorvi, kTodi};

// Hindustani twelve-svara tunings: the thaats first. Bilawal, Kafi, Bhairavi, Kalyan, Khamaj and
// Asavari are Ionian, Dorian, Phrygian, Lydian, Mixolydian and Aeolian under their Indian names.
constexpr uint8_t kSvara[] = {kBhairav, kMarwa, kPoorvi, kTodi, kIonian, kDorian, kPhrygian,
                              kLydian, kMixolydian, kAeolian, kLocrian, kPentatonicMinor,
                              kPhrygianDominant, kLydianDominant, kHarmonicMinor, kWholeTone,
                              kChromatic};

// The ten thaats on the 22 shrutis (the twelve svaras sit on fixed shrutis, see the chroma map
// of the tuning), then every shruti.
constexpr uint8_t kShruti[] = {kBhairav, kMarwa, kPoorvi, kTodi, kIonian, kDorian, kPhrygian,
                               kLydian, kMixolydian, kAeolian, kAllDegrees};

constexpr uint8_t kEdo24[] = {kRast, kBayati, kHijaz, kSaba, kAllDegrees};
constexpr uint8_t kEdo19[] = {k19Major, k19Minor, k19Penta, kAllDegrees};
constexpr uint8_t kEdo31[] = {k31Major, k31Minor, k31Penta, kAllDegrees};
constexpr uint8_t kEdo22[] = {k22Major, k22Minor, k22Penta, kAllDegrees};
constexpr uint8_t kEdo17[] = {k17Major, k17Minor, kAllDegrees};
constexpr uint8_t kEdo15[] = {k15Hepta, k15Penta, kAllDegrees};
constexpr uint8_t kEdo10[] = {k10Penta, kAllDegrees};
constexpr uint8_t kEdo41[] = {k41Major, k41Minor, k41Penta, kAllDegrees};
constexpr uint8_t kEdo53[] = {k53Major, k53Minor, k53Penta, kAllDegrees};
constexpr uint8_t kOvertone[] = {kOvertoneHepta, kOvertonePenta, kAllDegrees};
constexpr uint8_t kPartch[] = {kPartchMajor, kPartchMinor, kAllDegrees};
constexpr uint8_t kBohlenPierce[] = {kBohlenPierceLambda, kAllDegrees};
constexpr uint8_t kAllOnly[] = {kAllDegrees}; // 7- and 5-EDO, undertones, Carlos

template <size_t N> constexpr ScaleSet makeSet(const uint8_t (&scales)[N]) noexcept
{
  static_assert(N > 0 && N <= kMaxSetScales, "a set is 1..kMaxSetScales scales");
  return ScaleSet{scales, static_cast<uint8_t>(N)};
}

struct Entry
{
  uint8_t tuningId;
  ScaleSet set;
};

// Tunings that are not twelve-note (or are twelve-note Indian ones). Anything not listed is a
// twelve-note tuning of the West and gets kTwelveNote; the test suite checks that every library
// tuning with a number of degrees other than 12 is listed.
constexpr Entry kEntries[] = {
    {1, makeSet(kEdo24)},      // 24-EDO Quarter-Tone
    {2, makeSet(kEdo19)},      // 19-EDO
    {3, makeSet(kEdo31)},      // 31-EDO
    {4, makeSet(kEdo22)},      // 22-EDO
    {5, makeSet(kEdo17)},      // 17-EDO
    {6, makeSet(kEdo15)},      // 15-EDO
    {7, makeSet(kEdo10)},      // 10-EDO
    {8, makeSet(kAllOnly)},    // 7-EDO
    {9, makeSet(kAllOnly)},    // 5-EDO
    {10, makeSet(kEdo41)},     // 41-EDO
    {11, makeSet(kEdo53)},     // 53-EDO
    {35, makeSet(kOvertone)},  // Overtone 16-31
    {36, makeSet(kAllOnly)},   // Undertone 32-17
    {37, makeSet(kPartch)},    // Partch 43-Tone
    {96, makeSet(kShruti)},    // 22 Shruti
    {97, makeSet(kSvara)},     // 12 Svara JI
    {98, makeSet(kSvara)},     // Pythagorean Svara
    {128, makeSet(kBohlenPierce)},
    {129, makeSet(kAllOnly)},  // Carlos Alpha
    {130, makeSet(kAllOnly)},  // Carlos Beta
    {131, makeSet(kAllOnly)},  // Carlos Gamma
};

uint8_t rememberedScale(const Bank &bank, uint8_t tuningId) noexcept
{
  const int index = libraryIndexOf(tuningId);
  return index >= 0 ? bank.lastScale[index] : kNoScale;
}

void rememberScale(Bank &bank, uint8_t tuningId, uint8_t scaleIndex) noexcept
{
  const int index = libraryIndexOf(tuningId);
  if (index >= 0 && scaleAvailable(tuningId, scaleIndex))
    bank.lastScale[index] = scaleIndex;
}
} // namespace

ScaleSet scaleSet(uint8_t tuningId) noexcept
{
  for (const Entry &entry : kEntries)
    if (entry.tuningId == tuningId)
      return entry.set;
  return makeSet(kTwelveNote);
}

bool scaleAvailable(uint8_t tuningId, uint8_t scaleIndex) noexcept
{
  return scaleSlot(tuningId, scaleIndex) >= 0;
}

uint8_t defaultScale(uint8_t tuningId) noexcept
{
  return scaleSet(tuningId).scales[0];
}

int scaleSlot(uint8_t tuningId, uint8_t scaleIndex) noexcept
{
  const ScaleSet set = scaleSet(tuningId);
  for (uint8_t i = 0; i < set.count; ++i)
    if (set.scales[i] == scaleIndex)
      return i;
  return -1;
}

uint8_t scaleAtSlot(uint8_t tuningId, int slot) noexcept
{
  const ScaleSet set = scaleSet(tuningId);
  if (slot < 0)
    slot = 0;
  if (slot >= set.count)
    slot = set.count - 1;
  return set.scales[slot];
}

uint8_t stepScale(uint8_t tuningId, uint8_t scaleIndex, int steps) noexcept
{
  const ScaleSet set = scaleSet(tuningId);
  const int count = set.count;
  const int position = scaleSlot(tuningId, scaleIndex);
  int target;
  if (position < 0)
    target = steps >= 0 ? 0 : count - 1;
  else
    target = ((position + steps) % count + count) % count;
  return set.scales[target];
}

uint8_t coerceScale(uint8_t tuningId, uint8_t scaleIndex) noexcept
{
  return scaleAvailable(tuningId, scaleIndex) ? scaleIndex : defaultScale(tuningId);
}

int scalePeriodDegrees(uint8_t tuningId, uint8_t scaleIndex) noexcept
{
  return scaleIsNative(scaleIndex) ? resolve(tuningId).degrees : 12;
}

uint8_t scaleForTuning(const Bank &bank, uint8_t tuningId, uint8_t currentScaleIndex) noexcept
{
  if (scaleAvailable(tuningId, currentScaleIndex))
    return currentScaleIndex;
  const uint8_t remembered = rememberedScale(bank, tuningId);
  if (remembered != kNoScale && scaleAvailable(tuningId, remembered))
    return remembered;
  return defaultScale(tuningId);
}

bool applyTuningWithScale(Selection &selection, Bank &bank, uint8_t tuningId,
                          uint8_t &scaleIndex) noexcept
{
  if (!find(tuningId) || tuningId == selection.tuningId)
    return false;
  rememberScale(bank, selection.tuningId, scaleIndex);
  if (!applyTuning(selection, bank, tuningId))
    return false;
  scaleIndex = scaleForTuning(bank, tuningId, scaleIndex);
  return true;
}

bool swapWithPreviousWithScale(Selection &selection, Bank &bank, uint8_t &scaleIndex) noexcept
{
  if (!find(bank.previousId) || bank.previousId == selection.tuningId)
    return false;
  rememberScale(bank, selection.tuningId, scaleIndex);
  if (!swapWithPrevious(selection, bank))
    return false;
  scaleIndex = scaleForTuning(bank, selection.tuningId, scaleIndex);
  return true;
}

} // namespace tuning
