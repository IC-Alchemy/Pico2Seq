#include "Tuning.h"

// The tuning library: 29 tunings in five families. Everything is constexpr, so it lives
// in flash and the audio core can read it without a lock.
//
// How to add one: write its cents (or the ratios people quote and let fromRatios() do
// the logarithms), add a Tuning row with the next free id in its family (id = family * 32
// + index, keep rows in id order) and bump the counts in tests/unit/test_tuning.cpp. Never
// renumber an existing id: saved songs and favourites store it.
namespace tuning
{
namespace
{
using detail::centsOf;

template <size_t N>
struct Cents
{
  float v[N];
  constexpr Cents() noexcept : v{} {}
};

template <size_t N>
struct RatioTable
{
  Ratio r[N];
  constexpr RatioTable() noexcept : r{} {}
};

// N equal steps of a period (1200 c unless stated), the way every EDO is defined.
template <size_t N>
constexpr Cents<N> equalSteps(double periodCents) noexcept
{
  Cents<N> t;
  for (size_t i = 0; i < N; ++i)
    t.v[i] = static_cast<float>(periodCents * static_cast<double>(i) / static_cast<double>(N));
  return t;
}

// Cents of a ratio list. The list holds the degrees of one period only: the closing 2/1
// (or 3/1) is the period, not a degree.
template <size_t N>
constexpr Cents<N> fromRatios(const Ratio (&ratios)[N]) noexcept
{
  Cents<N> t;
  for (size_t i = 0; i < N; ++i)
    t.v[i] = static_cast<float>(centsOf(ratios[i].num, ratios[i].den));
  return t;
}

// --- 12-note temperaments, built from the size of each fifth around the circle ---------
// Order of the circle: C G D A E B F# C# G# D# A# F, so fifths[i] is the interval from
// circle note i to note i+1 and the twelve of them add up to seven octaves (8400 c).
struct Fifths
{
  double f[12];
};

constexpr Fifths allFifths(double value) noexcept
{
  Fifths x{};
  for (int i = 0; i < 12; ++i)
    x.f[i] = value;
  return x;
}
constexpr Fifths withFifth(Fifths x, int index, double value) noexcept
{
  x.f[index] = value;
  return x;
}
// Make fifth `index` whatever closes the circle: the wolf of meantone, or the small
// remainder Kirnberger III leaves on F#-C#.
constexpr Fifths closeCircleWith(Fifths x, int index) noexcept
{
  double others = 0.0;
  for (int i = 0; i < 12; ++i)
    if (i != index)
      others += x.f[i];
  x.f[index] = 8400.0 - others;
  return x;
}

constexpr Cents<12> chainOfFifths(const Fifths &x) noexcept
{
  constexpr int kCircle[12] = {0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5}; // pitch classes
  Cents<12> t;
  double run = 0.0;
  for (int i = 0; i < 11; ++i)
  {
    run += x.f[i];
    double c = run;
    while (c >= 1200.0)
      c -= 1200.0;
    t.v[kCircle[i + 1]] = static_cast<float>(c);
  }
  return t;
}

constexpr double kPureFifth = centsOf(3, 2);          // 701.955 c
constexpr double kSyntonicComma = centsOf(81, 80);    // 21.506 c
constexpr double kPythagoreanComma = 12.0 * kPureFifth - 8400.0; // 23.460 c
constexpr int kWolfIndex = 8; // circle index of the G#-D# (Ab-Eb) fifth, the conventional wolf

// --- EDO tables ---------------------------------------------------------------------
constexpr Cents<12> kEdo12 = equalSteps<12>(1200.0);
constexpr Cents<24> kEdo24 = equalSteps<24>(1200.0);
constexpr Cents<19> kEdo19 = equalSteps<19>(1200.0);
constexpr Cents<31> kEdo31 = equalSteps<31>(1200.0);
constexpr Cents<22> kEdo22 = equalSteps<22>(1200.0);
constexpr Cents<17> kEdo17 = equalSteps<17>(1200.0);
constexpr Cents<15> kEdo15 = equalSteps<15>(1200.0);
constexpr Cents<10> kEdo10 = equalSteps<10>(1200.0);
constexpr Cents<7> kEdo7 = equalSteps<7>(1200.0);
constexpr Cents<5> kEdo5 = equalSteps<5>(1200.0);
constexpr Cents<41> kEdo41 = equalSteps<41>(1200.0);
constexpr Cents<53> kEdo53 = equalSteps<53>(1200.0);

// --- Just intonation ------------------------------------------------------------------
// Symmetric 5-limit ("Ptolemy's intense chromatic"): every interval mirrors around the
// octave, so 16/15 pairs with 15/8 and 9/8 with 16/9.
constexpr Ratio kJi5[] = {{1, 1}, {16, 15}, {9, 8}, {6, 5}, {5, 4}, {4, 3},
                          {45, 32}, {3, 2}, {8, 5}, {5, 3}, {16, 9}, {15, 8}};
constexpr Cents<12> kJi5Cents = fromRatios(kJi5);

constexpr Ratio kJi7[] = {{1, 1}, {16, 15}, {9, 8}, {7, 6}, {5, 4}, {4, 3},
                          {7, 5}, {3, 2}, {8, 5}, {5, 3}, {7, 4}, {15, 8}};
constexpr Cents<12> kJi7Cents = fromRatios(kJi7);

// Overtones 16..31 and their mirror, the undertones 32..17 (all 2/1-reduced).
constexpr RatioTable<16> makeOvertones() noexcept
{
  RatioTable<16> t;
  for (unsigned i = 0; i < 16; ++i)
    t.r[i] = Ratio{static_cast<uint16_t>(16 + i), 16};
  return t;
}
constexpr RatioTable<16> makeUndertones() noexcept
{
  RatioTable<16> t;
  for (unsigned i = 0; i < 16; ++i)
    t.r[i] = Ratio{32, static_cast<uint16_t>(32 - i)};
  return t;
}
constexpr RatioTable<16> kOvertones = makeOvertones();
constexpr RatioTable<16> kUndertones = makeUndertones();
constexpr Cents<16> kOvertoneCents = fromRatios(kOvertones.r);
constexpr Cents<16> kUndertoneCents = fromRatios(kUndertones.r);

// Harry Partch's 43-tone 11-limit scale (2/1 closes it and is not listed).
constexpr Ratio kPartch[] = {
    {1, 1},   {81, 80}, {33, 32}, {21, 20}, {16, 15}, {12, 11}, {11, 10}, {10, 9},
    {9, 8},   {8, 7},   {7, 6},   {32, 27}, {6, 5},   {11, 9},  {5, 4},   {14, 11},
    {9, 7},   {21, 16}, {4, 3},   {27, 20}, {11, 8},  {7, 5},   {10, 7},  {16, 11},
    {40, 27}, {3, 2},   {32, 21}, {14, 9},  {11, 7},  {8, 5},   {18, 11}, {5, 3},
    {27, 16}, {12, 7},  {7, 4},   {16, 9},  {9, 5},   {20, 11}, {11, 6},  {15, 8},
    {40, 21}, {64, 33}, {160, 81}};
constexpr Cents<43> kPartchCents = fromRatios(kPartch);

// Pythagorean: a chain of pure fifths from Eb to G#, the wolf (678 c) between G# and Eb.
constexpr Cents<12> kPythagoreanCents =
    chainOfFifths(closeCircleWith(allFifths(kPureFifth), kWolfIndex));

// --- Historical temperaments ---------------------------------------------------------
constexpr double kQuarterCommaFifth = kPureFifth - kSyntonicComma / 4.0; // 696.578 c
constexpr Cents<12> kMeantoneCents =
    chainOfFifths(closeCircleWith(allFifths(kQuarterCommaFifth), kWolfIndex));

// Werckmeister III: C-G, G-D, D-A and B-F# are a quarter Pythagorean comma narrow.
constexpr double kWerckFifth = kPureFifth - kPythagoreanComma / 4.0;
constexpr Cents<12> kWerckmeisterCents = chainOfFifths(withFifth(
    withFifth(withFifth(withFifth(allFifths(kPureFifth), 0, kWerckFifth), 1, kWerckFifth), 2,
              kWerckFifth),
    5, kWerckFifth));

// Kirnberger III: C-G, G-D, D-A and A-E a quarter syntonic comma narrow (so C-E is a
// pure 5/4), F#-C# takes the small remainder, every other fifth is pure.
constexpr Cents<12> kKirnbergerCents = chainOfFifths(closeCircleWith(
    withFifth(withFifth(withFifth(withFifth(allFifths(kPureFifth), 0, kQuarterCommaFifth), 1,
                                  kQuarterCommaFifth),
                        2, kQuarterCommaFifth),
              3, kQuarterCommaFifth),
    6));

// Vallotti: the six fifths F-C-G-D-A-E-B are a sixth of a Pythagorean comma narrow.
constexpr double kVallottiFifth = kPureFifth - kPythagoreanComma / 6.0;
constexpr Cents<12> kVallottiCents = chainOfFifths(withFifth(
    withFifth(withFifth(withFifth(withFifth(withFifth(allFifths(kPureFifth), 0, kVallottiFifth), 1,
                                            kVallottiFifth),
                                  2, kVallottiFifth),
                        3, kVallottiFifth),
              4, kVallottiFifth),
    11, kVallottiFifth));

// --- Indian classical -------------------------------------------------------------------
// The 22 shrutis (Danielou's ratios), ascending.
constexpr Ratio kShruti[] = {{1, 1},   {256, 243}, {16, 15}, {10, 9},  {9, 8},   {32, 27},
                             {6, 5},   {5, 4},     {81, 64}, {4, 3},   {27, 20}, {45, 32},
                             {729, 512}, {3, 2},   {128, 81}, {8, 5},  {5, 3},   {27, 16},
                             {16, 9},  {9, 5},     {15, 8},  {243, 128}};
constexpr Cents<22> kShrutiCents = fromRatios(kShruti);
// The 12 svaras (Sa re Re ga Ga Ma Ma# Pa dha Dha ni Ni) sit on these shrutis. They are
// exactly the 12 Svara JI ratios below, so a thaat sounds the same in both tunings and
// the shrutis in between are the microtonal shading on top of it.
constexpr uint8_t kShrutiChroma[12] = {0, 2, 4, 6, 7, 9, 11, 13, 15, 16, 19, 20};

// Hindustani just intonation: komal Re 16/15, komal Ga 6/5, tivra Ma 45/32, komal Ni 9/5.
constexpr Ratio kSvaraJi[] = {{1, 1}, {16, 15}, {9, 8}, {6, 5}, {5, 4}, {4, 3},
                              {45, 32}, {3, 2}, {8, 5}, {5, 3}, {9, 5}, {15, 8}};
constexpr Cents<12> kSvaraJiCents = fromRatios(kSvaraJi);

// The 3-limit reading: 256/243 komal Re, 32/27 komal Ga, 729/512 tivra Ma, 16/9 komal Ni.
constexpr Ratio kSvaraPyth[] = {{1, 1}, {256, 243}, {9, 8}, {32, 27}, {81, 64}, {4, 3},
                                {729, 512}, {3, 2}, {128, 81}, {27, 16}, {16, 9}, {243, 128}};
constexpr Cents<12> kSvaraPythCents = fromRatios(kSvaraPyth);

// --- Xenharmonic -------------------------------------------------------------------------
constexpr double kTritave = centsOf(3, 1); // 1901.955 c
constexpr Cents<13> kBohlenPierceCents = equalSteps<13>(kTritave);
// Wendy Carlos's equal steps that are not an octave fraction: one degree per period, so
// every Note step is one 78 / 63.8 / 35.1 cent step up the ladder.
constexpr Cents<1> kCarlosStep = equalSteps<1>(1.0);

// Rows in id order (id = family * 32 + index within the family).
// Family::Equal ids 0-31, Just 32-63, Temperament 64-95, Indian 96-127, Xeno 128-159.
constexpr Tuning kLibrary[] = {
    // --- Equal divisions of the octave ---
    {0, Family::Equal, Naming::Western, 12, "12-EDO (Standard)", "12-EDO", "12/oct  100.0c step",
     1200.0f, kEdo12.v, nullptr, nullptr},
    {1, Family::Equal, Naming::Quarter, 24, "24-EDO Quarter-Tone", "24-EDO", "24/oct   50.0c step",
     1200.0f, kEdo24.v, nullptr, nullptr},
    {2, Family::Equal, Naming::Numeric, 19, "19-EDO", "19-EDO", "19/oct   63.2c step", 1200.0f,
     kEdo19.v, nullptr, nullptr},
    {3, Family::Equal, Naming::Numeric, 31, "31-EDO", "31-EDO", "31/oct   38.7c step", 1200.0f,
     kEdo31.v, nullptr, nullptr},
    {4, Family::Equal, Naming::Numeric, 22, "22-EDO", "22-EDO", "22/oct   54.5c step", 1200.0f,
     kEdo22.v, nullptr, nullptr},
    {5, Family::Equal, Naming::Numeric, 17, "17-EDO", "17-EDO", "17/oct   70.6c step", 1200.0f,
     kEdo17.v, nullptr, nullptr},
    {6, Family::Equal, Naming::Numeric, 15, "15-EDO", "15-EDO", "15/oct   80.0c step", 1200.0f,
     kEdo15.v, nullptr, nullptr},
    {7, Family::Equal, Naming::Numeric, 10, "10-EDO", "10-EDO", "10/oct  120.0c step", 1200.0f,
     kEdo10.v, nullptr, nullptr},
    {8, Family::Equal, Naming::Numeric, 7, "7-EDO", "7-EDO", "7/oct  171.4c step", 1200.0f,
     kEdo7.v, nullptr, nullptr},
    {9, Family::Equal, Naming::Numeric, 5, "5-EDO (Slendro)", "5-EDO", "5/oct  240.0c step",
     1200.0f, kEdo5.v, nullptr, nullptr},
    {10, Family::Equal, Naming::Numeric, 41, "41-EDO", "41-EDO", "41/oct   29.3c step", 1200.0f,
     kEdo41.v, nullptr, nullptr},
    {11, Family::Equal, Naming::Numeric, 53, "53-EDO", "53-EDO", "53/oct   22.6c step", 1200.0f,
     kEdo53.v, nullptr, nullptr},

    // --- Just intonation ---
    {32, Family::Just, Naming::Western, 12, "5-Limit JI", "5-Lim JI", "12 notes  Ptolemy",
     1200.0f, kJi5Cents.v, kJi5, nullptr},
    {33, Family::Just, Naming::Western, 12, "7-Limit JI", "7-Lim JI", "12 notes  7/6 7/5 7/4",
     1200.0f, kJi7Cents.v, kJi7, nullptr},
    {34, Family::Just, Naming::Western, 12, "Pythagorean", "Pythag", "12 notes  3-limit",
     1200.0f, kPythagoreanCents.v, nullptr, nullptr},
    {35, Family::Just, Naming::Numeric, 16, "Overtone 16-31", "Overtone", "16 harmonics 16-31",
     1200.0f, kOvertoneCents.v, kOvertones.r, nullptr},
    {36, Family::Just, Naming::Numeric, 16, "Undertone 32-17", "Undertone", "16 subharmonics",
     1200.0f, kUndertoneCents.v, kUndertones.r, nullptr},
    {37, Family::Just, Naming::Numeric, 43, "Partch 43-Tone", "Partch 43", "43 notes  11-limit",
     1200.0f, kPartchCents.v, kPartch, nullptr},

    // --- Historical temperaments ---
    {64, Family::Temperament, Naming::Western, 12, "1/4-Comma Meantone", "Meantone",
     "12 notes  wolf G#-Eb", 1200.0f, kMeantoneCents.v, nullptr, nullptr},
    {65, Family::Temperament, Naming::Western, 12, "Werckmeister III", "Werck III",
     "12 notes  no wolf", 1200.0f, kWerckmeisterCents.v, nullptr, nullptr},
    {66, Family::Temperament, Naming::Western, 12, "Kirnberger III", "Kirnb III",
     "12 notes  pure C-E", 1200.0f, kKirnbergerCents.v, nullptr, nullptr},
    {67, Family::Temperament, Naming::Western, 12, "Vallotti", "Vallotti", "12 notes  no wolf",
     1200.0f, kVallottiCents.v, nullptr, nullptr},

    // --- Indian classical (Hindustani) ---
    {96, Family::Indian, Naming::Sargam, 22, "22 Shruti (Danielou)", "22 Shruti",
     "22 notes  Sargam", 1200.0f, kShrutiCents.v, kShruti, kShrutiChroma},
    {97, Family::Indian, Naming::Sargam, 12, "12 Svara JI", "12 Svara", "12 notes  Hindustani",
     1200.0f, kSvaraJiCents.v, kSvaraJi, nullptr},
    {98, Family::Indian, Naming::Sargam, 12, "Pythagorean Svara", "Pyth Svr", "12 notes  3-limit",
     1200.0f, kSvaraPythCents.v, kSvaraPyth, nullptr},

    // --- Xenharmonic ---
    {128, Family::Xeno, Naming::Numeric, 13, "Bohlen-Pierce 13", "B-Pierce",
     "13 per 3:1  146.3c", static_cast<float>(kTritave), kBohlenPierceCents.v, nullptr, nullptr},
    {129, Family::Xeno, Naming::Numeric, 1, "Carlos Alpha", "C Alpha", "78.0c steps", 78.0f,
     kCarlosStep.v, nullptr, nullptr},
    {130, Family::Xeno, Naming::Numeric, 1, "Carlos Beta", "C Beta", "63.8c steps", 63.8f,
     kCarlosStep.v, nullptr, nullptr},
    {131, Family::Xeno, Naming::Numeric, 1, "Carlos Gamma", "C Gamma", "35.1c steps", 35.1f,
     kCarlosStep.v, nullptr, nullptr},
};

constexpr size_t kLibrarySize = sizeof(kLibrary) / sizeof(kLibrary[0]);
static_assert(kLibrarySize <= kMaxLibrary, "one pad per tuning on the Tuning page");

constexpr const char *kFamilyNames[kFamilyCount] = {"Equal Divisions", "Just Intonation",
                                                    "Temperaments", "Indian Classical",
                                                    "Xenharmonic"};
constexpr const char *kFamilyShortNames[kFamilyCount] = {"EDO", "JUST", "TEMPER", "INDIAN", "XENO"};
} // namespace

size_t libraryCount() noexcept { return kLibrarySize; }

const Tuning &libraryAt(size_t index) noexcept
{
  return kLibrary[index < kLibrarySize ? index : kLibrarySize - 1];
}

const Tuning *find(uint8_t id) noexcept
{
  for (size_t i = 0; i < kLibrarySize; ++i)
    if (kLibrary[i].id == id)
      return &kLibrary[i];
  return nullptr;
}

const Tuning &resolve(uint8_t id) noexcept
{
  const Tuning *t = find(id);
  return t ? *t : kLibrary[0];
}

size_t familySize(Family family) noexcept
{
  size_t n = 0;
  for (size_t i = 0; i < kLibrarySize; ++i)
    if (kLibrary[i].family == family)
      ++n;
  return n;
}

const Tuning *familyMember(Family family, size_t index) noexcept
{
  size_t seen = 0;
  for (size_t i = 0; i < kLibrarySize; ++i)
  {
    if (kLibrary[i].family != family)
      continue;
    if (seen == index)
      return &kLibrary[i];
    ++seen;
  }
  return nullptr;
}

const char *familyName(Family family) noexcept
{
  const size_t i = static_cast<size_t>(family);
  return kFamilyNames[i < kFamilyCount ? i : 0];
}

const char *familyShortName(Family family) noexcept
{
  const size_t i = static_cast<size_t>(family);
  return kFamilyShortNames[i < kFamilyCount ? i : 0];
}

int libraryIndexOf(uint8_t id) noexcept
{
  for (size_t i = 0; i < kLibrarySize; ++i)
    if (kLibrary[i].id == id)
      return static_cast<int>(i);
  return -1;
}

uint8_t stepInLibrary(uint8_t currentId, int steps) noexcept
{
  const int count = static_cast<int>(kLibrarySize);
  const int position = libraryIndexOf(currentId);
  int target;
  if (position < 0)
    target = steps >= 0 ? 0 : count - 1; // enter at the end the turn points to
  else
    target = ((position + steps) % count + count) % count;
  return kLibrary[target].id;
}

} // namespace tuning
