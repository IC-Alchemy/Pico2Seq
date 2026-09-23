// Unit tests for the Arpeggiator mode note engine
// (src/pico2seq-core/arpeggiator/): chord and latch semantics, pattern walks,
// rate/gate/swing timing at 480 PPQN, encoder rate turning, lidar dynamics.
// Portable C++ only — no hardware, no Arduino stubs.

#include "pico2seq-core/arpeggiator/Arpeggiator.h"
#include "ui/ControlSurfaceLogic.h" // LedLayout, for the pad/LED geometry pin

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>
#include <vector>

#include "ui/ArpDisplay.h"

using namespace Arpeggiator;
using Catch::Matchers::WithinAbs;

namespace
{
// One note-on the engine reported, with the tick it happened on.
struct Played
{
    uint16_t tick;
    uint8_t slot;
    uint8_t degree;
    uint8_t octave;
};

struct Stopped
{
    uint16_t tick;
    uint8_t slot;
};

// Drive the engine the way ArpPlayback does: one tick per PPQN pulse, notes and
// gates collected for the assertions.
struct Recorder
{
    Engine engine;
    std::vector<Played> notes;
    std::vector<Stopped> stops;
    uint16_t tick = 0;

    void run(uint16_t ticks)
    {
        for (uint16_t i = 0; i < ticks; ++i)
        {
            ++tick;
            const Tick out = engine.tick();
            for (uint8_t slot = 0; slot < kMaxSlots; ++slot)
            {
                if (out.startMask & (1u << slot))
                    notes.push_back({tick, slot, out.degrees[slot], out.octaves[slot]});
                if (out.stopMask & (1u << slot))
                    stops.push_back({tick, slot});
            }
        }
    }
};

std::vector<uint8_t> degrees(const std::vector<Played> &notes)
{
    std::vector<uint8_t> out;
    for (const auto &note : notes)
        out.push_back(note.degree);
    return out;
}

std::vector<uint16_t> ticks(const std::vector<Played> &notes)
{
    std::vector<uint16_t> out;
    for (const auto &note : notes)
        out.push_back(note.tick);
    return out;
}

// A three-note chord (root, third, fifth in scale degrees) pressed in a given
// order, which is what the touch panel produces.
void pressChord(Engine &engine, std::initializer_list<uint8_t> pads)
{
    for (uint8_t pad : pads)
        engine.pressPad(pad);
}
} // namespace

TEST_CASE("Arpeggiator rate table maps to PPQN note divisions", "[arpeggiator]")
{
    CHECK(rateTicks(Rate::Quarter) == 480);
    CHECK(rateTicks(Rate::QuarterTriplet) == 320);
    CHECK(rateTicks(Rate::Eighth) == 240);
    CHECK(rateTicks(Rate::EighthTriplet) == 160);
    CHECK(rateTicks(Rate::Sixteenth) == 120);
    CHECK(rateTicks(Rate::SixteenthTriplet) == 80);
    CHECK(rateTicks(Rate::ThirtySecond) == 60);
    CHECK(rateTicks(Rate::ThirtySecondTriplet) == 40);
    // Every division is slower than the next one down the table.
    for (uint8_t i = 1; i < kRateCount; ++i)
        CHECK(rateTicks(static_cast<Rate>(i)) < rateTicks(static_cast<Rate>(i - 1)));

    for (uint8_t i = 0; i < kRateCount; ++i)
        CHECK(rateName(static_cast<Rate>(i))[0] != '\0');
    for (uint8_t i = 0; i < kPatternCount; ++i)
        CHECK(patternName(static_cast<Pattern>(i))[0] != '\0');
    CHECK(static_cast<uint8_t>(Rate::Count) == 8);
}

TEST_CASE("Arpeggiator pattern buttons and octave cycling", "[arpeggiator]")
{
    CHECK(patternForButtonBit(0) == Pattern::Up);
    CHECK(patternForButtonBit(1) == Pattern::Down);
    CHECK(patternForButtonBit(2) == Pattern::UpDown);
    CHECK(patternForButtonBit(3) == Pattern::Random);
    CHECK(patternForButtonBit(4) == Pattern::Order);
    CHECK(patternForButtonBit(5) == Pattern::Chord);
    // Shift (7) and Latch (6) keep their own meanings.
    CHECK(patternForButtonBit(6) == Pattern::Count);
    CHECK(patternForButtonBit(7) == Pattern::Count);
    CHECK(patternForButtonBit(200) == Pattern::Count);
    CHECK(kPatternCount == 6);

    CHECK(nextOctaves(1) == 2);
    CHECK(nextOctaves(2) == 3);
    CHECK(nextOctaves(3) == 4);
    CHECK(nextOctaves(4) == 1);
    CHECK(nextOctaves(0) == 2); // clamped before cycling
    CHECK(clampOctaves(0) == 1);
    CHECK(clampOctaves(9) == 4);

    // A fader spreads the whole range over 1..4 octaves.
    CHECK(octavesForFader(0.0f) == 1);
    CHECK(octavesForFader(0.2f) == 2);
    CHECK(octavesForFader(0.5f) == 3);
    CHECK(octavesForFader(0.8f) == 3);
    CHECK(octavesForFader(0.9f) == 4);
    CHECK(octavesForFader(1.0f) == 4);
    CHECK(octavesForFader(-1.0f) == 1);
    CHECK(octavesForFader(9.0f) == 4);
    CHECK(octavesForFader(std::numeric_limits<float>::quiet_NaN()) == 1);
}

TEST_CASE("Arpeggiator gate and dynamics clamping is total", "[arpeggiator]")
{
    CHECK(clampGate(0.5f) == 0.5f);
    CHECK(clampGate(-1.0f) == kMinGate);
    CHECK(clampGate(2.0f) == kMaxGate);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(clampGate(nan) == kMinGate);

    Engine engine;
    engine.setGate(0.0f);
    CHECK(engine.settings().gate == kMinGate);
    engine.setGate(1.0f);
    CHECK(engine.settings().gate == kMaxGate);
    engine.setSwing(5.0f);
    CHECK(engine.settings().swing == 1.0f);
    engine.setSwing(-3.0f);
    CHECK(engine.settings().swing == 0.0f);
    engine.setFilter(9.0f);
    CHECK(engine.settings().filter == 1.0f);
    engine.setOctaves(7);
    CHECK(engine.settings().octaves == kMaxOctaves);
}

TEST_CASE("Arpeggiator slot voices spread the chord over the instrument", "[arpeggiator]")
{
    // Slot 0 is always the voice the player selected for the arp.
    for (uint8_t voice = 0; voice < 4; ++voice)
    {
        CHECK(slotVoiceIndex(0, voice) == voice);
        CHECK(slotVoiceIndex(1, voice) != voice);
        CHECK(slotVoiceIndex(2, voice) != voice);
        CHECK(slotVoiceIndex(3, voice) != voice);
        // Slots 1..3 cover every other voice exactly once.
        bool seen[4] = {false, false, false, false};
        for (uint8_t slot = 1; slot < kMaxSlots; ++slot)
        {
            const uint8_t other = slotVoiceIndex(slot, voice);
            CHECK(other < 4);
            CHECK_FALSE(seen[other]);
            seen[other] = true;
        }
    }
    CHECK(slotVoiceIndex(0, 2) == 2);
    CHECK(slotVoiceIndex(1, 2) == 3);
    CHECK(slotVoiceIndex(2, 2) == 0);
    CHECK(slotVoiceIndex(3, 2) == 1);
    // An out-of-range selection still lands on a real voice.
    CHECK(slotVoiceIndex(0, 9) == 1);
}

TEST_CASE("Arpeggiator pads paint their own LED", "[arpeggiator]")
{
    // The LED panel mirrors the pads. LedLayout expresses that as (band, step),
    // where band = row/2 and step = (row%2)*8 + col for the pad's own (row, col);
    // this pins the reduction ArpPlayback and the LED renderer rely on.
    for (uint8_t pad = 0; pad < kPadCount; ++pad)
    {
        const uint8_t row = static_cast<uint8_t>(pad / 8);
        const uint8_t col = static_cast<uint8_t>(pad % 8);
        const int linear = ControlSurface::LedLayout::linearIndex(
            static_cast<uint8_t>(row / 2), static_cast<uint8_t>((row % 2) * 8 + col));
        CHECK(linear == pad);
        CHECK(ledIndexForPad(pad) == linear);
    }
    CHECK(ledIndexForPad(kPadCount) == -1);
}

TEST_CASE("Arpeggiator chord tracks presses, order and names", "[arpeggiator]")
{
    Engine engine;
    CHECK(engine.chordCount() == 0);
    CHECK_FALSE(engine.padInChord(4));
    CHECK(engine.chordDegree(0) == kNoDegree);

    // Pressed out of pitch order: the sorted walk and the as-played walk differ.
    pressChord(engine, {7, 0, 4});
    CHECK(engine.chordCount() == 3);
    CHECK(engine.chordDegree(0) == 0);
    CHECK(engine.chordDegree(1) == 4);
    CHECK(engine.chordDegree(2) == 7);
    CHECK(engine.chordDegree(3) == kNoDegree);
    CHECK(engine.orderDegree(0) == 7);
    CHECK(engine.orderDegree(1) == 0);
    CHECK(engine.orderDegree(2) == 4);
    CHECK(engine.padInChord(0));
    CHECK(engine.padHeld(0));
    CHECK_FALSE(engine.padInChord(1));

    // A repeat press must not duplicate the entry.
    engine.pressPad(4);
    CHECK(engine.chordCount() == 3);

    engine.releasePad(0);
    CHECK(engine.chordCount() == 2);
    CHECK(engine.orderDegree(0) == 7);
    CHECK(engine.orderDegree(1) == 4);
    // A release with no press behind it, and an out-of-range pad, are no-ops.
    engine.releasePad(0);
    engine.pressPad(kPadCount);
    engine.releasePad(200);
    CHECK(engine.chordCount() == 2);

    engine.clearChord();
    CHECK(engine.chordCount() == 0);
    CHECK(engine.orderDegree(0) == kNoDegree);
    CHECK_FALSE(engine.padInChord(7));
}

TEST_CASE("Arpeggiator latch holds notes and starts the next chord clean", "[arpeggiator]")
{
    Engine engine;
    CHECK_FALSE(engine.latchEnabled());
    engine.setLatch(true);
    CHECK(engine.latchEnabled());

    pressChord(engine, {0, 4});
    engine.releasePad(0);
    engine.releasePad(4);
    // The latch keeps both notes, so nothing is left on the panel.
    CHECK(engine.chordCount() == 2);
    CHECK(engine.padInChord(0));
    CHECK_FALSE(engine.padHeld(0));

    // The classic gesture: with nothing held, the next press starts a new chord.
    engine.pressPad(7);
    CHECK(engine.chordCount() == 1);
    CHECK(engine.chordDegree(0) == 7);
    engine.releasePad(7);
    CHECK(engine.chordCount() == 1); // and the latch takes the new note

    // Turning the latch off lets the latched notes go.
    engine.toggleLatch();
    CHECK(engine.chordCount() == 0);

    // Note added with a finger still down: the two notes share the chord, and
    // turning the latch off lets only the latched one go.
    engine.pressPad(3);
    engine.setLatch(true);
    engine.pressPad(5);
    engine.releasePad(3);
    engine.releasePad(5);
    CHECK(engine.chordCount() == 2);
    CHECK(engine.chordDegree(0) == 3);
    CHECK(engine.chordDegree(1) == 5);

    engine.pressPad(9);
    engine.setLatch(false); // 9 is under a finger, 3 and 5 are latched
    CHECK(engine.chordCount() == 1);
    CHECK(engine.chordDegree(0) == 9);
    CHECK(engine.orderDegree(1) == kNoDegree);
}

TEST_CASE("Arpeggiator randomize chord stays in range and latches", "[arpeggiator]")
{
    Engine engine;
    engine.randomizeChord(4, 0xC0FFEEu);
    CHECK(engine.chordCount() == 4);
    CHECK(engine.latchEnabled());
    for (uint8_t i = 0; i < 4; ++i)
    {
        const uint8_t degree = engine.chordDegree(i);
        REQUIRE(degree < kPadCount);
        if (i > 0)
            CHECK(degree > engine.chordDegree(static_cast<uint8_t>(i - 1)));
    }
    // The as-played order matches the panel's ascending display.
    CHECK(engine.orderDegree(0) == engine.chordDegree(0));

    // Deterministic for a given seed, and different across seeds.
    Engine other;
    other.randomizeChord(4, 0xC0FFEEu);
    CHECK(engine.chordDegree(0) == other.chordDegree(0));
    CHECK(engine.chordDegree(3) == other.chordDegree(3));
    Engine third;
    third.randomizeChord(4, 0x1234u);
    CHECK(third.chordCount() == 4);

    // A nonsense size is clamped instead of corrupting the chord.
    Engine one;
    one.randomizeChord(0, 7u);
    CHECK(one.chordCount() == 1);
    Engine all;
    all.randomizeChord(99, 9u);
    CHECK(all.chordCount() == kPadCount);
}

TEST_CASE("Arpeggiator walks the chord in every pattern", "[arpeggiator]")
{
    SECTION("Up climbs the octave range after the whole chord")
    {
        Recorder rec;
        rec.engine.setActive(true);
        rec.engine.setRate(Rate::Sixteenth);
        rec.engine.setOctaves(2);
        rec.engine.setPattern(Pattern::Up);
        pressChord(rec.engine, {0, 4, 7});
        rec.run(6 * 120);
        REQUIRE(rec.notes.size() >= 6);
        CHECK(degrees(rec.notes) == std::vector<uint8_t>{0, 4, 7, 0, 4, 7});
        CHECK(rec.notes[0].octave == 0);
        CHECK(rec.notes[2].octave == 0);
        CHECK(rec.notes[3].octave == 1); // the second pass is an octave up
        CHECK(rec.notes[5].octave == 1);
    }

    SECTION("Down starts high and walks back")
    {
        Recorder rec;
        rec.engine.setActive(true);
        rec.engine.setPattern(Pattern::Down);
        rec.engine.setOctaves(2);
        pressChord(rec.engine, {0, 4, 7});
        rec.run(6 * 120);
        REQUIRE(rec.notes.size() >= 6);
        CHECK(degrees(rec.notes) == std::vector<uint8_t>{7, 4, 0, 7, 4, 0});
        CHECK(rec.notes[0].octave == 1);
        CHECK(rec.notes[2].octave == 1);
        CHECK(rec.notes[3].octave == 0);
    }

    SECTION("Up-Down turns without repeating the turning points")
    {
        Recorder rec;
        rec.engine.setActive(true);
        rec.engine.setPattern(Pattern::UpDown);
        pressChord(rec.engine, {0, 4, 7});
        rec.run(8 * 120);
        REQUIRE(rec.notes.size() >= 8);
        CHECK(degrees(rec.notes) == std::vector<uint8_t>{0, 4, 7, 4, 0, 4, 7, 4});
    }

    SECTION("Order plays the pads as they were pressed")
    {
        Recorder rec;
        rec.engine.setActive(true);
        rec.engine.setPattern(Pattern::Order);
        pressChord(rec.engine, {7, 0, 4});
        rec.run(3 * 120);
        REQUIRE(rec.notes.size() >= 3);
        CHECK(degrees(rec.notes) == std::vector<uint8_t>{7, 0, 4});
    }

    SECTION("Chord sounds up to four notes at once and climbs by chord")
    {
        Recorder rec;
        rec.engine.setActive(true);
        rec.engine.setPattern(Pattern::Chord);
        rec.engine.setOctaves(2);
        pressChord(rec.engine, {0, 4, 7});
        rec.run(120);
        REQUIRE(rec.notes.size() == 3);
        CHECK(rec.notes[0].slot == 0);
        CHECK(rec.notes[0].degree == 0);
        CHECK(rec.notes[1].slot == 1);
        CHECK(rec.notes[1].degree == 4);
        CHECK(rec.notes[2].slot == 2);
        CHECK(rec.notes[2].degree == 7);
        for (const auto &note : rec.notes)
            CHECK(note.octave == 0);

        rec.notes.clear();
        rec.run(120);
        REQUIRE(rec.notes.size() == 3);
        for (const auto &note : rec.notes)
            CHECK(note.octave == 1); // the range steps once per chord
    }

    SECTION("Chord caps at the four voices")
    {
        Recorder rec;
        rec.engine.setActive(true);
        rec.engine.setPattern(Pattern::Chord);
        pressChord(rec.engine, {0, 1, 2, 3, 4, 5});
        rec.run(120);
        REQUIRE(rec.notes.size() == kMaxSlots);
        CHECK(degrees(rec.notes) == std::vector<uint8_t>{0, 1, 2, 3});
    }

    SECTION("Random stays inside the chord and octave range")
    {
        Recorder rec;
        rec.engine.setActive(true);
        rec.engine.setPattern(Pattern::Random);
        rec.engine.setOctaves(2);
        rec.engine.seedRandom(12345u);
        pressChord(rec.engine, {0, 4, 7});
        rec.run(20 * 120);
        REQUIRE(rec.notes.size() >= 20);
        bool sawOtherThanRoot = false;
        for (const auto &note : rec.notes)
        {
            const bool inChord = note.degree == 0 || note.degree == 4 || note.degree == 7;
            CHECK(inChord);
            CHECK(note.octave < 2);
            if (note.degree != 0)
                sawOtherThanRoot = true;
        }
        CHECK(sawOtherThanRoot);
    }
}

TEST_CASE("Arpeggiator repeats the same walk for the same seed", "[arpeggiator]")
{
    Recorder first;
    first.engine.setActive(true);
    first.engine.setPattern(Pattern::Random);
    first.engine.seedRandom(4242u);
    pressChord(first.engine, {2, 5, 9, 12});
    first.run(12 * 120);

    Recorder second;
    second.engine.setActive(true);
    second.engine.setPattern(Pattern::Random);
    second.engine.seedRandom(4242u);
    pressChord(second.engine, {2, 5, 9, 12});
    second.run(12 * 120);

    REQUIRE(first.notes.size() >= 12);
    CHECK(degrees(first.notes) == degrees(second.notes));
}

TEST_CASE("Arpeggiator lands notes and gates exactly on the PPQN grid", "[arpeggiator]")
{
    Recorder rec;
    rec.engine.setActive(true);
    rec.engine.setRate(Rate::Sixteenth); // 120 ticks = a 16th at 480 PPQN
    rec.engine.setGate(0.5f);
    pressChord(rec.engine, {0, 4, 7});

    // A fresh engine (and a restarted one) plays on the very first tick.
    rec.run(1);
    REQUIRE(rec.notes.size() == 1);
    CHECK(rec.notes[0].tick == 1);
    CHECK(rec.stops.empty());

    rec.run(240); // through tick 241, where the third note lands
    REQUIRE(rec.notes.size() == 3);
    CHECK(ticks(rec.notes) == std::vector<uint16_t>{1, 121, 241});
    // 50% gate on a 120-tick interval ends 60 ticks after each note-on.
    REQUIRE(rec.stops.size() == 2);
    CHECK(rec.stops[0].tick == 61);
    CHECK(rec.stops[0].slot == 0);
    CHECK(rec.stops[1].tick == 181);
    CHECK(rec.engine.lastGateTicks() == 60);
    // The note that started on tick 241 is still gated: its stop falls 60 ticks
    // later, and the engine reports a gate-off on the tick it actually ends.
    uint8_t gatedDegree = 0;
    uint8_t gatedOctave = 0;
    CHECK(rec.engine.slotSounding(0, gatedDegree, gatedOctave));
    rec.run(60);
    REQUIRE(rec.stops.size() == 3);
    CHECK(rec.stops[2].tick == 301);
    CHECK_FALSE(rec.engine.slotSounding(0, gatedDegree, gatedOctave));
}

TEST_CASE("Arpeggiator keeps the beat through rests and rate changes", "[arpeggiator]")
{
    SECTION("An empty chord leaves rests, and the next note lands in time")
    {
        Recorder rec;
        rec.engine.setActive(true);
        rec.engine.setRate(Rate::Sixteenth);
        rec.run(60); // no chord yet
        CHECK(rec.notes.empty());
        CHECK(rec.engine.stepCount() == 0);
        pressChord(rec.engine, {0});
        rec.run(61);
        // The walk was already running, so the note lands where the grid says
        // (tick 121), not on the press at tick 60.
        REQUIRE(rec.notes.size() == 1);
        CHECK(rec.notes[0].tick == 121);
    }

    SECTION("A faster rate takes effect without waiting out the old division")
    {
        Recorder rec;
        rec.engine.setActive(true);
        rec.engine.setRate(Rate::Quarter); // 480 ticks between notes
        pressChord(rec.engine, {0});
        rec.run(2);
        REQUIRE(rec.notes.size() == 1);
        rec.engine.setRate(Rate::Eighth); // 240 ticks
        // The new interval is measured from the rate change, so the note lands
        // 240 ticks later instead of at the old 480-tick deadline.
        rec.run(239);
        REQUIRE(rec.notes.size() == 1);
        rec.run(1);
        REQUIRE(rec.notes.size() == 2);
        CHECK(rec.notes[1].tick == 242);
    }

    SECTION("A chord that empties out stops scheduling notes but not the grid")
    {
        Recorder rec;
        rec.engine.setActive(true);
        rec.engine.setRate(Rate::Sixteenth);
        pressChord(rec.engine, {0});
        rec.run(120); // one note, then the gate closes
        REQUIRE(rec.notes.size() == 1);
        rec.engine.releasePad(0);
        rec.run(360);
        uint8_t degree = 0;
        uint8_t octave = 0;
        CHECK(rec.notes.size() == 1);
        CHECK(rec.stops.size() == 1);
        CHECK_FALSE(rec.engine.slotSounding(0, degree, octave));
    }
}

TEST_CASE("Arpeggiator swing delays every second note without drifting", "[arpeggiator]")
{
    Recorder rec;
    rec.engine.setActive(true);
    rec.engine.setRate(Rate::Sixteenth); // interval 120
    rec.engine.setSwing(1.0f);           // full swing: half an interval
    pressChord(rec.engine, {0, 4});

    rec.run(4 * 120);
    REQUIRE(rec.notes.size() >= 4);
    // longest gap 179, shortest 61: a pair still totals two intervals, so the
    // notes stay on the transport's grid.
    CHECK(ticks(rec.notes) == std::vector<uint16_t>{1, 180, 241, 420});

    // Off by default: without swing the gaps are exactly one interval.
    Recorder straight;
    straight.engine.setActive(true);
    straight.engine.setRate(Rate::Sixteenth);
    pressChord(straight.engine, {0, 4});
    straight.run(4 * 120);
    REQUIRE(straight.notes.size() >= 4);
    CHECK(ticks(straight.notes) == std::vector<uint16_t>{1, 121, 241, 361});
}

TEST_CASE("Arpeggiator rate turning keeps slow turns and reversals honest", "[arpeggiator]")
{
    Engine engine;
    engine.setRate(Rate::Eighth);
    // Halves of a detent: exact in binary floating point, so this pins the
    // accumulator rather than a rounding accident.
    const float detent = 0.02f;
    const float half = 0.01f;
    // Motion below a detent accumulates instead of being thrown away.
    engine.turnRate(half, detent);
    CHECK(engine.settings().rate == Rate::Eighth);
    engine.turnRate(half, detent); // exactly one detent
    CHECK(engine.settings().rate == Rate::EighthTriplet);

    // A reversal answers immediately rather than unwinding the other way first:
    // 0.015 pending the other way would have swallowed this detent.
    engine.turnRate(-0.75f * detent, detent);
    CHECK(engine.settings().rate == Rate::EighthTriplet);
    engine.turnRate(detent, detent);
    CHECK(engine.settings().rate == Rate::Sixteenth);

    // Bigger turns move several divisions and stop at the ends of the table.
    engine.turnRate(1.0f, detent);
    CHECK(engine.settings().rate == Rate::ThirtySecondTriplet);
    engine.turnRate(1.0f, detent);
    CHECK(engine.settings().rate == Rate::ThirtySecondTriplet);
    engine.turnRate(-9.0f, detent);
    CHECK(engine.settings().rate == Rate::Quarter);
    // Junk input is ignored, not propagated into the rate.
    engine.turnRate(std::numeric_limits<float>::quiet_NaN(), detent);
    engine.turnRate(0.1f, 0.0f);
    CHECK(engine.settings().rate == Rate::Quarter);
    CHECK(rateTicks(engine.settings().rate) == 480);
}

TEST_CASE("Arpeggiator dynamics scale the patch velocity from the lidar", "[arpeggiator]")
{
    Engine engine;
    // No hand: the patch's own velocity, unattenuated.
    CHECK_FALSE(engine.handInRange());
    CHECK(engine.dynamics() == 1.0f);
    CHECK(engine.velocityScale() == 1.0f);

    engine.observeDynamics(true, 0.0f); // hand right at the sensor
    CHECK(engine.handInRange());
    CHECK_THAT(engine.velocityScale(), WithinAbs(0.25f, 0.0001f));
    engine.observeDynamics(true, 1.0f); // hand raised
    CHECK_THAT(engine.velocityScale(), WithinAbs(1.0f, 0.0001f));
    engine.observeDynamics(true, 0.5f);
    CHECK_THAT(engine.dynamics(), WithinAbs(0.5f, 0.0001f));
    CHECK_THAT(engine.velocityScale(), WithinAbs(0.625f, 0.0001f));

    // Out-of-window and NaN heights cannot leave the playable range.
    engine.observeDynamics(true, 4.0f);
    CHECK(engine.dynamics() == 1.0f);
    engine.observeDynamics(true, -4.0f);
    CHECK(engine.dynamics() == 0.0f);
    engine.observeDynamics(true, std::numeric_limits<float>::quiet_NaN());
    CHECK(engine.dynamics() == 0.0f);
    engine.observeDynamics(false, 0.0f);
    CHECK_FALSE(engine.handInRange());
    CHECK(engine.velocityScale() == 1.0f);
}

TEST_CASE("Arpeggiator restart and mode changes never strand a note", "[arpeggiator]")
{
    Recorder rec;
    rec.engine.setActive(true);
    rec.engine.setRate(Rate::Quarter);
    pressChord(rec.engine, {0, 4});
    rec.run(1);
    REQUIRE(rec.notes.size() == 1);
    uint8_t gatedDegree = 0;
    uint8_t gatedOctave = 0;
    CHECK(rec.engine.slotSounding(0, gatedDegree, gatedOctave));
    CHECK(rec.engine.lastDegree() == 0);
    CHECK(rec.engine.lastOctave() == 0);

    // Re-sync: the walk goes back to the chord root and the sounding note is
    // reported for gate-off on the next tick.
    rec.engine.restart();
    CHECK(rec.engine.stepCount() == 0);
    rec.notes.clear();
    rec.stops.clear();
    rec.run(1);
    REQUIRE(rec.stops.size() == 1);
    CHECK(rec.stops[0].slot == 0);
    REQUIRE(rec.notes.size() == 1);
    CHECK(rec.notes[0].degree == 0);
    CHECK(rec.notes[0].octave == 0);
    CHECK(rec.engine.stepCount() == 1);

    // The mode's own edges reset everything performance-shaped, so nothing
    // bleeds into the next session or back into the sequencer.
    rec.engine.observeDynamics(true, 0.4f);
    rec.engine.setPattern(Pattern::Chord);
    rec.engine.setOctaves(3);
    rec.engine.setLatch(true);
    rec.engine.setActive(false);
    uint8_t degree = 0;
    uint8_t octave = 0;
    CHECK_FALSE(rec.engine.active());
    CHECK(rec.engine.chordCount() == 0);
    CHECK_FALSE(rec.engine.latchEnabled());
    CHECK_FALSE(rec.engine.slotSounding(0, degree, octave));
    CHECK(rec.engine.stepCount() == 0);
    CHECK(rec.engine.lastDegree() == kNoDegree);
    // Settings survive the mode change; they are preferences, not performance
    // state. Dynamics do not (they describe a hand that is no longer there).
    CHECK(rec.engine.settings().pattern == Pattern::Chord);
    CHECK(rec.engine.settings().octaves == 3);
    rec.notes.clear();
    rec.stops.clear();
    rec.run(480);
    CHECK(rec.notes.empty());
    CHECK(rec.stops.empty());

    // Re-entering starts from the top, silent until a note is due.
    rec.engine.setActive(true);
    CHECK(rec.engine.active());
    rec.run(3);
    CHECK(rec.notes.empty());
}

TEST_CASE("Arpeggiator drops held pads but keeps latched ones", "[arpeggiator]")
{
    Engine engine;
    pressChord(engine, {1, 5});
    engine.setLatch(true);
    engine.releasePad(1);
    engine.releasePad(5); // both latched, nothing held
    engine.pressPad(9);   // the next press starts a new chord: 9 is under a finger
    CHECK(engine.chordCount() == 1);

    // A modal state swallowed the release of pad 9: the engine must let it go
    // rather than believe the finger is still down.
    engine.releaseAllHeldPads();
    CHECK(engine.chordCount() == 0);
    CHECK_FALSE(engine.padHeld(9));
    CHECK_FALSE(engine.padInChord(9));

    // Latched notes are not held and survive.
    engine.pressPad(2);
    engine.pressPad(6);
    engine.releasePad(2);
    engine.releasePad(6);
    REQUIRE(engine.chordCount() == 2);
    engine.releaseAllHeldPads();
    CHECK(engine.chordCount() == 2);
    CHECK(engine.chordDegree(0) == 2);
    CHECK(engine.chordDegree(1) == 6);
    CHECK(engine.orderDegree(1) == 6);
}

TEST_CASE("Rhythm grids keep the requested hit count through every rotation", "[arpeggiator][rhythm]")
{
    for (uint8_t length = 1; length <= kMaxRhythmSteps; ++length)
        for (uint8_t hits = 0; hits <= length; ++hits)
            for (uint8_t rotate = 0; rotate < length; ++rotate) {
                unsigned count = 0;
                for (uint8_t step = 0; step < length; ++step) {
                    count += rhythmHit(step, hits, length, rotate);
                    CHECK(rhythmHit(step, hits, length, rotate) ==
                          rhythmHit((step + length - rotate) % length, hits, length, 0));
                }
                CHECK(count == hits);
                if (hits) CHECK(rhythmHit(rotate, hits, length, rotate));
            }
    CHECK_FALSE(rhythmHit(0, 1, 0, 0));
    CHECK_FALSE(rhythmHit(16, 8, 16, 0));
}

TEST_CASE("Tresillo leaves clocked rests and the note walk continues through them", "[arpeggiator][rhythm]")
{
    Recorder rec;
    rec.engine.setActive(true);
    pressChord(rec.engine, {0, 2, 4, 6});
    rec.engine.setRhythmPreset(2);
    rec.run(961);
    CHECK(ticks(rec.notes) == std::vector<uint16_t>{1, 361, 721, 961});
    CHECK(degrees(rec.notes) == std::vector<uint8_t>{0, 6, 4, 0});
    CHECK(rec.engine.rhythmStep() == 0);
    CHECK(rec.engine.stepCount() == 4);
    REQUIRE(rec.stops.size() == 3);
    CHECK(rec.stops[0].tick == 61);
    CHECK(rec.stops[1].tick == 421);
    CHECK(rec.stops[2].tick == 781);
}

TEST_CASE("Silent rhythm keeps phase and editing does not shorten a sounding gate", "[arpeggiator][rhythm]")
{
    Recorder rec;
    rec.engine.setActive(true);
    rec.engine.pressPad(0);
    rec.run(1);
    const uint16_t lastGate = rec.engine.lastGateTicks();
    rec.engine.setRhythm(0, 8, 0);
    rec.run(300);
    REQUIRE(rec.notes.size() == 1);
    REQUIRE(rec.stops.size() == 1);
    CHECK(rec.stops[0].tick == 61);
    CHECK(rec.engine.lastGateTicks() == lastGate);
    CHECK(rec.engine.rhythmStep() == 2);
    rec.engine.setRhythm(8, 8, 0);
    rec.run(60);
    REQUIRE(rec.notes.size() == 2);
    CHECK(rec.notes[1].tick == 361);
    CHECK(rec.engine.rhythmStep() == 3);
}

TEST_CASE("Swing and sparse chords keep distinct gates on the transport grid", "[arpeggiator][rhythm]")
{
    Recorder rec;
    rec.engine.setActive(true);
    rec.engine.setPattern(Pattern::Chord);
    pressChord(rec.engine, {0, 2, 4, 6});
    rec.engine.setRhythm(3, 8, 0);
    rec.engine.setSwing(1.0f);
    rec.engine.setGate(0.95f);
    rec.run(961);
    REQUIRE(rec.notes.size() == 16);
    CHECK(rec.notes[0].tick == 1);
    CHECK(rec.notes[4].tick == 420);
    CHECK(rec.notes[8].tick == 721);
    CHECK(rec.notes[12].tick == 961);
    REQUIRE(rec.stops.size() == 12);
    CHECK(rec.stops[0].tick < rec.notes[4].tick);
    CHECK(rec.stops[4].tick < rec.notes[8].tick);
    CHECK(rec.stops[8].tick < rec.notes[12].tick);
}

TEST_CASE("Accents and hand dynamics are captured together at note start", "[arpeggiator][rhythm]")
{
    Engine e;
    e.setActive(true);
    e.pressPad(0);
    e.setRhythm(4, 8, 1);
    e.setAccent(1.0f);
    e.observeDynamics(true, 0.0f);
    CHECK_FALSE(e.tick().started());
    for (int i = 0; i < 120; ++i) e.tick();
    CHECK_THAT(e.lastVelocityScale(), WithinAbs(0.25f, 0.0001f));
    e.observeDynamics(false, 1.0f);
    CHECK_THAT(e.lastVelocityScale(), WithinAbs(0.25f, 0.0001f));
    for (int i = 0; i < 240; ++i) e.tick();
    CHECK_THAT(e.lastVelocityScale(), WithinAbs(0.25f, 0.0001f));
    e.setAccent(0.0f);
    for (int i = 0; i < 240; ++i) e.tick();
    CHECK_THAT(e.lastVelocityScale(), WithinAbs(1.0f, 0.0001f));
}

TEST_CASE("Rhythm presets and faders remain bounded and survive mode changes", "[arpeggiator][rhythm]")
{
    Engine e;
    for (uint8_t i = 0; i < kRhythmPresetCount; ++i) {
        e.setRhythmPreset(i);
        CHECK(e.rhythmPreset() == i);
    }
    e.setRhythm(255, 255, 255);
    CHECK(e.settings().hits == 16);
    CHECK(e.settings().length == 16);
    CHECK(e.settings().rotation == 15);
    e.setRhythmFader(1, 0.0f);
    CHECK(e.settings().hits == 1);
    CHECK(e.settings().length == 1);
    CHECK(e.settings().rotation == 0);
    e.setRhythmFader(0, 0.0f);
    CHECK(e.settings().hits == 0);
    e.setRhythmFader(1, 1.0f);
    e.setRhythmFader(0, 1.0f);
    e.setRhythmFader(2, 1.0f);
    e.setRhythmFader(3, 1.0f);
    e.setActive(true);
    e.tick();
    e.restart();
    CHECK_FALSE(e.hasRhythmStep());
    e.tick();
    CHECK(e.rhythmStep() == 0);
    e.setActive(false);
    e.setActive(true);
    CHECK_FALSE(e.hasRhythmStep());
    CHECK(e.settings().hits == 16);
    CHECK(e.settings().rotation == 15);
    CHECK(e.settings().accent == 1.0f);
}

TEST_CASE("OLED chord rows never wrap and report the omitted note count", "[arpeggiator][display]")
{
    Engine e;
    ArpDisplay::Row row;
    ArpDisplay::chord(e, nullptr, row);
    CHECK(std::string(row) == "Touch pads for chord");
    pressChord(e, {0, 4, 7});
    ArpDisplay::chord(e, nullptr, row);
    CHECK(std::string(row) == "KEYS C3 E3 G3");
    for (uint8_t pad = 0; pad < 32; ++pad) e.pressPad(pad);
    ArpDisplay::chord(e, nullptr, row);
    CHECK(std::string(row) == "KEYS C3 C#3 D3 +29");
    CHECK(std::strlen(row) <= ArpDisplay::kColumns);
    e.clearChord();
    pressChord(e, {7, 0, 4});
    e.setPattern(Pattern::Order);
    ArpDisplay::chord(e, nullptr, row);
    CHECK(std::string(row) == "KEYS G3 C3 E3");
    ArpDisplay::fit("A deliberately long preset name", row);
    CHECK(std::strlen(row) == ArpDisplay::kColumns);
    CHECK(row[20] == '~');
}

TEST_CASE("OLED gate durations use the engine's quantized swing intervals", "[arpeggiator][display]")
{
    Engine e;
    e.setActive(true);
    e.pressPad(0);
    e.setSwing(1.0f);
    e.tick();
    CHECK(e.lastGateTicks() == gateTicks(e.settings(), true));
    for (int i = 0; i < 179; ++i) e.tick();
    CHECK(e.lastGateTicks() == gateTicks(e.settings(), false));
    ArpDisplay::Row row;
    ArpDisplay::gate(e.settings(), 120, row);
    CHECK(std::string(row) == "32-94ms");
    CHECK(ArpDisplay::swingLong(e.settings()) == 75);
}
