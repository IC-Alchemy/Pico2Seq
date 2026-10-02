#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <set>
#include <string>

#include "tuning/TuningScales.h"

// Which scales belong to which tuning. The rule the tests enforce is the one a musician
// would: a twelve-note mode only in a twelve-note tuning, a tuned scale only in the tuning
// whose degrees it was written in, and every tuning always has something to play.
using namespace tuning;

namespace
{
uint8_t scaleNamed(const char *name)
{
  for (size_t i = 0; i < SCALES_COUNT; ++i)
    if (std::strcmp(scaleNames[i], name) == 0)
      return static_cast<uint8_t>(i);
  FAIL("no scale named " << name);
  return 0;
}

// Degrees per period a tuned scale row was written for, from its name.
int periodOfTunedScale(size_t index)
{
  struct Prefix { const char *prefix; int period; };
  static const Prefix prefixes[] = {
      {"Maqam", 24}, {"19-EDO", 19}, {"31-EDO", 31}, {"22-EDO", 22}, {"17-EDO", 17},
      {"15-EDO", 15}, {"10-EDO", 10}, {"41-EDO", 41}, {"53-EDO", 53}, {"Overtone", 16},
      {"Partch", 43}, {"Bohlen-Pierce", 13},
  };
  for (const Prefix &p : prefixes)
    if (std::strncmp(scaleNames[index], p.prefix, std::strlen(p.prefix)) == 0)
      return p.period;
  return 0;
}

bool isTwelveNote(const Tuning &t) { return t.degrees == 12 || t.chroma != nullptr; }
} // namespace

TEST_CASE("every tuning has a set of scales that is non-empty, unique and in range", "[tuning][tuning-scales]")
{
  for (size_t i = 0; i < libraryCount(); ++i)
  {
    const Tuning &t = libraryAt(i);
    const ScaleSet set = scaleSet(t.id);
    INFO("tuning " << t.name);
    REQUIRE(set.count >= 1);
    REQUIRE(set.count <= kMaxSetScales);
    std::set<uint8_t> seen;
    for (uint8_t s = 0; s < set.count; ++s)
    {
      REQUIRE(set.scales[s] < SCALES_COUNT);
      REQUIRE(seen.insert(set.scales[s]).second);
    }
    REQUIRE(defaultScale(t.id) == set.scales[0]);
    REQUIRE(scaleAvailable(t.id, defaultScale(t.id)));
  }
}

TEST_CASE("an unknown tuning gets the twelve-note set", "[tuning][tuning-scales]")
{
  REQUIRE(scaleSet(200).scales == scaleSet(0).scales);
  REQUIRE(scaleSet(kEmptySlot).count == scaleSet(0).count);
}

TEST_CASE("a twelve-note mode is only ever offered in a twelve-note tuning", "[tuning][tuning-scales]")
{
  for (size_t i = 0; i < libraryCount(); ++i)
  {
    const Tuning &t = libraryAt(i);
    const ScaleSet set = scaleSet(t.id);
    for (uint8_t s = 0; s < set.count; ++s)
    {
      if (scaleIsNative(set.scales[s]))
        continue;
      INFO("tuning " << t.name << " scale " << scaleNames[set.scales[s]]);
      // Dorian on 24 or 31 notes per octave means nothing; on the 22 shrutis the svara layout
      // gives each semitone slot a place, so the classic rows are meaningful there.
      REQUIRE(isTwelveNote(t));
    }
  }
}

TEST_CASE("every tuning that is not twelve-note has its own scales", "[tuning][tuning-scales]")
{
  const ScaleSet westernSet = scaleSet(0);
  for (size_t i = 0; i < libraryCount(); ++i)
  {
    const Tuning &t = libraryAt(i);
    if (t.degrees == 12)
      continue;
    INFO("tuning " << t.name);
    REQUIRE(scaleSet(t.id).scales != westernSet.scales);
    REQUIRE(scaleAvailable(t.id, static_cast<uint8_t>(SCALE_ALL_DEGREES))); // every degree, always
  }
}

TEST_CASE("a tuned scale is only offered where its degrees per period match", "[tuning][tuning-scales]")
{
  for (size_t i = 0; i < libraryCount(); ++i)
  {
    const Tuning &t = libraryAt(i);
    const ScaleSet set = scaleSet(t.id);
    for (uint8_t s = 0; s < set.count; ++s)
    {
      const uint8_t index = set.scales[s];
      if (index < SCALE_FIRST_TUNED)
        continue;
      INFO("tuning " << t.name << " scale " << scaleNames[index]);
      REQUIRE(periodOfTunedScale(index) == t.degrees);
    }
  }
}

TEST_CASE("every scale is reachable from some tuning and tuned scales from exactly their own", "[tuning][tuning-scales]")
{
  for (size_t s = 0; s < SCALES_COUNT; ++s)
  {
    int offeredBy = 0;
    int edoOrSeriesOwners = 0;
    for (size_t i = 0; i < libraryCount(); ++i)
    {
      if (scaleAvailable(libraryAt(i).id, static_cast<uint8_t>(s)))
      {
        ++offeredBy;
        if (s >= SCALE_FIRST_TUNED)
          ++edoOrSeriesOwners;
      }
    }
    INFO("scale " << scaleNames[s]);
    REQUIRE(offeredBy >= 1);
    if (s >= SCALE_FIRST_TUNED)
      REQUIRE(edoOrSeriesOwners == 1); // a tuned row has one home
  }
}

TEST_CASE("the twelve-note tunings offer the modes everyone knows and the four thaats", "[tuning][tuning-scales]")
{
  for (const uint8_t id : {0, 32, 33, 34, 64, 65, 66, 67})
  {
    INFO("tuning " << int(id));
    REQUIRE(scaleAvailable(id, scaleNamed("Ionian Major")));
    REQUIRE(scaleAvailable(id, scaleNamed("Dorian")));
    REQUIRE(scaleAvailable(id, scaleNamed("Chromatic")));
    REQUIRE(scaleAvailable(id, scaleNamed("Bhairav Thaat")));
    REQUIRE(scaleAvailable(id, scaleNamed("Todi Thaat")));
    REQUIRE(defaultScale(id) == scaleNamed("Ionian Major"));
  }
}

TEST_CASE("the Indian tunings lead with the thaats", "[tuning][tuning-scales]")
{
  for (const uint8_t id : {96, 97, 98})
  {
    INFO("tuning " << int(id));
    REQUIRE(defaultScale(id) == scaleNamed("Bhairav Thaat"));
    REQUIRE(scaleAtSlot(id, 1) == scaleNamed("Marwa Thaat"));
    REQUIRE(scaleAtSlot(id, 2) == scaleNamed("Poorvi Thaat"));
    REQUIRE(scaleAtSlot(id, 3) == scaleNamed("Todi Thaat"));
  }
  REQUIRE(scaleAvailable(96, scaleNamed("All Degrees"))); // all 22 shrutis
  REQUIRE_FALSE(scaleAvailable(97, scaleNamed("All Degrees")));
}

TEST_CASE("the quarter-tone tuning offers maqams, then every quarter-tone", "[tuning][tuning-scales]")
{
  const ScaleSet set = scaleSet(1);
  REQUIRE(set.count == 5);
  REQUIRE(set.scales[0] == scaleNamed("Maqam Rast"));
  REQUIRE(set.scales[1] == scaleNamed("Maqam Bayati"));
  REQUIRE(set.scales[2] == scaleNamed("Maqam Hijaz"));
  REQUIRE(set.scales[3] == scaleNamed("Maqam Saba"));
  REQUIRE(set.scales[4] == scaleNamed("All Degrees"));
  REQUIRE_FALSE(scaleAvailable(1, scaleNamed("Dorian")));
}

TEST_CASE("slots and scales map both ways", "[tuning][tuning-scales]")
{
  for (size_t i = 0; i < libraryCount(); ++i)
  {
    const uint8_t id = libraryAt(i).id;
    const ScaleSet set = scaleSet(id);
    for (uint8_t slot = 0; slot < set.count; ++slot)
    {
      REQUIRE(scaleSlot(id, set.scales[slot]) == slot);
      REQUIRE(scaleAtSlot(id, slot) == set.scales[slot]);
    }
    REQUIRE(scaleAtSlot(id, -5) == set.scales[0]);
    REQUIRE(scaleAtSlot(id, 500) == set.scales[set.count - 1]);
  }
  REQUIRE(scaleSlot(1, scaleNamed("Dorian")) == -1);
  REQUIRE(scaleSlot(1, 250) == -1);
  REQUIRE_FALSE(scaleAvailable(1, scaleNamed("Dorian")));
}

TEST_CASE("stepScale walks the tuning's own scales and wraps", "[tuning][tuning-scales]")
{
  REQUIRE(stepScale(1, scaleNamed("Maqam Rast"), 1) == scaleNamed("Maqam Bayati"));
  REQUIRE(stepScale(1, scaleNamed("All Degrees"), 1) == scaleNamed("Maqam Rast")); // wraps
  REQUIRE(stepScale(1, scaleNamed("Maqam Rast"), -1) == scaleNamed("All Degrees"));
  REQUIRE(stepScale(1, scaleNamed("Maqam Rast"), 5) == scaleNamed("Maqam Rast"));
  REQUIRE(stepScale(1, scaleNamed("Maqam Rast"), -11) == scaleNamed("All Degrees"));
  REQUIRE(stepScale(0, scaleNamed("Ionian Major"), 1) == scaleNamed("Dorian"));
  REQUIRE(stepScale(0, scaleNamed("Todi Thaat"), 1) == scaleNamed("Ionian Major"));
  // A scale the tuning does not offer: forward enters at the first, backward at the last.
  REQUIRE(stepScale(1, scaleNamed("Ionian Major"), 1) == scaleNamed("Maqam Rast"));
  REQUIRE(stepScale(1, scaleNamed("Ionian Major"), -1) == scaleNamed("All Degrees"));
  REQUIRE(stepScale(1, scaleNamed("Ionian Major"), 0) == scaleNamed("Maqam Rast"));
  // Stepping through every scale of every tuning visits each exactly once.
  for (size_t i = 0; i < libraryCount(); ++i)
  {
    const uint8_t id = libraryAt(i).id;
    std::set<uint8_t> seen;
    uint8_t scale = defaultScale(id);
    for (uint8_t n = 0; n < scaleSet(id).count; ++n)
    {
      REQUIRE(seen.insert(scale).second);
      scale = stepScale(id, scale, 1);
    }
    REQUIRE(scale == defaultScale(id));
  }
}

TEST_CASE("coerceScale keeps a scale the tuning offers and replaces any other", "[tuning][tuning-scales]")
{
  REQUIRE(coerceScale(0, scaleNamed("Dorian")) == scaleNamed("Dorian"));
  REQUIRE(coerceScale(1, scaleNamed("Maqam Hijaz")) == scaleNamed("Maqam Hijaz"));
  REQUIRE(coerceScale(1, scaleNamed("Dorian")) == scaleNamed("Maqam Rast"));
  REQUIRE(coerceScale(0, scaleNamed("All Degrees")) == scaleNamed("Ionian Major")); // not offered in 12 notes
  REQUIRE(coerceScale(1, 200) == scaleNamed("Maqam Rast"));
  REQUIRE(coerceScale(96, scaleNamed("Maqam Rast")) == scaleNamed("Bhairav Thaat"));
  REQUIRE(coerceScale(200, scaleNamed("Dorian")) == scaleNamed("Dorian")); // unknown tuning: twelve-note
}

TEST_CASE("scalePeriodDegrees counts a classic row in twelve and a native row in the tuning's degrees",
          "[tuning][tuning-scales]")
{
  REQUIRE(scalePeriodDegrees(0, scaleNamed("Dorian")) == 12);
  REQUIRE(scalePeriodDegrees(96, scaleNamed("Bhairav Thaat")) == 12); // semitone slots, mapped to shrutis
  REQUIRE(scalePeriodDegrees(96, scaleNamed("All Degrees")) == 22);
  REQUIRE(scalePeriodDegrees(1, scaleNamed("All Degrees")) == 24);
  REQUIRE(scalePeriodDegrees(1, scaleNamed("Maqam Rast")) == 24);
  REQUIRE(scalePeriodDegrees(128, scaleNamed("All Degrees")) == 13);
  REQUIRE(scalePeriodDegrees(3, scaleNamed("31-EDO Major")) == 31);
}

TEST_CASE("the heptatonic scales read as seven notes per octave for the arpeggiator", "[tuning][tuning-scales]")
{
  // The arpeggiator lays a seven-note scale out one octave per pad row; that has to hold in
  // every tuning, counted in that tuning's own degrees.
  REQUIRE(scaleNotesPerPeriod(scale[scaleNamed("Ionian Major")], scalePeriodDegrees(0, scaleNamed("Ionian Major"))) == 7);
  REQUIRE(scaleNotesPerPeriod(scale[scaleNamed("Bhairav Thaat")], scalePeriodDegrees(96, scaleNamed("Bhairav Thaat"))) == 7);
  for (const char *name : {"Maqam Rast", "Maqam Bayati", "Maqam Hijaz", "Maqam Saba", "19-EDO Major", "19-EDO Minor",
                           "31-EDO Major", "31-EDO Minor", "22-EDO Major", "22-EDO Minor", "17-EDO Major",
                           "17-EDO Minor", "15-EDO Heptatonic", "41-EDO Major", "41-EDO Minor", "53-EDO Major",
                           "53-EDO Minor", "Overtone Heptatonic", "Partch Major", "Partch Minor"})
  {
    const uint8_t index = scaleNamed(name);
    INFO(name);
    REQUIRE(scaleNotesPerPeriod(scale[index], periodOfTunedScale(index)) == 7);
  }
  for (const char *name : {"19-EDO Pentatonic", "31-EDO Pentatonic", "22-EDO Pentatonic", "15-EDO Pentatonic",
                           "10-EDO Pentatonic", "41-EDO Pentatonic", "53-EDO Pentatonic", "Overtone Pentatonic"})
  {
    const uint8_t index = scaleNamed(name);
    INFO(name);
    REQUIRE(scaleNotesPerPeriod(scale[index], periodOfTunedScale(index)) == 5);
  }
}

TEST_CASE("a tuned scale holds at most six periods of its tuning", "[tuning][tuning-scales]")
{
  for (size_t i = 0; i < libraryCount(); ++i)
  {
    const Tuning &t = libraryAt(i);
    const ScaleSet set = scaleSet(t.id);
    for (uint8_t s = 0; s < set.count; ++s)
    {
      const uint8_t index = set.scales[s];
      if (index < SCALE_FIRST_TUNED)
        continue; // classic rows and All Degrees (the plain ladder) are checked in test_scales.cpp
      INFO("tuning " << t.name << " scale " << scaleNames[index]);
      for (size_t step = 0; step < SCALE_STEPS; ++step)
      {
        REQUIRE(scale[index][step] >= 0);
        REQUIRE(scale[index][step] <= 6 * t.degrees);
      }
    }
  }
}

// --- Remembering a scale per tuning --------------------------------------------------------------

TEST_CASE("scaleForTuning prefers the current scale, then the remembered one, then the default",
          "[tuning][tuning-scales]")
{
  Bank bank;
  defaultBank(bank);
  const uint8_t rast = scaleNamed("Maqam Rast");
  const uint8_t saba = scaleNamed("Maqam Saba");
  // Current scale offered: it stays.
  REQUIRE(scaleForTuning(bank, 1, saba) == saba);
  // Not offered, nothing remembered: the default.
  REQUIRE(scaleForTuning(bank, 1, scaleNamed("Dorian")) == rast);
  // Remembered for 24-EDO: that.
  bank.lastScale[libraryIndexOf(1)] = saba;
  REQUIRE(scaleForTuning(bank, 1, scaleNamed("Dorian")) == saba);
  // A corrupt memory (a scale 24-EDO does not offer) is ignored, not trusted.
  bank.lastScale[libraryIndexOf(1)] = scaleNamed("Dorian");
  REQUIRE(scaleForTuning(bank, 1, scaleNamed("Lydian")) == rast);
  bank.lastScale[libraryIndexOf(1)] = 250;
  REQUIRE(scaleForTuning(bank, 1, scaleNamed("Lydian")) == rast);
  // An unknown tuning has no memory: its twelve-note default.
  REQUIRE(scaleForTuning(bank, 200, scaleNamed("Maqam Rast")) == scaleNamed("Ionian Major"));
}

TEST_CASE("applyTuningWithScale changes nothing for an unknown or current tuning", "[tuning][tuning-scales]")
{
  Selection selection;
  Bank bank;
  defaultBank(bank);
  uint8_t scaleIndex = scaleNamed("Dorian");
  REQUIRE_FALSE(applyTuningWithScale(selection, bank, 200, scaleIndex));
  REQUIRE_FALSE(applyTuningWithScale(selection, bank, 0, scaleIndex)); // already playing it
  REQUIRE(selection.tuningId == 0);
  REQUIRE(scaleIndex == scaleNamed("Dorian"));
  REQUIRE(bank.lastScale[libraryIndexOf(0)] == kNoScale);
}

TEST_CASE("leaving a tuning remembers its scale, but never a scale it does not offer", "[tuning][tuning-scales]")
{
  Selection selection;
  Bank bank;
  defaultBank(bank);
  uint8_t scaleIndex = scaleNamed("Lydian");
  REQUIRE(applyTuningWithScale(selection, bank, 1, scaleIndex)); // 12-EDO -> 24-EDO
  REQUIRE(bank.lastScale[libraryIndexOf(0)] == scaleNamed("Lydian"));
  REQUIRE(scaleIndex == scaleNamed("Maqam Rast"));

  // Leave 24-EDO while (wrongly) holding a twelve-note scale: nothing is remembered for it.
  scaleIndex = scaleNamed("Dorian");
  REQUIRE(applyTuningWithScale(selection, bank, 0, scaleIndex));
  REQUIRE(bank.lastScale[libraryIndexOf(1)] == kNoScale);
  REQUIRE(scaleIndex == scaleNamed("Dorian"));
}

TEST_CASE("swapWithPreviousWithScale swaps both ways and changes nothing without a partner",
          "[tuning][tuning-scales]")
{
  Selection selection;
  Bank bank;
  defaultBank(bank);
  uint8_t scaleIndex = scaleNamed("Phrygian");
  REQUIRE_FALSE(swapWithPreviousWithScale(selection, bank, scaleIndex));
  REQUIRE(scaleIndex == scaleNamed("Phrygian"));

  applyTuningWithScale(selection, bank, 96, scaleIndex); // 22 Shruti keeps Phrygian (it is offered)
  REQUIRE(scaleIndex == scaleNamed("Phrygian"));
  scaleIndex = scaleNamed("Todi Thaat");
  REQUIRE(swapWithPreviousWithScale(selection, bank, scaleIndex)); // back to 12-EDO
  REQUIRE(selection.tuningId == 0);
  REQUIRE(scaleIndex == scaleNamed("Todi Thaat")); // offered there too: kept
  REQUIRE(bank.lastScale[libraryIndexOf(96)] == scaleNamed("Todi Thaat"));

  bank.previousId = 200; // a partner that no longer exists
  REQUIRE_FALSE(swapWithPreviousWithScale(selection, bank, scaleIndex));
}
