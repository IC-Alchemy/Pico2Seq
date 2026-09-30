#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>

#include "ui/TuningPageControls.h"
#include "ui/TuningPageLogic.h"

// The Tuning page: its gesture policy (open chord, scale buttons, hot favourites, A/B) and the
// pure logic that turns gestures into a tuning::Selection, the scale that plays in it and the
// Bank. One page holds the whole library, one tuning per pad.
using namespace TuningPage;
using Catch::Approx;

namespace
{
constexpr uint8_t kShift = Controls::kShift;
constexpr uint8_t kBtn3 = Controls::kOpenKey;
constexpr uint8_t kBtn(int n) { return static_cast<uint8_t>(1u << (n - 1)); }

// Open the page the way a hand does: Shift down, then button 3, then everything up.
Controls openPage(uint32_t now = 0)
{
  Controls c;
  c.poll(kShift, 0, true, now);
  const Input in = c.poll(kShift | kBtn3, 0, true, now);
  REQUIRE(in.open);
  c.poll(0, 0, true, now); // fingers lifted: waitRelease clears
  REQUIRE(c.active);
  REQUIRE_FALSE(c.waitRelease);
  return c;
}

uint8_t scaleNamed(const char *name)
{
  for (size_t i = 0; i < SCALES_COUNT; ++i)
    if (std::strcmp(scaleNames[i], name) == 0)
      return static_cast<uint8_t>(i);
  FAIL("no scale named " << name);
  return 0;
}

// A fresh unit: 12-EDO playing Ionian, the default bank.
struct Rig
{
  tuning::Selection selection;
  tuning::Bank bank;
  uint8_t scaleIndex = 0;
  Rig() { tuning::defaultBank(bank); }
};

uint8_t padOf(uint8_t tuningId)
{
  const uint8_t pad = padOfTuning(tuningId);
  REQUIRE(pad != kNoControl);
  return pad;
}
} // namespace

// --- Gesture policy ------------------------------------------------------------------------

TEST_CASE("open chord needs Shift held and a fresh press of button 3", "[tuning][tuning-page]")
{
  Controls c;
  // Button 3 alone keeps its short action (cycle the scale): nothing is consumed.
  Input in = c.poll(kBtn3, 0, true, 0);
  REQUIRE_FALSE(in.consumed);
  REQUIRE_FALSE(in.open);
  REQUIRE_FALSE(c.active);
  c.poll(0, 0, true, 0);

  // Shift then button 3 opens.
  c.poll(kShift, 0, true, 0);
  in = c.poll(kShift | kBtn3, 0, true, 0);
  REQUIRE(in.consumed);
  REQUIRE(in.open);
  REQUIRE(c.active);
  REQUIRE(c.waitRelease);
}

TEST_CASE("a button 3 already held when Shift lands does not open", "[tuning][tuning-page]")
{
  Controls c;
  c.poll(kBtn3, 0, true, 0);
  const Input in = c.poll(kBtn3 | kShift, 0, true, 0); // Shift is the new edge
  REQUIRE_FALSE(in.open);
  REQUIRE_FALSE(c.active);
}

TEST_CASE("another page owning the chord blocks opening", "[tuning][tuning-page]")
{
  Controls c;
  c.poll(kShift, 0, false, 0);
  const Input in = c.poll(kShift | kBtn3, 0, false, 0);
  REQUIRE_FALSE(in.consumed);
  REQUIRE_FALSE(c.active);
}

TEST_CASE("the fingers that opened the page are swallowed until every button is up",
          "[tuning][tuning-page]")
{
  Controls c;
  c.poll(kShift, 0, true, 0);
  c.poll(kShift | kBtn3, 0, true, 0);
  // Still down, and a scale key plus a voice button land too: consumed, no actions.
  Input in = c.poll(kShift | kBtn3 | kBtn(1), 1, true, 0);
  REQUIRE(in.consumed);
  REQUIRE(in.scaleSlot < 0);
  REQUIRE(in.recallSlot < 0);
  REQUIRE(c.waitRelease);
  in = c.poll(0, 0, true, 0);
  REQUIRE(in.consumed);
  REQUIRE_FALSE(c.waitRelease);
}

TEST_CASE("Shift leaves the page and its release tail is consumed", "[tuning][tuning-page]")
{
  Controls c = openPage();
  const Input in = c.poll(kShift, 0, true, 0);
  REQUIRE(in.exit);
  REQUIRE(in.consumed);
  REQUIRE_FALSE(c.active);
  REQUIRE(c.waitRelease);
  // Shift + button 3 again is not an open while the tail drains.
  Input again = c.poll(kShift | kBtn3, 0, true, 0);
  REQUIRE_FALSE(again.open);
  REQUIRE(again.consumed);
  c.poll(0, 0, true, 0);
  REQUIRE_FALSE(c.waitRelease);
  REQUIRE_FALSE(c.poll(kBtn3, 0, true, 0).consumed); // normal input is back
}

TEST_CASE("buttons 1 to 6 choose scale slots 0 to 5", "[tuning][tuning-page]")
{
  Controls c = openPage();
  for (int n = 1; n <= 6; ++n)
  {
    const Input in = c.poll(kBtn(n), 0, true, 0);
    REQUIRE(in.consumed);
    REQUIRE(in.scaleSlot == n - 1);
    REQUIRE_FALSE(in.swap);
    c.poll(0, 0, true, 0);
  }
  // The first press after opening works too: button 3 is scale slot 2 once the chord is over.
  REQUIRE(c.poll(kBtn3, 0, true, 0).scaleSlot == 2);
}

TEST_CASE("two scale keys at once: the lowest wins", "[tuning][tuning-page]")
{
  Controls c = openPage();
  REQUIRE(c.poll(kBtn(4) | kBtn(2), 0, true, 0).scaleSlot == 1);
}

TEST_CASE("a held scale key is not a second press", "[tuning][tuning-page]")
{
  Controls c = openPage();
  REQUIRE(c.poll(kBtn(5), 0, true, 0).scaleSlot == 4);
  const Input held = c.poll(kBtn(5), 0, true, 100);
  REQUIRE(held.scaleSlot < 0);
  REQUIRE_FALSE(held.swap);
}

TEST_CASE("button 7 asks for the A/B swap and Shift, 8, is not a scale key", "[tuning][tuning-page]")
{
  Controls c = openPage();
  const Input in = c.poll(kBtn(7), 0, true, 0);
  REQUIRE(in.swap);
  REQUIRE(in.scaleSlot < 0);
  c.poll(0, 0, true, 0);
  REQUIRE(c.poll(kBtn(7), 0, true, 0).swap); // every fresh press asks again
}

TEST_CASE("a short voice-button press recalls its hot favourite on release", "[tuning][tuning-page]")
{
  Controls c = openPage();
  REQUIRE(c.poll(0, kBtn(3), true, 1000).recallSlot < 0); // nothing yet: recall is on release
  const Input in = c.poll(0, 0, true, 1200);
  REQUIRE(in.recallSlot == 2);
  REQUIRE(in.storeSlot < 0);
}

TEST_CASE("holding a voice button stores once at the threshold and not again on release",
          "[tuning][tuning-page]")
{
  Controls c = openPage();
  c.poll(0, kBtn(2), true, 1000);
  REQUIRE(c.poll(0, kBtn(2), true, 1000 + kHoldMs - 1).storeSlot < 0);
  const Input fired = c.poll(0, kBtn(2), true, 1000 + kHoldMs);
  REQUIRE(fired.storeSlot == 1);
  REQUIRE(c.poll(0, kBtn(2), true, 3000).storeSlot < 0); // still held: fires once
  const Input released = c.poll(0, 0, true, 3100);
  REQUIRE(released.recallSlot < 0);
  REQUIRE(released.storeSlot < 0);
}

TEST_CASE("a hold noticed only at release still counts as a hold", "[tuning][tuning-page]")
{
  Controls c = openPage();
  c.poll(0, kBtn(1), true, 0);
  const Input in = c.poll(0, 0, true, 2 * kHoldMs); // no poll in between
  REQUIRE(in.storeSlot == 0);
  REQUIRE(in.recallSlot < 0);
}

TEST_CASE("hold timing survives the millisecond counter wrapping", "[tuning][tuning-page]")
{
  Controls c = openPage();
  const uint32_t nearWrap = 0xFFFFFFFFu - 100u;
  c.poll(0, kBtn(4), true, nearWrap);
  REQUIRE(c.poll(0, kBtn(4), true, nearWrap + 50u).storeSlot < 0);
  REQUIRE(c.poll(0, kBtn(4), true, nearWrap + kHoldMs).storeSlot == 3);
}

TEST_CASE("voice buttons held when the page closes do not carry a hold into the next open",
          "[tuning][tuning-page]")
{
  Controls c = openPage();
  c.poll(0, kBtn(1), true, 0);
  c.poll(kShift, kBtn(1), true, 10); // exit with the voice button still down
  REQUIRE(c.voiceHolds.down == 0);
  c.poll(0, 0, true, 20);
  c.poll(kShift, 0, true, 30);
  c.poll(kShift | kBtn3, 0, true, 30);
  c.poll(0, 0, true, 40);
  const Input in = c.poll(0, 0, true, 5000);
  REQUIRE(in.recallSlot < 0);
  REQUIRE(in.storeSlot < 0);
}

TEST_CASE("observe keeps levels current so a button held across another screen is not a press",
          "[tuning][tuning-page]")
{
  Controls c;
  c.observe(kShift | kBtn3, 0);
  REQUIRE_FALSE(c.poll(kShift | kBtn3, 0, true, 0).open);
  REQUIRE_FALSE(c.active);
}

TEST_CASE("the press tracker resolves each press exactly once", "[tuning][tuning-page]")
{
  PressTracker<kHotSlots> tracker;
  using Result = PressTracker<kHotSlots>::Result;
  tracker.press(0, 100);
  REQUIRE(tracker.release(0, 100 + kHoldMs - 1) == Result::Tap);
  REQUIRE(tracker.release(0, 5000) == Result::None); // not down any more
  tracker.press(1, 0);
  REQUIRE(tracker.pollHold(kHoldMs) == 1);
  REQUIRE(tracker.pollHold(kHoldMs) == -1);
  REQUIRE(tracker.release(1, 2 * kHoldMs) == Result::None); // the hold already fired
  tracker.press(2, 0);
  REQUIRE(tracker.release(2, kHoldMs) == Result::Hold);
  tracker.press(99, 0); // out of range: ignored
  REQUIRE(tracker.release(99, 0) == Result::None);
  tracker.press(3, 0);
  tracker.clear();
  REQUIRE(tracker.pollHold(10 * kHoldMs) == -1);
}

// --- One page: which tuning is on which pad --------------------------------------------------

TEST_CASE("pad k holds library tuning k and the pads past the last one are dark", "[tuning][tuning-page]")
{
  for (uint8_t pad = 0; pad < tuning::libraryCount(); ++pad)
    REQUIRE(padTuningId(pad) == tuning::libraryAt(pad).id);
  for (uint8_t pad = static_cast<uint8_t>(tuning::libraryCount()); pad < kPadCount; ++pad)
    REQUIRE(padTuningId(pad) == kNoTuning);
  REQUIRE(padTuningId(kPadCount) == kNoTuning);
  REQUIRE(padTuningId(255) == kNoTuning);
}

TEST_CASE("every library tuning is on exactly one pad and padOfTuning finds it", "[tuning][tuning-page]")
{
  std::set<uint8_t> seen;
  for (size_t i = 0; i < tuning::libraryCount(); ++i)
  {
    const uint8_t id = tuning::libraryAt(i).id;
    const uint8_t pad = padOfTuning(id);
    REQUIRE(pad == i);
    REQUIRE(padTuningId(pad) == id);
    REQUIRE(seen.insert(pad).second);
  }
  REQUIRE(padOfTuning(200) == kNoControl);
  REQUIRE(padOfTuning(tuning::kEmptySlot) == kNoControl);
}

TEST_CASE("the pads of a family are one unbroken run", "[tuning][tuning-page]")
{
  // The matrix shows each family as a block of one colour.
  uint8_t previousFamily = 0;
  std::set<uint8_t> closed;
  for (uint8_t pad = 0; pad < tuning::libraryCount(); ++pad)
  {
    const uint8_t family = static_cast<uint8_t>(tuning::resolve(padTuningId(pad)).family);
    if (pad > 0 && family != previousFamily)
      closed.insert(previousFamily);
    REQUIRE(closed.count(family) == 0);
    previousFamily = family;
  }
}

TEST_CASE("padView tells the LEDs what each pad is", "[tuning][tuning-page]")
{
  Rig rig;
  tuning::applyTuning(rig.selection, rig.bank, 32); // 5-Limit JI now, 12-EDO before

  PadView view = padView(padOf(32), rig.selection, rig.bank);
  REQUIRE(view.exists);
  REQUIRE(view.tuningId == 32);
  REQUIRE(view.current);
  REQUIRE_FALSE(view.previous);
  REQUIRE(view.hotSlot == 2); // a default hot favourite
  REQUIRE(view.family == static_cast<uint8_t>(tuning::Family::Just));

  view = padView(padOf(0), rig.selection, rig.bank);
  REQUIRE(view.previous); // the A/B partner
  REQUIRE_FALSE(view.current);
  REQUIRE(view.hotSlot == 0);
  REQUIRE(view.family == static_cast<uint8_t>(tuning::Family::Equal));

  view = padView(padOf(4), rig.selection, rig.bank); // 22-EDO: nothing special
  REQUIRE(view.exists);
  REQUIRE_FALSE(view.current);
  REQUIRE_FALSE(view.previous);
  REQUIRE(view.hotSlot == kNoTuning);

  view = padView(31, rig.selection, rig.bank); // dark
  REQUIRE_FALSE(view.exists);
  REQUIRE(view.tuningId == kNoTuning);
  REQUIRE_FALSE(view.current);
  REQUIRE_FALSE(view.previous);
  REQUIRE(view.hotSlot == kNoTuning);
}

TEST_CASE("hotSlotOf finds the first slot holding a tuning", "[tuning][tuning-page]")
{
  Rig rig;
  REQUIRE(hotSlotOf(rig.bank, 96) == 3);
  REQUIRE(hotSlotOf(rig.bank, 64) == kNoTuning);
  tuning::storeFavorite(rig.bank, 0, 96);
  REQUIRE(hotSlotOf(rig.bank, 96) == 0);
}

// --- Choosing a tuning brings a scale of that tuning ------------------------------------------

TEST_CASE("tapping a pad chooses its tuning and remembers the old one for A/B", "[tuning][tuning-page]")
{
  Rig rig;
  const Result r = padTap(padOf(32), rig.selection, rig.bank, rig.scaleIndex);
  REQUIRE(r.change == Change::Applied);
  REQUIRE(r.tuningId == 32);
  REQUIRE(rig.selection.tuningId == 32);
  REQUIRE(rig.bank.previousId == 0);
  // Both are twelve-note tunings: the scale stays put, nothing to announce.
  REQUIRE(rig.scaleIndex == 0);
  REQUIRE_FALSE(r.scaleChanged);
  REQUIRE(r.scaleIndex == 0);
}

TEST_CASE("tapping the tuning that is already playing, or a dark pad, does nothing", "[tuning][tuning-page]")
{
  Rig rig;
  rig.scaleIndex = 3;
  REQUIRE(padTap(padOf(0), rig.selection, rig.bank, rig.scaleIndex).change == Change::None);
  REQUIRE(padTap(31, rig.selection, rig.bank, rig.scaleIndex).change == Change::None);
  REQUIRE(padTap(200, rig.selection, rig.bank, rig.scaleIndex).change == Change::None);
  REQUIRE(rig.selection.tuningId == 0);
  REQUIRE(rig.scaleIndex == 3);
  REQUIRE(rig.bank.previousId == 0);
}

TEST_CASE("a tuning that is not twelve-note brings a scale of its own", "[tuning][tuning-page]")
{
  Rig rig;
  rig.scaleIndex = scaleNamed("Dorian");
  const Result r = padTap(padOf(1), rig.selection, rig.bank, rig.scaleIndex); // 24-EDO
  REQUIRE(r.change == Change::Applied);
  REQUIRE(r.scaleChanged);
  REQUIRE(rig.scaleIndex == scaleNamed("Maqam Rast"));
  REQUIRE(r.scaleIndex == rig.scaleIndex);
  REQUIRE(tuning::scaleAvailable(1, rig.scaleIndex));
  REQUIRE(scaleIsNative(rig.scaleIndex)); // it holds 24-EDO degrees, not semitones
}

TEST_CASE("every tuning leaves a scale that belongs to it, from every scale of every tuning",
          "[tuning][tuning-page]")
{
  for (uint8_t from = 0; from < tuning::libraryCount(); ++from)
  {
    const uint8_t fromId = padTuningId(from);
    const tuning::ScaleSet set = tuning::scaleSet(fromId);
    for (uint8_t slot = 0; slot < set.count; ++slot)
      for (uint8_t to = 0; to < tuning::libraryCount(); ++to)
      {
        Rig rig;
        rig.selection.tuningId = fromId;
        rig.scaleIndex = set.scales[slot];
        padTap(to, rig.selection, rig.bank, rig.scaleIndex);
        INFO("from tuning " << int(fromId) << " scale " << scaleNames[set.scales[slot]] << " to pad " << int(to));
        REQUIRE(rig.selection.tuningId == padTuningId(to));
        REQUIRE(tuning::scaleAvailable(rig.selection.tuningId, rig.scaleIndex));
      }
  }
}

TEST_CASE("coming back to a tuning brings back the scale you had there", "[tuning][tuning-page]")
{
  Rig rig;
  rig.scaleIndex = scaleNamed("Dorian");
  padTap(padOf(32), rig.selection, rig.bank, rig.scaleIndex); // JI, still Dorian
  REQUIRE(rig.scaleIndex == scaleNamed("Dorian"));
  padTap(padOf(3), rig.selection, rig.bank, rig.scaleIndex);  // 31-EDO
  REQUIRE(rig.scaleIndex == scaleNamed("31-EDO Major"));
  padTap(padOf(32), rig.selection, rig.bank, rig.scaleIndex); // back to JI
  REQUIRE(rig.scaleIndex == scaleNamed("Dorian"));            // not the set's first scale
  padTap(padOf(3), rig.selection, rig.bank, rig.scaleIndex);
  REQUIRE(rig.scaleIndex == scaleNamed("31-EDO Major"));
}

TEST_CASE("the scale you chose in a tuning is the one remembered for it", "[tuning][tuning-page]")
{
  Rig rig;
  padTap(padOf(1), rig.selection, rig.bank, rig.scaleIndex); // 24-EDO -> Rast
  scaleButton(2, rig.selection, rig.scaleIndex);             // slot 2: Hijaz
  REQUIRE(rig.scaleIndex == scaleNamed("Maqam Hijaz"));
  padTap(padOf(0), rig.selection, rig.bank, rig.scaleIndex); // 12-EDO: Hijaz is not offered
  REQUIRE(rig.scaleIndex == 0);
  padTap(padOf(1), rig.selection, rig.bank, rig.scaleIndex);
  REQUIRE(rig.scaleIndex == scaleNamed("Maqam Hijaz"));
}

TEST_CASE("a pad tap reports the scale it left playing", "[tuning][tuning-page]")
{
  Rig rig;
  const Result r = padTap(padOf(96), rig.selection, rig.bank, rig.scaleIndex); // 22 Shruti
  REQUIRE(r.change == Change::Applied);
  // Ionian means something on the 22 shrutis (the svara layout maps it), so it stays.
  REQUIRE(rig.scaleIndex == 0);
  REQUIRE_FALSE(r.scaleChanged);
  const Result away = padTap(padOf(128), rig.selection, rig.bank, rig.scaleIndex); // Bohlen-Pierce
  REQUIRE(away.scaleChanged);
  REQUIRE(rig.scaleIndex == scaleNamed("Bohlen-Pierce Lambda"));
}

// --- Buttons, hot favourites, A/B, encoder ------------------------------------------------------

TEST_CASE("scale buttons choose from the playing tuning's own scales", "[tuning][tuning-page]")
{
  Rig rig;
  rig.selection.tuningId = 1; // 24-EDO: Rast, Bayati, Hijaz, Saba, All Degrees
  rig.scaleIndex = scaleNamed("Maqam Rast");
  Result r = scaleButton(1, rig.selection, rig.scaleIndex);
  REQUIRE(r.change == Change::ScaleChosen);
  REQUIRE(r.scaleChanged);
  REQUIRE(rig.scaleIndex == scaleNamed("Maqam Bayati"));
  REQUIRE(r.scaleIndex == rig.scaleIndex);

  r = scaleButton(1, rig.selection, rig.scaleIndex); // the same scale again: still reported
  REQUIRE(r.change == Change::ScaleChosen);
  REQUIRE_FALSE(r.scaleChanged);

  r = scaleButton(4, rig.selection, rig.scaleIndex);
  REQUIRE(rig.scaleIndex == scaleNamed("All Degrees"));
}

TEST_CASE("a scale button past the end of the tuning's scales says so and changes nothing",
          "[tuning][tuning-page]")
{
  Rig rig;
  rig.selection.tuningId = 128; // Bohlen-Pierce: two scales
  rig.scaleIndex = scaleNamed("Bohlen-Pierce Lambda");
  for (uint8_t slot = 2; slot < kScaleKeys; ++slot)
  {
    const Result r = scaleButton(slot, rig.selection, rig.scaleIndex);
    REQUIRE(r.change == Change::NoScale);
    REQUIRE_FALSE(r.scaleChanged);
    REQUIRE(rig.scaleIndex == scaleNamed("Bohlen-Pierce Lambda"));
  }
}

TEST_CASE("hot favourites: tap recalls with the scale, empty reports Empty", "[tuning][tuning-page]")
{
  Rig rig;
  Result r = hotRecall(1, rig.selection, rig.bank, rig.scaleIndex); // slot 2 = 24-EDO
  REQUIRE(r.change == Change::Applied);
  REQUIRE(r.slot == 1);
  REQUIRE(rig.selection.tuningId == 1);
  REQUIRE(r.scaleChanged);
  REQUIRE(rig.scaleIndex == scaleNamed("Maqam Rast"));

  tuning::clearFavorite(rig.bank, 2);
  r = hotRecall(2, rig.selection, rig.bank, rig.scaleIndex);
  REQUIRE(r.change == Change::Empty);
  REQUIRE(r.slot == 2);
  REQUIRE(rig.selection.tuningId == 1);

  r = hotRecall(1, rig.selection, rig.bank, rig.scaleIndex); // already playing it
  REQUIRE(r.change == Change::None);
}

TEST_CASE("hot favourites: hold stores the playing tuning, and again clears it", "[tuning][tuning-page]")
{
  Rig rig;
  padTap(padOf(64), rig.selection, rig.bank, rig.scaleIndex);
  Result r = hotStore(2, rig.selection, rig.bank);
  REQUIRE(r.change == Change::Starred);
  REQUIRE(r.slot == 2);
  REQUIRE(r.tuningId == 64);
  REQUIRE(tuning::favoriteAt(rig.bank, 2) == 64);
  r = hotStore(2, rig.selection, rig.bank);
  REQUIRE(r.change == Change::Unstarred);
  REQUIRE(tuning::favoriteAt(rig.bank, 2) == tuning::kEmptySlot);
  REQUIRE(hotStore(9, rig.selection, rig.bank).change == Change::None);
}

TEST_CASE("A/B swaps the two most recent tunings, each with the scale it had", "[tuning][tuning-page]")
{
  Rig rig;
  rig.scaleIndex = scaleNamed("Dorian");
  padTap(padOf(1), rig.selection, rig.bank, rig.scaleIndex); // 24-EDO, Rast
  REQUIRE(rig.scaleIndex == scaleNamed("Maqam Rast"));

  Result r = swapAB(rig.selection, rig.bank, rig.scaleIndex);
  REQUIRE(r.change == Change::Applied);
  REQUIRE(r.tuningId == 0);
  REQUIRE(rig.selection.tuningId == 0);
  REQUIRE(rig.scaleIndex == scaleNamed("Dorian")); // the scale of the tuning it came from
  REQUIRE(r.scaleChanged);

  r = swapAB(rig.selection, rig.bank, rig.scaleIndex);
  REQUIRE(rig.selection.tuningId == 1);
  REQUIRE(rig.scaleIndex == scaleNamed("Maqam Rast"));

  Rig fresh;
  REQUIRE(swapAB(fresh.selection, fresh.bank, fresh.scaleIndex).change == Change::None);
  REQUIRE_FALSE(swapAB(fresh.selection, fresh.bank, fresh.scaleIndex).scaleChanged);
}

TEST_CASE("the encoder steps through the whole library and wraps", "[tuning][tuning-page]")
{
  Rig rig;
  Result r = encoderStep(1, rig.selection, rig.bank, rig.scaleIndex);
  REQUIRE(r.change == Change::Applied);
  REQUIRE(rig.selection.tuningId == 1);
  REQUIRE(rig.scaleIndex == scaleNamed("Maqam Rast")); // and brings the scale along

  // Walk all the way round: every tuning is visited once, in pad order.
  Rig walk;
  for (uint8_t pad = 1; pad < tuning::libraryCount(); ++pad)
  {
    encoderStep(1, walk.selection, walk.bank, walk.scaleIndex);
    REQUIRE(walk.selection.tuningId == padTuningId(pad));
    REQUIRE(tuning::scaleAvailable(walk.selection.tuningId, walk.scaleIndex));
  }
  encoderStep(1, walk.selection, walk.bank, walk.scaleIndex);
  REQUIRE(walk.selection.tuningId == 0); // wrapped

  Rig back;
  encoderStep(-1, back.selection, back.bank, back.scaleIndex);
  REQUIRE(back.selection.tuningId == tuning::libraryAt(tuning::libraryCount() - 1).id);
  REQUIRE(encoderStep(0, back.selection, back.bank, back.scaleIndex).change == Change::None);
}

// --- Faders -----------------------------------------------------------------------------------

TEST_CASE("faders 1-3 own tonic, A4 and scale; fader 4 is free", "[tuning][tuning-page]")
{
  REQUIRE(faderForChannel(0) == Fader::Tonic);
  REQUIRE(faderForChannel(1) == Fader::Reference);
  REQUIRE(faderForChannel(2) == Fader::Scale);
  REQUIRE(faderForChannel(3) == Fader::Unassigned);
  REQUIRE(faderForChannel(200) == Fader::Unassigned);
}

TEST_CASE("the tonic fader is twelve equal zones and round-trips", "[tuning][tuning-page]")
{
  REQUIRE(tonicForFader(0.0f) == 0);
  REQUIRE(tonicForFader(0.999f) == 11);
  REQUIRE(tonicForFader(1.0f) == 11);
  REQUIRE(tonicForFader(-3.0f) == 0);
  REQUIRE(tonicForFader(7.0f) == 11);
  for (uint8_t t = 0; t < 12; ++t)
    REQUIRE(tonicForFader(tonicFaderPosition(t)) == t);
}

TEST_CASE("the A4 fader spans 415 to 466 Hz with detents at the pitches in use", "[tuning][tuning-page]")
{
  REQUIRE(a4ForFader(0.0f) == Approx(415.0f));
  REQUIRE(a4ForFader(1.0f) == Approx(466.0f));
  REQUIRE(a4ForFader(-1.0f) == Approx(415.0f));
  REQUIRE(a4ForFader(2.0f) == Approx(466.0f));
  // Every position within reach of 432 / 440 / 442 lands exactly on it.
  for (const float detent : kA4DetentHz)
  {
    const float centre = a4FaderPosition(detent);
    for (float d = -0.004f; d <= 0.004f; d += 0.001f)
    {
      REQUIRE(a4ForFader(centre + d) == detent);
    }
    REQUIRE(a4ForFader(centre) == detent);
  }
  // Reachable values are half-hertz steps.
  for (float x = 0.0f; x <= 1.0f; x += 0.013f)
  {
    const float steps = (a4ForFader(x) - 415.0f) / kA4StepHz;
    REQUIRE(steps == Approx(std::round(steps)).margin(1e-4));
  }
  // The 441 Hz notch between two detents is still reachable.
  bool reached441 = false;
  for (float x = 0.0f; x <= 1.0f; x += 0.0005f)
    reached441 = reached441 || a4ForFader(x) == 441.0f;
  REQUIRE(reached441);
}

TEST_CASE("the A4 fader is monotonic", "[tuning][tuning-page]")
{
  float last = 0.0f;
  for (float x = 0.0f; x <= 1.0f; x += 0.001f)
  {
    const float hz = a4ForFader(x);
    REQUIRE(hz >= last);
    last = hz;
  }
}

TEST_CASE("the scale fader spreads over the playing tuning's own scales", "[tuning][tuning-page]")
{
  for (size_t i = 0; i < tuning::libraryCount(); ++i)
  {
    const uint8_t id = tuning::libraryAt(i).id;
    const tuning::ScaleSet set = tuning::scaleSet(id);
    INFO("tuning " << int(id) << " with " << int(set.count) << " scales");
    REQUIRE(scaleSlotForFader(0.0f, id) == 0);
    REQUIRE(scaleSlotForFader(1.0f, id) == set.count - 1);
    REQUIRE(scaleSlotForFader(-1.0f, id) == 0);
    REQUIRE(scaleSlotForFader(9.0f, id) == set.count - 1);
    for (uint8_t slot = 0; slot < set.count; ++slot)
    {
      const float position = scaleFaderPosition(id, set.scales[slot]);
      REQUIRE(scaleSlotForFader(position, id) == slot); // the bar sits in the zone it came from
    }
  }
  // A scale the tuning does not offer parks the bar at the first zone.
  REQUIRE(scaleSlotForFader(scaleFaderPosition(1, 0), 1) == 0);
}

TEST_CASE("applyFader reports a change only when the setting moved", "[tuning][tuning-page]")
{
  tuning::Selection selection;
  uint8_t scale = 0;

  REQUIRE(applyFader(Fader::Tonic, tonicFaderPosition(2), selection, scale));
  REQUIRE(selection.tonic == 2);
  REQUIRE_FALSE(applyFader(Fader::Tonic, tonicFaderPosition(2), selection, scale));

  REQUIRE(applyFader(Fader::Reference, a4FaderPosition(432.0f), selection, scale));
  REQUIRE(selection.a4Hz == 432.0f);
  REQUIRE_FALSE(applyFader(Fader::Reference, a4FaderPosition(432.0f), selection, scale));

  // Twelve-note tuning: the scale fader walks the twelve-note set.
  REQUIRE(applyFader(Fader::Scale, scaleFaderPosition(0, scaleNamed("Bhairav Thaat")), selection, scale));
  REQUIRE(scale == scaleNamed("Bhairav Thaat"));
  REQUIRE_FALSE(applyFader(Fader::Scale, scaleFaderPosition(0, scaleNamed("Bhairav Thaat")), selection, scale));

  // 24-EDO: the same fader now picks maqams.
  selection.tuningId = 1;
  scale = scaleNamed("Maqam Rast");
  REQUIRE(applyFader(Fader::Scale, 0.3f, selection, scale));
  REQUIRE(scale == scaleNamed("Maqam Bayati"));
  REQUIRE(tuning::scaleAvailable(1, scale));

  REQUIRE_FALSE(applyFader(Fader::Unassigned, 0.5f, selection, scale));
}

TEST_CASE("the scale fader never leaves a scale the tuning does not offer", "[tuning][tuning-page]")
{
  for (size_t i = 0; i < tuning::libraryCount(); ++i)
  {
    tuning::Selection selection;
    selection.tuningId = tuning::libraryAt(i).id;
    uint8_t scale = tuning::defaultScale(selection.tuningId);
    for (float x = 0.0f; x <= 1.0f; x += 0.01f)
    {
      applyFader(Fader::Scale, x, selection, scale);
      REQUIRE(tuning::scaleAvailable(selection.tuningId, scale));
    }
  }
}

TEST_CASE("applyFader ignores NaN and infinity", "[tuning][tuning-page]")
{
  tuning::Selection selection;
  selection.tonic = 5;
  uint8_t scale = 3;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  for (const float bad : {nan, inf, -inf})
  {
    REQUIRE_FALSE(applyFader(Fader::Tonic, bad, selection, scale));
    REQUIRE_FALSE(applyFader(Fader::Reference, bad, selection, scale));
    REQUIRE_FALSE(applyFader(Fader::Scale, bad, selection, scale));
  }
  REQUIRE(selection.tonic == 5);
  REQUIRE(selection.a4Hz == 440.0f);
  REQUIRE(scale == 3);
  REQUIRE_FALSE(finiteFloat(nan));
  REQUIRE(finiteFloat(0.25f));
}

TEST_CASE("the fader position agrees with what applyFader would set", "[tuning][tuning-page]")
{
  tuning::Selection selection;
  selection.tuningId = 1;
  selection.tonic = 9;
  selection.a4Hz = 432.0f;
  const uint8_t scale = scaleNamed("Maqam Hijaz");
  REQUIRE(tonicForFader(faderPositionFor(Fader::Tonic, selection, scale)) == 9);
  REQUIRE(a4ForFader(faderPositionFor(Fader::Reference, selection, scale)) == 432.0f);
  REQUIRE(tuning::scaleAtSlot(1, scaleSlotForFader(faderPositionFor(Fader::Scale, selection, scale), 1)) == scale);
  REQUIRE(faderPositionFor(Fader::Unassigned, selection, scale) == 0.0f);
}

// --- OLED text --------------------------------------------------------------------------------

TEST_CASE("the header says where the tuning sits in the library and its family", "[tuning][tuning-page]")
{
  char line[kLine];
  tuning::Selection selection;
  formatHeader(selection, line, sizeof(line));
  REQUIRE(std::string(line) == "TUNING 1/29 EDO");
  selection.tuningId = 32;
  formatHeader(selection, line, sizeof(line));
  REQUIRE(std::string(line) == "TUNING 13/29 JUST");
  selection.tuningId = 96;
  formatHeader(selection, line, sizeof(line));
  REQUIRE(std::string(line) == "TUNING 23/29 INDIAN");
  selection.tuningId = 131;
  formatHeader(selection, line, sizeof(line));
  REQUIRE(std::string(line) == "TUNING 29/29 XENO");
  for (size_t i = 0; i < tuning::libraryCount(); ++i)
  {
    selection.tuningId = tuning::libraryAt(i).id;
    formatHeader(selection, line, sizeof(line));
    REQUIRE(std::strlen(line) <= tuning::kMaxNameLength);
  }
}

TEST_CASE("the scale line shows the slot among this tuning's scales and the name", "[tuning][tuning-page]")
{
  char line[kLine];
  tuning::Selection selection;
  formatScaleLine(selection, 0, line, sizeof(line));
  REQUIRE(std::string(line) == "1/17 Ionian Major");

  selection.tuningId = 1; // 24-EDO
  formatScaleLine(selection, scaleNamed("Maqam Hijaz"), line, sizeof(line));
  REQUIRE(std::string(line) == "3/5 Maqam Hijaz");
  formatScaleLine(selection, scaleNamed("All Degrees"), line, sizeof(line));
  REQUIRE(std::string(line) == "5/5 All Degrees");

  // Too long for the line: the short name takes over.
  selection.tuningId = 128;
  formatScaleLine(selection, scaleNamed("Bohlen-Pierce Lambda"), line, sizeof(line));
  REQUIRE(std::string(line) == "1/2 BP Lambda");

  // A scale the tuning does not offer still prints, with a dash for the slot.
  selection.tuningId = 1;
  formatScaleLine(selection, 0, line, sizeof(line));
  REQUIRE(std::string(line) == "-/5 Ionian Major");
}

TEST_CASE("the scale line fits the screen for every scale of every tuning", "[tuning][tuning-page]")
{
  char line[kLine];
  for (size_t i = 0; i < tuning::libraryCount(); ++i)
  {
    tuning::Selection selection;
    selection.tuningId = tuning::libraryAt(i).id;
    const tuning::ScaleSet set = tuning::scaleSet(selection.tuningId);
    for (uint8_t slot = 0; slot < set.count; ++slot)
    {
      formatScaleLine(selection, set.scales[slot], line, sizeof(line) - 1); // marker column taken
      INFO("tuning " << int(selection.tuningId) << " slot " << int(slot) << ": " << line);
      REQUIRE(std::strlen(line) <= tuning::kMaxNameLength - 1);
    }
  }
}

TEST_CASE("scale labels use the full name when it fits", "[tuning][tuning-page]")
{
  REQUIRE(std::string(scaleLabel(scaleNamed("Maqam Rast"), 21)) == "Maqam Rast");
  REQUIRE(std::string(scaleLabel(scaleNamed("Overtone Heptatonic"), 21)) == "Overtone Heptatonic");
  REQUIRE(std::string(scaleLabel(scaleNamed("Bohlen-Pierce Lambda"), 19)) == "BP Lambda");
  REQUIRE(std::string(scaleLabel(200, 21)) == scaleNames[0]); // out of range: the first scale
}

TEST_CASE("the pitch line says Sa for Indian tunings and Root otherwise", "[tuning][tuning-page]")
{
  char line[kLine];
  tuning::Selection selection;
  selection.tonic = 2;
  formatPitchLine(selection, line, sizeof(line));
  REQUIRE(std::string(line) == "Root=D  A4=440.0Hz");

  selection.tuningId = 97; // 12 Svara JI
  selection.a4Hz = 432.0f;
  formatPitchLine(selection, line, sizeof(line));
  REQUIRE(std::string(line) == "Sa=D  A4=432.0Hz");

  selection.tuningId = 96; // 22 Shruti
  selection.tonic = 11;
  selection.a4Hz = 466.0f;
  formatPitchLine(selection, line, sizeof(line));
  REQUIRE(std::string(line) == "Sa=B  A4=466.0Hz");
  REQUIRE(std::strlen(line) <= tuning::kMaxNameLength);
}

TEST_CASE("the status line is empty at standard pitch and short otherwise", "[tuning][tuning-page]")
{
  char line[kLine];
  tuning::Selection selection;
  formatStatus(selection, line, sizeof(line));
  REQUIRE(std::string(line).empty());

  selection.tuningId = 32;
  formatStatus(selection, line, sizeof(line));
  REQUIRE(std::string(line) == std::string(tuning::resolve(32).shortName) + " C");

  selection.tonic = 2;
  selection.a4Hz = 432.0f;
  formatStatus(selection, line, sizeof(line));
  REQUIRE(std::string(line) == std::string(tuning::resolve(32).shortName) + " D A432.0");

  // 12-EDO with only the tonic or reference moved is still shown.
  selection = tuning::Selection{};
  selection.tonic = 7;
  formatStatus(selection, line, sizeof(line));
  REQUIRE(std::string(line) == std::string(tuning::resolve(0).shortName) + " G");
}

TEST_CASE("status and pitch lines fit the screen for every tuning, tonic and reference",
          "[tuning][tuning-page]")
{
  char line[kLine];
  for (size_t i = 0; i < tuning::libraryCount(); ++i)
    for (uint8_t tonic = 0; tonic < 12; ++tonic)
      for (const float a4 : {415.0f, 432.0f, 440.0f, 466.0f})
      {
        tuning::Selection selection;
        selection.tuningId = tuning::libraryAt(i).id;
        selection.tonic = tonic;
        selection.a4Hz = a4;
        formatStatus(selection, line, sizeof(line));
        REQUIRE(std::strlen(line) <= tuning::kMaxNameLength);
        formatPitchLine(selection, line, sizeof(line));
        REQUIRE(std::strlen(line) <= tuning::kMaxNameLength);
      }
}

TEST_CASE("notices name what just happened", "[tuning][tuning-page]")
{
  char line[kLine];
  Result r;
  r.change = Change::Applied;
  r.tuningId = 96;
  formatNotice(r, line, sizeof(line));
  REQUIRE(std::string(line) == tuning::resolve(96).name); // scale unchanged: the full name

  r.scaleChanged = true;
  r.scaleIndex = scaleNamed("Bhairav Thaat");
  formatNotice(r, line, sizeof(line));
  // 22 Shruti + the full scale name would not fit the row: the short scale name is used.
  REQUIRE(std::string(line) == std::string(tuning::resolve(96).shortName) + " > Bhairav");
  r.tuningId = 1;
  r.scaleIndex = scaleNamed("Maqam Rast");
  formatNotice(r, line, sizeof(line));
  REQUIRE(std::string(line) == "24-EDO > Maqam Rast"); // room for the full name

  r.change = Change::ScaleChosen;
  r.scaleIndex = scaleNamed("Maqam Saba");
  formatNotice(r, line, sizeof(line));
  REQUIRE(std::string(line) == "Maqam Saba");

  r.change = Change::Starred;
  r.slot = 3;
  formatNotice(r, line, sizeof(line));
  REQUIRE(std::string(line) == "Saved to Hot 4");

  r.change = Change::Unstarred;
  formatNotice(r, line, sizeof(line));
  REQUIRE(std::string(line) == "Cleared Hot 4");

  r.change = Change::Empty;
  r.slot = 2;
  formatNotice(r, line, sizeof(line));
  REQUIRE(std::string(line) == "Hot 3 is empty");

  r.change = Change::NoScale;
  r.tuningId = 128;
  formatNotice(r, line, sizeof(line));
  REQUIRE(std::string(line) == "Only 2 scales here");

  r.change = Change::None;
  formatNotice(r, line, sizeof(line));
  REQUIRE(std::string(line).empty());
}

TEST_CASE("every notice a gesture can produce fits the OLED line", "[tuning][tuning-page]")
{
  char line[kLine + 3]; // the notice buffer in UIState is larger than one line: check the text
  // Choosing each tuning from each other tuning announces the new tuning and its scale.
  for (uint8_t from = 0; from < tuning::libraryCount(); ++from)
    for (uint8_t to = 0; to < tuning::libraryCount(); ++to)
    {
      if (from == to) continue;
      Rig rig;
      rig.selection.tuningId = padTuningId(from);
      rig.scaleIndex = tuning::defaultScale(rig.selection.tuningId);
      const Result r = padTap(to, rig.selection, rig.bank, rig.scaleIndex);
      formatNotice(r, line, sizeof(line));
      INFO("from " << int(from) << " to " << int(to) << ": " << line);
      REQUIRE(std::strlen(line) <= tuning::kMaxNameLength);
      REQUIRE(line[0] != '\0');
    }
  // Every scale a button can choose.
  for (size_t i = 0; i < tuning::libraryCount(); ++i)
  {
    Result r;
    r.change = Change::ScaleChosen;
    r.tuningId = tuning::libraryAt(i).id;
    const tuning::ScaleSet set = tuning::scaleSet(r.tuningId);
    for (uint8_t slot = 0; slot < set.count; ++slot)
    {
      r.scaleIndex = set.scales[slot];
      formatNotice(r, line, sizeof(line));
      REQUIRE(std::strlen(line) <= tuning::kMaxNameLength);
    }
  }
}

TEST_CASE("fader value lines", "[tuning][tuning-page]")
{
  char line[kLine];
  tuning::Selection selection;
  selection.tonic = 6;
  selection.a4Hz = 442.0f;
  formatFaderValue(Fader::Tonic, selection, 0, line, sizeof(line));
  REQUIRE(std::string(line) == "Tonic F#");
  formatFaderValue(Fader::Reference, selection, 0, line, sizeof(line));
  REQUIRE(std::string(line) == "A4 442.0Hz");
  selection.tuningId = 1;
  formatFaderValue(Fader::Scale, selection, scaleNamed("Maqam Bayati"), line, sizeof(line));
  REQUIRE(std::string(line) == "Scale 2/5");
  formatFaderValue(Fader::Unassigned, selection, 0, line, sizeof(line));
  REQUIRE(std::string(line).empty());
}

TEST_CASE("text helpers survive tiny and null buffers", "[tuning][tuning-page]")
{
  tuning::Selection selection;
  selection.tuningId = 97;
  char tiny[4];
  formatHeader(selection, tiny, sizeof(tiny));
  REQUIRE(std::strlen(tiny) < sizeof(tiny));
  formatPitchLine(selection, tiny, sizeof(tiny));
  REQUIRE(std::strlen(tiny) < sizeof(tiny));
  formatScaleLine(selection, 0, tiny, sizeof(tiny));
  REQUIRE(std::strlen(tiny) < sizeof(tiny));
  selection.tuningId = 32;
  selection.tonic = 3;
  formatStatus(selection, tiny, sizeof(tiny));
  REQUIRE(std::strlen(tiny) < sizeof(tiny));
  Result r;
  r.change = Change::Applied;
  r.tuningId = 96;
  r.scaleChanged = true;
  formatNotice(r, tiny, sizeof(tiny));
  REQUIRE(std::strlen(tiny) < sizeof(tiny));
  formatFaderValue(Fader::Reference, selection, 0, tiny, sizeof(tiny));
  REQUIRE(std::strlen(tiny) < sizeof(tiny));

  formatHeader(selection, nullptr, 0);
  formatPitchLine(selection, nullptr, 0);
  formatScaleLine(selection, 0, nullptr, 0);
  formatStatus(selection, nullptr, 0);
  formatNotice(r, nullptr, 0);
  formatFaderValue(Fader::Tonic, selection, 0, nullptr, 0);
  char one[1] = {'x'};
  formatStatus(selection, one, 0);
  REQUIRE(one[0] == 'x');
}

// --- LED colour --------------------------------------------------------------------------------

TEST_CASE("every family has its own LED hue", "[tuning][tuning-page]")
{
  for (uint8_t a = 0; a < tuning::kFamilyCount; ++a)
    for (uint8_t b = static_cast<uint8_t>(a + 1); b < tuning::kFamilyCount; ++b)
      REQUIRE(familyHue(a) != familyHue(b));
  // An unknown family falls back to the first hue rather than reading past the table.
  REQUIRE(familyHue(tuning::kFamilyCount) == familyHue(0));
  REQUIRE(familyHue(255) == familyHue(0));
}
