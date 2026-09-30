#pragma once

#include <cstddef>
#include <cstdint>

// Tuning: what pitch each scale degree actually sounds.
//
// Musical role: a scale says WHICH degrees a pattern uses; a tuning says HOW FAR APART
// the degrees are. The same Note lane can be played in equal temperament, 5-limit just
// intonation, 24 quarter-tones per octave, 22 shrutis, a historical well-temperament or
// a non-octave scale, with a movable tonic (Sa) and reference pitch (A4).
//
// Technical role: portable, allocation-free, flash-resident data plus the pitch maths
// the audio voices and the OLED share. A tuning is an ascending table of cents for one
// period (usually an octave); a degree is an index on the infinite ladder that table
// repeats into. Core note: Core 0 chooses a Selection; Core 1 only reads the immutable
// Tuning tables through a PitchWorld, so nothing here needs a lock. No Arduino
// includes - this folder stays reusable like scales/ and sequencer/.
//
// Persisted by id: ids are part of the saved-song format. Never renumber or reuse one.
namespace tuning {

constexpr uint8_t kFamilyCount = 5;
// Ids are family * kSlotsPerFamily + index within the family, so a family's ids stay
// together and a new tuning slots into its family's free numbers without touching saved songs.
constexpr uint8_t kSlotsPerFamily = 32;
// The Tuning page lays the whole library out on the 32 pads, one tuning per pad in library
// order; the library can therefore never hold more than this.
constexpr uint8_t kMaxLibrary = 32;
constexpr uint8_t kMaxDegrees = 53;    // widest table in the library (53-EDO)
constexpr uint8_t kFavoriteSlots = 4;  // the four hot favourites, one per voice button
constexpr uint8_t kHotSlots = kFavoriteSlots;
constexpr uint8_t kEmptySlot = 0xFF;   // "no tuning" in a favourite slot
constexpr uint8_t kDefaultTuningId = 0; // 12-EDO, the sound the firmware always had
constexpr float kDefaultA4Hz = 440.0f;
constexpr float kMinA4Hz = 415.0f; // baroque pitch
constexpr float kMaxA4Hz = 466.0f; // a semitone above 440
constexpr size_t kMaxNameLength = 21; // OLED characters per line at text size 1

// Hz of the scale root at tonic C with A4 = 440: MIDI note 48, the firmware's base pitch.
constexpr int kRootMidiNote = 48;
constexpr int kA4MidiNote = 69;
// The frequency clamp the old 128-entry MIDI table applied implicitly (notes 0..127).
constexpr float kMinHz = 8.175799f;
constexpr float kMaxHz = 12543.854f;

enum class Family : uint8_t { Equal = 0, Just, Temperament, Indian, Xeno };

// How the OLED names a pitch. Western prints the nearest 12-EDO note plus cents, Quarter
// adds a "+" for a quarter-tone above the note, Sargam prints Sa/Re/Ga.. relative to the
// tonic, and Numeric prints octave:degree for tunings that have no familiar names.
enum class Naming : uint8_t { Western = 0, Quarter, Sargam, Numeric };

struct Ratio
{
  uint16_t num;
  uint16_t den;
};

struct Tuning
{
  uint8_t id;          // stable, persisted; see the file comment
  Family family;
  Naming naming;
  uint8_t degrees;     // notes per period, 1..kMaxDegrees
  const char *name;    // full name, at most kMaxNameLength characters
  const char *shortName; // status-line name, at most 9 characters
  const char *detail;  // one line of facts, at most kMaxNameLength characters
  float periodCents;   // 1200 for octave tunings; 1901.955 for a Bohlen-Pierce tritave
  const float *cents;  // degrees entries, cents[0] == 0, strictly ascending, < periodCents
  const Ratio *ratios; // exact ratios for just tunings, null otherwise (documentation/tests)
  // Optional 12-entry map from a classic semitone slot (0..11) to a degree of the period.
  // Null means "nearest degree by cents", which is what an equal tuning wants; the Indian
  // tunings use it so komal Re lands on 16/15 rather than whichever shruti is nearest.
  const uint8_t *chroma;
};

// The pitch world a voice plays in. Built on Core 0, copied into the voice's control
// update, and only ever read on Core 1.
struct Selection
{
  uint8_t tuningId = kDefaultTuningId;
  uint8_t tonic = 0;          // semitones above C, 0..11: where 1/1 (Sa) sits
  float a4Hz = kDefaultA4Hz;  // reference pitch of the 12-EDO grid the note names use
};

inline bool operator==(const Selection &a, const Selection &b) noexcept
{
  return a.tuningId == b.tuningId && a.tonic == b.tonic && a.a4Hz == b.a4Hz;
}
inline bool operator!=(const Selection &a, const Selection &b) noexcept { return !(a == b); }

struct PitchWorld
{
  const Tuning *tuning = nullptr; // null behaves as the default 12-EDO
  float rootHz = 130.81278f;      // frequency of degree 0
  uint8_t tonic = 0;
  // True when the world is exactly the historical one (12-EDO, tonic C, A4 = 440): the
  // voice then keeps its old MIDI-table lookup, so nothing that already sounded moves.
  bool standard = true;
};

// --- Library (TuningLibrary.cpp) ---------------------------------------------------

size_t libraryCount() noexcept;
const Tuning &libraryAt(size_t index) noexcept; // index clamped to the last entry
const Tuning *find(uint8_t id) noexcept;         // null for an unknown id
const Tuning &resolve(uint8_t id) noexcept;      // unknown id -> the default 12-EDO

// Position of a tuning in the library (its pad on the Tuning page), or -1 for an unknown id.
int libraryIndexOf(uint8_t id) noexcept;

size_t familySize(Family family) noexcept;
const Tuning *familyMember(Family family, size_t index) noexcept; // null past the end
const char *familyName(Family family) noexcept;      // "Just Intonation"
const char *familyShortName(Family family) noexcept; // "JUST"
// Step through the whole library in pad order and wrap. An unknown current id enters at the
// first tuning going forward and the last going back. Returns an id.
uint8_t stepInLibrary(uint8_t currentId, int steps) noexcept;

// --- Pitch maths (Tuning.cpp) ------------------------------------------------------

int floorDiv(int value, int divisor) noexcept; // divisor > 0; rounds toward -infinity
// Cents above degree 0 of ladder degree `degree` (negative degrees are allowed).
float degreeCents(const Tuning &tuning, int degree) noexcept;
// Ladder degree a classic semitone slot (a 12-EDO scale-row value) plays in this tuning.
int slotToDegree(const Tuning &tuning, int slot) noexcept;
// Frequency of the tonic: C3 transposed by `tonic` semitones, on an A4 = `a4Hz` grid.
float rootHz(const Selection &selection) noexcept;
bool isStandard(const Selection &selection) noexcept;
PitchWorld makeWorld(const Selection &selection) noexcept;
// Scale-row value -> ladder degree. A "native" scale row already holds degrees (the All
// Degrees scale); every other row holds 12-EDO semitone slots.
int rowValueToDegree(const PitchWorld &world, int rowValue, bool nativeScale) noexcept;
// Hz of a degree shifted by octaveSemitones (multiples of 12), clamped to kMinHz..kMaxHz.
float frequencyHz(const PitchWorld &world, int degree, int octaveSemitones) noexcept;

// --- Names (Tuning.cpp) ------------------------------------------------------------

// "E3-14c", "C+3", "Ga3" or "3:07" per the tuning's Naming. Always terminates `out`.
// `extraCents` is a fine detune on top of the degree (an oscillator's detune); the Western
// and Quarter names show it as cents, Sargam and Numeric names ignore it.
void noteName(const PitchWorld &world, int degree, int octaveSemitones, char *out,
              size_t size, float extraCents = 0.0f) noexcept;
const char *tonicName(uint8_t tonic) noexcept; // "C".."B", clamped
// "440.0Hz" style; always terminates `out`.
void formatA4(float a4Hz, char *out, size_t size) noexcept;

// --- Organisation: selection history and favourites (Tuning.cpp) -----------------------

constexpr uint8_t kNoScale = 0xFF; // "no scale remembered" in Bank::lastScale

// What the performer organised. The favourites and the A/B partner are saved with the song
// beside the Selection; lastScale is working memory only (TuningScales.h uses it to bring
// back the scale you had in a tuning when you return to it) and starts empty every boot.
struct Bank
{
  uint8_t favorites[kFavoriteSlots];
  uint8_t previousId; // the tuning before the current one, for the A/B swap
  uint8_t lastScale[kMaxLibrary]; // by library index; kNoScale when none
};

// The four voice buttons hold 12-EDO, 24-EDO, 5-Limit JI and 22 Shruti on a fresh unit.
void defaultBank(Bank &bank) noexcept;
bool storeFavorite(Bank &bank, uint8_t slot, uint8_t tuningId) noexcept;
bool clearFavorite(Bank &bank, uint8_t slot) noexcept;
uint8_t favoriteAt(const Bank &bank, uint8_t slot) noexcept; // id or kEmptySlot
// Every favourite empty or a known id, and a known previousId.
bool bankValid(const Bank &bank) noexcept;
// Make `tuningId` current, remembering the old one for A/B. False (nothing changed) when
// the id is unknown or already current.
bool applyTuning(Selection &selection, Bank &bank, uint8_t tuningId) noexcept;
// Swap the current and previous tunings. False when there is nothing different to swap to.
bool swapWithPrevious(Selection &selection, Bank &bank) noexcept;

namespace detail {
// Compile-time cents so tables can be written as the ratios people actually quote. No
// std::log2 in a constant expression is portable, so this is an atanh series: exact to
// far better than the float the tables store.
constexpr double kLn2 = 0.6931471805599453;
constexpr double ln(double x) noexcept
{
  int k = 0;
  while (x >= 1.5)
  {
    x *= 0.5;
    ++k;
  }
  while (x < 0.75)
  {
    x *= 2.0;
    --k;
  }
  const double z = (x - 1.0) / (x + 1.0);
  const double z2 = z * z;
  double term = z;
  double sum = 0.0;
  for (int i = 1; i < 60; i += 2)
  {
    sum += term / i;
    term *= z2;
  }
  return 2.0 * sum + k * kLn2;
}
constexpr double centsOf(unsigned num, unsigned den) noexcept
{
  return 1200.0 * ln(static_cast<double>(num) / static_cast<double>(den)) / kLn2;
}
} // namespace detail

} // namespace tuning
