#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <set>
#include <string>

#include "pico2seq-core/tuning/Tuning.h"

// The tuning library and its pitch maths. The tables are checked three ways: structurally
// (ids, name lengths, ascending cents), against the ratios they were written from, and
// against published values for the historical temperaments.
using Catch::Approx;
using namespace tuning;

namespace
{
double centsOfRatio(double num, double den) { return 1200.0 * std::log2(num / den); }

std::string nameOf(const PitchWorld &world, int degree, int octave = 0)
{
  char buffer[32];
  noteName(world, degree, octave, buffer, sizeof(buffer));
  return buffer;
}

PitchWorld worldFor(uint8_t id, uint8_t tonic = 0, float a4 = 440.0f)
{
  Selection s;
  s.tuningId = id;
  s.tonic = tonic;
  s.a4Hz = a4;
  return makeWorld(s);
}

const Tuning &byId(uint8_t id)
{
  const Tuning *t = find(id);
  REQUIRE(t != nullptr);
  return *t;
}
} // namespace

TEST_CASE("constexpr cents match std::log2", "[tuning]")
{
  for (unsigned num = 1; num <= 40; ++num)
    for (unsigned den = 1; den <= 40; ++den)
      REQUIRE(detail::centsOf(num, den) ==
              Approx(centsOfRatio(num, den)).margin(1e-9));
  REQUIRE(detail::centsOf(3, 2) == Approx(701.955).margin(1e-3));
  REQUIRE(detail::centsOf(81, 80) == Approx(21.5063).margin(1e-3));
}

TEST_CASE("library structure", "[tuning]")
{
  REQUIRE(libraryCount() == 29);

  std::set<int> ids;
  std::set<std::string> names;
  for (size_t i = 0; i < libraryCount(); ++i)
  {
    const Tuning &t = libraryAt(i);
    INFO("tuning " << t.name << " id " << int(t.id));
    REQUIRE(ids.insert(t.id).second);     // ids are unique
    REQUIRE(names.insert(t.name).second); // and so are names
    // id = family * 32 + index, rows in id order so pads line up with ids.
    REQUIRE(t.id / kSlotsPerFamily == static_cast<int>(t.family));
    if (i > 0)
      REQUIRE(t.id > libraryAt(i - 1).id);

    REQUIRE(std::strlen(t.name) >= 1);
    REQUIRE(std::strlen(t.name) <= kMaxNameLength);
    REQUIRE(std::strlen(t.shortName) >= 1);
    REQUIRE(std::strlen(t.shortName) <= 9);
    REQUIRE(std::strlen(t.detail) <= kMaxNameLength);

    REQUIRE(t.degrees >= 1);
    REQUIRE(t.degrees <= kMaxDegrees);
    REQUIRE(t.periodCents > 0.0f);
    REQUIRE(t.cents[0] == 0.0f);
    for (int d = 1; d < t.degrees; ++d)
      REQUIRE(t.cents[d] > t.cents[d - 1]); // strictly ascending
    REQUIRE(t.cents[t.degrees - 1] < t.periodCents);
    if (t.degrees > 1)
    {
      // The last degree is always below the period; the gap to it is a normal step.
      REQUIRE(t.periodCents - t.cents[t.degrees - 1] > 1.0f);
    }
    if (t.chroma)
    {
      REQUIRE(t.periodCents == 1200.0f);
      REQUIRE(t.chroma[0] == 0);
      for (int pc = 1; pc < 12; ++pc)
      {
        REQUIRE(t.chroma[pc] > t.chroma[pc - 1]);
        REQUIRE(t.chroma[pc] < t.degrees);
      }
    }
  }
}

TEST_CASE("library ids and families", "[tuning]")
{
  REQUIRE(byId(kDefaultTuningId).degrees == 12);
  REQUIRE(std::strcmp(byId(kDefaultTuningId).name, "12-EDO (Standard)") == 0);
  REQUIRE(find(200) == nullptr);
  REQUIRE(&resolve(200) == &resolve(kDefaultTuningId)); // unknown ids fall back to 12-EDO
  REQUIRE(&libraryAt(9999) == &libraryAt(libraryCount() - 1));

  // The stable ids the saved-song format and the default favourites rely on.
  REQUIRE(byId(1).degrees == 24);
  REQUIRE(byId(32).naming == Naming::Western);
  REQUIRE(byId(96).degrees == 22);
  REQUIRE(byId(96).naming == Naming::Sargam);
  REQUIRE(byId(128).family == Family::Xeno);

  REQUIRE(familySize(Family::Equal) == 12);
  REQUIRE(familySize(Family::Just) == 6);
  REQUIRE(familySize(Family::Temperament) == 4);
  REQUIRE(familySize(Family::Indian) == 3);
  REQUIRE(familySize(Family::Xeno) == 4);
  size_t total = 0;
  for (uint8_t f = 0; f < kFamilyCount; ++f)
  {
    total += familySize(static_cast<Family>(f));
    REQUIRE(std::strlen(familyName(static_cast<Family>(f))) <= kMaxNameLength);
    REQUIRE(std::strlen(familyShortName(static_cast<Family>(f))) <= 6);
    // Ids are family * 32 + index, so a family can never outgrow its id range.
    REQUIRE(familySize(static_cast<Family>(f)) <= kSlotsPerFamily);
  }
  REQUIRE(total == libraryCount());
  // The Tuning page puts the whole library on the 32 pads, one tuning per pad.
  REQUIRE(libraryCount() <= kMaxLibrary);

  // Pad order: the k-th member of a family, then nothing.
  REQUIRE(familyMember(Family::Just, 0)->id == 32);
  REQUIRE(familyMember(Family::Just, 5)->id == 37);
  REQUIRE(familyMember(Family::Just, 6) == nullptr);
}

TEST_CASE("equal divisions are exact", "[tuning]")
{
  for (uint8_t id = 0; id <= 11; ++id)
  {
    const Tuning &t = byId(id);
    INFO(t.name);
    REQUIRE(t.family == Family::Equal);
    for (int d = 0; d < t.degrees; ++d)
      REQUIRE(t.cents[d] == Approx(1200.0f * d / t.degrees).margin(1e-3));
  }
  // The default is the piano: every degree a hundred cents.
  for (int d = 0; d < 12; ++d)
    REQUIRE(byId(0).cents[d] == 100.0f * d);
  for (int d = 0; d < 24; ++d)
    REQUIRE(byId(1).cents[d] == 50.0f * d);
}

TEST_CASE("just and indian tables agree with their ratios", "[tuning]")
{
  for (size_t i = 0; i < libraryCount(); ++i)
  {
    const Tuning &t = libraryAt(i);
    if (!t.ratios)
      continue;
    INFO(t.name);
    for (int d = 0; d < t.degrees; ++d)
    {
      REQUIRE(t.ratios[d].num > 0);
      REQUIRE(t.ratios[d].den > 0);
      REQUIRE(t.cents[d] ==
              Approx(centsOfRatio(t.ratios[d].num, t.ratios[d].den)).margin(2e-3));
    }
  }
  // A few well-known intervals.
  REQUIRE(byId(32).cents[7] == Approx(701.955).margin(1e-3)); // 3/2
  REQUIRE(byId(32).cents[4] == Approx(386.314).margin(1e-3)); // 5/4
  REQUIRE(byId(33).cents[3] == Approx(266.871).margin(1e-3)); // 7/6
  REQUIRE(byId(33).cents[10] == Approx(968.826).margin(1e-3)); // 7/4
}

TEST_CASE("overtone and undertone series mirror each other", "[tuning]")
{
  const Tuning &over = byId(35);
  const Tuning &under = byId(36);
  REQUIRE(over.degrees == 16);
  REQUIRE(under.degrees == 16);
  REQUIRE(over.ratios[0].num == 16);
  REQUIRE(over.ratios[15].num == 31);
  REQUIRE(under.ratios[15].den == 17);
  for (int d = 1; d < 16; ++d)
    REQUIRE(over.cents[d] + under.cents[16 - d] == Approx(1200.0).margin(1e-2));
}

TEST_CASE("Partch 43-tone scale is symmetric about the octave", "[tuning]")
{
  const Tuning &t = byId(37);
  REQUIRE(t.degrees == 43);
  for (int d = 1; d < 43; ++d)
    REQUIRE(t.cents[d] + t.cents[43 - d] == Approx(1200.0).margin(1e-2));
  REQUIRE(t.cents[1] == Approx(21.506).margin(1e-3)); // 81/80
}

TEST_CASE("historical temperaments match the published cents", "[tuning]")
{
  // Quarter-comma meantone, Eb..G# (wolf G#-Eb).
  const float meantone[12] = {0.0f,   76.049f,  193.157f, 310.265f, 386.314f, 503.422f,
                              579.471f, 696.578f, 772.627f, 889.735f, 1006.843f, 1082.892f};
  const float werck[12] = {0.0f,   90.225f,  192.180f, 294.135f, 390.225f, 498.045f,
                           588.270f, 696.090f, 792.180f, 888.270f, 996.090f, 1092.180f};
  const float kirn[12] = {0.0f,   90.225f,  193.157f, 294.135f, 386.314f, 498.045f,
                          590.224f, 696.578f, 792.180f, 889.735f, 996.090f, 1088.269f};
  const float vallotti[12] = {0.0f,   94.135f,  196.090f, 298.045f, 392.180f, 501.955f,
                              592.180f, 698.045f, 796.090f, 894.135f, 1000.000f, 1090.225f};
  const float pyth[12] = {0.0f,   113.685f, 203.910f, 294.135f, 407.820f, 498.045f,
                          611.730f, 701.955f, 815.640f, 905.865f, 996.090f, 1109.775f};
  for (int d = 0; d < 12; ++d)
  {
    INFO("degree " << d);
    REQUIRE(byId(64).cents[d] == Approx(meantone[d]).margin(2e-3));
    REQUIRE(byId(65).cents[d] == Approx(werck[d]).margin(2e-3));
    REQUIRE(byId(66).cents[d] == Approx(kirn[d]).margin(2e-3));
    REQUIRE(byId(67).cents[d] == Approx(vallotti[d]).margin(2e-3));
    REQUIRE(byId(34).cents[d] == Approx(pyth[d]).margin(2e-3));
  }
  // Kirnberger III has a pure major third above C; meantone's is the pure 5/4 too.
  REQUIRE(byId(66).cents[4] == Approx(centsOfRatio(5, 4)).margin(1e-3));
  REQUIRE(byId(64).cents[4] == Approx(centsOfRatio(5, 4)).margin(1e-3));
}

TEST_CASE("the twelve svaras sit on the 22 shrutis", "[tuning]")
{
  const Tuning &shruti = byId(96);
  const Tuning &svara = byId(97);
  REQUIRE(shruti.chroma != nullptr);
  // Each svara position in the shruti tuning is exactly the 12 Svara JI pitch, so a
  // thaat sounds the same in either and the extra shrutis are only shading.
  for (int pc = 0; pc < 12; ++pc)
    REQUIRE(shruti.cents[shruti.chroma[pc]] == Approx(svara.cents[pc]).margin(1e-3));
}

TEST_CASE("non-octave tunings carry their own period", "[tuning]")
{
  const Tuning &bp = byId(128);
  REQUIRE(bp.degrees == 13);
  REQUIRE(bp.periodCents == Approx(1901.955).margin(1e-2)); // 3/1
  REQUIRE(bp.cents[1] == Approx(1901.955 / 13.0).margin(1e-2));
  REQUIRE(degreeCents(bp, 13) == Approx(1901.955).margin(1e-2)); // one tritave up
  const Tuning &alpha = byId(129);
  REQUIRE(alpha.degrees == 1);
  REQUIRE(degreeCents(alpha, 5) == Approx(5 * 78.0).margin(1e-3));
}

TEST_CASE("degreeCents walks the ladder in both directions", "[tuning]")
{
  const Tuning &t = byId(1); // 24-EDO
  REQUIRE(degreeCents(t, 0) == 0.0f);
  REQUIRE(degreeCents(t, 3) == 150.0f);
  REQUIRE(degreeCents(t, 24) == 1200.0f);
  REQUIRE(degreeCents(t, 25) == 1250.0f);
  REQUIRE(degreeCents(t, -1) == -50.0f);
  REQUIRE(degreeCents(t, -24) == -1200.0f);
  REQUIRE(floorDiv(-1, 12) == -1);
  REQUIRE(floorDiv(-12, 12) == -1);
  REQUIRE(floorDiv(-13, 12) == -2);
  REQUIRE(floorDiv(11, 12) == 0);
  REQUIRE(floorDiv(24, 12) == 2);
}

TEST_CASE("slot mapping: twelve-note tunings are the identity", "[tuning]")
{
  for (uint8_t id : {0, 32, 33, 34, 64, 65, 66, 67, 97, 98})
    for (int slot = 0; slot <= 72; ++slot)
      REQUIRE(slotToDegree(byId(id), slot) == slot);
}

TEST_CASE("slot mapping: 24-EDO doubles the slot", "[tuning]")
{
  for (int slot = 0; slot <= 72; ++slot)
    REQUIRE(slotToDegree(byId(1), slot) == 2 * slot);
}

TEST_CASE("slot mapping: other equal tunings pick the nearest degree", "[tuning]")
{
  const Tuning &edo31 = byId(3);
  // 31-EDO's major third (4 semitones = 400 c) is 10 steps = 387 c; the fifth is 18.
  REQUIRE(slotToDegree(edo31, 0) == 0);
  REQUIRE(slotToDegree(edo31, 4) == 10);
  REQUIRE(slotToDegree(edo31, 7) == 18);
  REQUIRE(slotToDegree(edo31, 12) == 31);
  // 19-EDO: minor third 300 c -> 5 steps (315.8 c).
  REQUIRE(slotToDegree(byId(2), 3) == 5);
  // 5-EDO: 700 c -> 3 steps (720 c).
  REQUIRE(slotToDegree(byId(9), 7) == 3);
  // Every octave of slots lands a whole period up.
  for (int slot = 0; slot <= 48; ++slot)
    REQUIRE(slotToDegree(edo31, slot + 12) == slotToDegree(edo31, slot) + 31);
  // Ties go to the lower degree: 15-EDO steps are 80 c, so 40 c... use 10-EDO (120 c):
  // slot 6 = 600 c is exactly between degrees 5 (600 c) - not a tie - check degree 5.
  REQUIRE(slotToDegree(byId(7), 6) == 5);
  // 17-EDO slot 6 = 600 c, steps of 70.59: 8 -> 564.7, 9 -> 635.3; nearer is 8? |35.3| both.
  REQUIRE(slotToDegree(byId(5), 6) == 8); // 600 - 564.7 = 35.3 < 635.3 - 600 = 35.3? lower wins
}

TEST_CASE("slot mapping: shruti tuning uses the svara layout", "[tuning]")
{
  const Tuning &t = byId(96);
  const uint8_t expected[12] = {0, 2, 4, 6, 7, 9, 11, 13, 15, 16, 19, 20};
  for (int slot = 0; slot < 12; ++slot)
    REQUIRE(slotToDegree(t, slot) == expected[slot]);
  REQUIRE(slotToDegree(t, 12) == 22);
  REQUIRE(slotToDegree(t, 19) == 22 + 13);
  REQUIRE(slotToDegree(t, 24) == 44);
}

TEST_CASE("slot mapping: Bohlen-Pierce and Carlos steps use nearest by cents", "[tuning]")
{
  // 146.3 c steps: 100 c -> step 1 (146.3) vs 0: nearer is 0 (100 > 73.15) -> step 1.
  const Tuning &bp = byId(128);
  REQUIRE(slotToDegree(bp, 0) == 0);
  REQUIRE(slotToDegree(bp, 1) == 1);
  REQUIRE(slotToDegree(bp, 2) == 1); // 200 c: 146.3 is 53.7 away, 292.6 is 92.6 away
  REQUIRE(slotToDegree(bp, 19) == 13); // 1900 c is the tritave (1901.955)
  const Tuning &alpha = byId(129);
  REQUIRE(slotToDegree(alpha, 3) == 4); // 300 / 78 = 3.85 -> 4
}

TEST_CASE("root frequency follows tonic and reference", "[tuning]")
{
  Selection s;
  REQUIRE(rootHz(s) == Approx(130.8128).epsilon(1e-6)); // C3
  REQUIRE(isStandard(s));
  s.tonic = 9; // A
  REQUIRE(rootHz(s) == Approx(220.0).epsilon(1e-6));
  REQUIRE_FALSE(isStandard(s));
  s.tonic = 0;
  s.a4Hz = 432.0f;
  REQUIRE(rootHz(s) == Approx(130.8128 * 432.0 / 440.0).epsilon(1e-6));
  REQUIRE_FALSE(isStandard(s));
  s.a4Hz = 440.0f;
  s.tuningId = 32;
  REQUIRE_FALSE(isStandard(s));
  s.tuningId = 0;
  s.tonic = 200; // out of range clamps to B
  REQUIRE(rootHz(s) == Approx(130.8128 * std::pow(2.0, 11.0 / 12.0)).epsilon(1e-6));
}

TEST_CASE("12-EDO reproduces the historical MIDI table", "[tuning]")
{
  const PitchWorld world = worldFor(0);
  REQUIRE(world.standard);
  for (int slot = 0; slot <= 72; ++slot)
    for (int octave : {-24, -12, 0, 12, 24})
    {
      const int midi = 48 + slot + octave;
      if (midi < 0 || midi > 127)
        continue;
      const double expected = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
      REQUIRE(frequencyHz(world, slot, octave) == Approx(expected).epsilon(2e-6));
    }
}

TEST_CASE("frequencies follow the tuning, tonic and reference", "[tuning]")
{
  // 5-limit JI: the 3/2 above C3 is exactly 1.5 x the root.
  const PitchWorld ji = worldFor(32);
  REQUIRE(frequencyHz(ji, 7, 0) == Approx(1.5 * 130.8128).epsilon(1e-5));
  REQUIRE(frequencyHz(ji, 4, 0) == Approx(1.25 * 130.8128).epsilon(1e-5));
  REQUIRE(frequencyHz(ji, 4, 12) == Approx(2.5 * 130.8128).epsilon(1e-5));
  REQUIRE(frequencyHz(ji, 12, 0) == Approx(2.0 * 130.8128).epsilon(1e-5));

  // Sa on D: 1/1 sits on D3 (146.83 Hz) and every ratio hangs from it.
  const PitchWorld d = worldFor(97, 2);
  REQUIRE(frequencyHz(d, 0, 0) == Approx(146.8324).epsilon(1e-5));
  REQUIRE(frequencyHz(d, 7, 0) == Approx(1.5 * 146.8324).epsilon(1e-5)); // Pa

  // A4 = 432 lowers everything by the same ratio.
  const PitchWorld low = worldFor(0, 0, 432.0f);
  REQUIRE(frequencyHz(low, 9, 12) == Approx(432.0).epsilon(1e-5)); // A4
  REQUIRE_FALSE(low.standard);

  // 24-EDO with the All Degrees scale (native rows): degree 1 is a quarter tone up.
  const PitchWorld quarter = worldFor(1);
  REQUIRE(frequencyHz(quarter, 1, 0) == Approx(130.8128 * std::pow(2.0, 50.0 / 1200.0)).epsilon(1e-5));
  REQUIRE(rowValueToDegree(quarter, 5, true) == 5);
  REQUIRE(rowValueToDegree(quarter, 5, false) == 10); // a classic slot doubles
}

TEST_CASE("frequencies are clamped like the old MIDI table", "[tuning]")
{
  const PitchWorld world = worldFor(0, 11);
  REQUIRE(frequencyHz(world, 72, 24) == Approx(kMaxHz)); // far above note 127
  REQUIRE(frequencyHz(world, -200, 0) == Approx(kMinHz));
  REQUIRE(frequencyHz(world, 60, 24) <= kMaxHz);
}

TEST_CASE("Western names carry cents deviations", "[tuning]")
{
  const PitchWorld standard = worldFor(0);
  REQUIRE(nameOf(standard, 0) == "C3");
  REQUIRE(nameOf(standard, 9) == "A3");
  REQUIRE(nameOf(standard, 12) == "C4");
  REQUIRE(nameOf(standard, 0, -12) == "C2");
  REQUIRE(nameOf(standard, 1, 0) == "C#3");
  const PitchWorld ji = worldFor(32);
  REQUIRE(nameOf(ji, 4) == "E3-14c"); // 5/4 is 13.7 cents flat of equal E
  REQUIRE(nameOf(ji, 7) == "G3+2c");  // 3/2 is 2 cents sharp of equal G
  REQUIRE(nameOf(ji, 0) == "C3");
  const PitchWorld d = worldFor(0, 2);
  REQUIRE(nameOf(d, 0) == "D3"); // the tonic moves the names with it
}

TEST_CASE("Quarter-tone names mark the cell between two semitones", "[tuning]")
{
  const PitchWorld q = worldFor(1);
  REQUIRE(nameOf(q, 0) == "C3");
  REQUIRE(nameOf(q, 1) == "C+3");
  REQUIRE(nameOf(q, 2) == "C#3");
  REQUIRE(nameOf(q, 3) == "C#+3");
  REQUIRE(nameOf(q, 24) == "C4");
  REQUIRE(nameOf(q, 23) == "B+3");
}

TEST_CASE("Sargam names are relative to the tonic", "[tuning]")
{
  const PitchWorld svara = worldFor(97);
  REQUIRE(nameOf(svara, 0) == "Sa3");
  REQUIRE(nameOf(svara, 1) == "re3");
  REQUIRE(nameOf(svara, 4) == "Ga3");
  REQUIRE(nameOf(svara, 6) == "Ma#3");
  REQUIRE(nameOf(svara, 7) == "Pa3");
  REQUIRE(nameOf(svara, 11) == "Ni3");
  REQUIRE(nameOf(svara, 12) == "Sa4");
  // Movable Sa: the name does not change with the key.
  REQUIRE(nameOf(worldFor(97, 5), 7) == "Pa3");

  // 22 shrutis: svara positions are named, the shrutis between them get a "+".
  const PitchWorld shruti = worldFor(96);
  REQUIRE(nameOf(shruti, 0) == "Sa3");
  REQUIRE(nameOf(shruti, 1) == "Sa+3");
  REQUIRE(nameOf(shruti, 2) == "re3");
  REQUIRE(nameOf(shruti, 3) == "re+3");
  REQUIRE(nameOf(shruti, 4) == "Re3");
  REQUIRE(nameOf(shruti, 13) == "Pa3");
  REQUIRE(nameOf(shruti, 21) == "Ni+3");
  REQUIRE(nameOf(shruti, 22) == "Sa4");
  REQUIRE(nameOf(shruti, 0, 12) == "Sa4"); // octave parameter raises the number
}

TEST_CASE("Numeric names give octave and degree", "[tuning]")
{
  const PitchWorld edo19 = worldFor(2);
  REQUIRE(nameOf(edo19, 0) == "3:00");
  REQUIRE(nameOf(edo19, 7) == "3:07");
  REQUIRE(nameOf(edo19, 19) == "4:00");
  REQUIRE(nameOf(edo19, 0, -12) == "2:00");
  REQUIRE(nameOf(worldFor(129), 5) == "8:00"); // Carlos: one degree per period
}

TEST_CASE("noteName always terminates and never overflows", "[tuning]")
{
  char tiny[3];
  noteName(worldFor(32), 4, 0, tiny, sizeof(tiny));
  REQUIRE(std::strlen(tiny) <= 2);
  char one[1] = {'x'};
  noteName(worldFor(0), 0, 0, one, sizeof(one));
  REQUIRE(one[0] == '\0');
  noteName(worldFor(0), 0, 0, nullptr, 8); // must not crash
  noteName(worldFor(0), 0, 0, tiny, 0);
}

TEST_CASE("tonic and reference formatting", "[tuning]")
{
  REQUIRE(std::string(tonicName(0)) == "C");
  REQUIRE(std::string(tonicName(1)) == "C#");
  REQUIRE(std::string(tonicName(11)) == "B");
  REQUIRE(std::string(tonicName(99)) == "B");
  char buf[16];
  formatA4(440.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "440.0Hz");
  formatA4(432.5f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "432.5Hz");
}

TEST_CASE("library order gives every tuning exactly one pad", "[tuning]")
{
  for (size_t i = 0; i < libraryCount(); ++i)
    REQUIRE(libraryIndexOf(libraryAt(i).id) == static_cast<int>(i));
  REQUIRE(libraryIndexOf(200) == -1);
  REQUIRE(libraryIndexOf(kEmptySlot) == -1);

  // Family-grouped: walking the library never returns to a family it has left, so each
  // family is one contiguous run of pads (one colour on the LED matrix).
  std::set<int> left;
  int current = -1;
  for (size_t i = 0; i < libraryCount(); ++i)
  {
    const int family = static_cast<int>(libraryAt(i).family);
    if (family != current)
    {
      REQUIRE(left.count(family) == 0);
      if (current >= 0)
        left.insert(current);
      current = family;
    }
  }
}

TEST_CASE("stepInLibrary walks the whole library and wraps", "[tuning]")
{
  REQUIRE(stepInLibrary(0, 1) == 1);
  REQUIRE(stepInLibrary(0, 0) == 0);
  REQUIRE(stepInLibrary(11, 1) == 32);  // last equal division -> first just tuning
  REQUIRE(stepInLibrary(32, -1) == 11); // and back
  REQUIRE(stepInLibrary(98, 1) == 128); // Indian -> Xeno
  REQUIRE(stepInLibrary(0, -1) == 131); // wraps to the last tuning
  REQUIRE(stepInLibrary(131, 1) == 0);
  const int count = static_cast<int>(libraryCount());
  REQUIRE(stepInLibrary(0, count) == 0);
  REQUIRE(stepInLibrary(0, -count - 1) == 131);
  REQUIRE(stepInLibrary(5, 3 * count + 2) == stepInLibrary(5, 2));
  // An unknown id enters at the end the turn points to.
  REQUIRE(stepInLibrary(200, 1) == 0);
  REQUIRE(stepInLibrary(200, -1) == 131);
}

TEST_CASE("favourites: defaults, store, clear, validation", "[tuning]")
{
  static_assert(kFavoriteSlots == 4, "one hot favourite per voice button");
  Bank bank;
  defaultBank(bank);
  REQUIRE(favoriteAt(bank, 0) == 0);
  REQUIRE(favoriteAt(bank, 1) == 1);
  REQUIRE(favoriteAt(bank, 2) == 32);
  REQUIRE(favoriteAt(bank, 3) == 96);
  REQUIRE(bank.previousId == kDefaultTuningId);
  for (uint8_t i = 0; i < kMaxLibrary; ++i)
    REQUIRE(bank.lastScale[i] == kNoScale); // working memory starts empty
  REQUIRE(bankValid(bank));

  REQUIRE(storeFavorite(bank, 3, 64));
  REQUIRE(favoriteAt(bank, 3) == 64);
  REQUIRE_FALSE(storeFavorite(bank, 3, 200)); // unknown id refused, slot unchanged
  REQUIRE(favoriteAt(bank, 3) == 64);
  REQUIRE_FALSE(storeFavorite(bank, kFavoriteSlots, 0)); // out of range
  REQUIRE(favoriteAt(bank, kFavoriteSlots) == kEmptySlot);
  REQUIRE(clearFavorite(bank, 3));
  REQUIRE(favoriteAt(bank, 3) == kEmptySlot);
  REQUIRE_FALSE(clearFavorite(bank, 40));

  bank.favorites[1] = 77; // not a tuning id
  REQUIRE_FALSE(bankValid(bank));
  bank.favorites[1] = kEmptySlot;
  REQUIRE(bankValid(bank)); // an empty slot is fine
  bank.previousId = 250;
  REQUIRE_FALSE(bankValid(bank));
}

TEST_CASE("applyTuning remembers the previous tuning for A/B", "[tuning]")
{
  Selection s;
  Bank bank;
  defaultBank(bank);
  REQUIRE(applyTuning(s, bank, 32));
  REQUIRE(s.tuningId == 32);
  REQUIRE(bank.previousId == 0);
  REQUIRE_FALSE(applyTuning(s, bank, 32));   // already current: no history change
  REQUIRE(bank.previousId == 0);
  REQUIRE_FALSE(applyTuning(s, bank, 200));  // unknown
  REQUIRE(s.tuningId == 32);

  REQUIRE(swapWithPrevious(s, bank));
  REQUIRE(s.tuningId == 0);
  REQUIRE(bank.previousId == 32);
  REQUIRE(swapWithPrevious(s, bank)); // and back
  REQUIRE(s.tuningId == 32);
  REQUIRE(bank.previousId == 0);

  Selection fresh;
  Bank empty;
  defaultBank(empty);
  REQUIRE_FALSE(swapWithPrevious(fresh, empty)); // previous == current: nothing to swap
}

TEST_CASE("noteName: extra detune shows as cents in Western and Quarter names only", "[tuning]")
{
  char buffer[32];
  const PitchWorld west = worldFor(0);
  noteName(west, 0, 0, buffer, sizeof(buffer), 7.0f);
  REQUIRE(std::string(buffer) == "C3+7c");
  noteName(west, 0, 0, buffer, sizeof(buffer), -12.0f);
  REQUIRE(std::string(buffer) == "C3-12c");
  noteName(west, 0, 0, buffer, sizeof(buffer), 0.0f);
  REQUIRE(std::string(buffer) == "C3");
  noteName(west, 0, 0, buffer, sizeof(buffer));
  REQUIRE(std::string(buffer) == "C3");

  const PitchWorld quarter = worldFor(1);
  noteName(quarter, 1, 0, buffer, sizeof(buffer), 10.0f);
  REQUIRE(std::string(buffer) == "C+3+10c");

  const PitchWorld sargam = worldFor(97);
  noteName(sargam, 1, 0, buffer, sizeof(buffer), 30.0f);
  REQUIRE(std::string(buffer) == "re3"); // Sargam ignores the detune
  const PitchWorld numeric = worldFor(2);
  noteName(numeric, 7, 0, buffer, sizeof(buffer), 30.0f);
  REQUIRE(std::string(buffer) == "3:07");
}
