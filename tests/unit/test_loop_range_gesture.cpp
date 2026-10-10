#include <catch2/catch_test_macros.hpp>

#include "ui/LoopRangeGesture.h"

// Two-pad loop gesture: hold a step pad, press another on the same voice row,
// and the pair names the loop; neither pad then acts on release.

using LoopRange::Gesture;
using LoopRange::Pair;

TEST_CASE("A second pad in the anchor's bank pairs with it", "[loop_range]")
{
    Gesture g;
    g.notePress(3); // step 4
    g.notePress(7); // step 8
    const Pair pair = g.pairFor(7);
    REQUIRE(pair.valid);
    CHECK(pair.anchorPad == 3);
    CHECK(pair.otherPad == 7);
    CHECK(g.consumed(3));
    CHECK(g.consumed(7));

    // Both releases are swallowed, then the state clears.
    g.noteRelease(7);
    CHECK_FALSE(g.consumed(7));
    CHECK(g.consumed(3));
    g.noteRelease(3);
    CHECK_FALSE(g.consumed(3));
}

TEST_CASE("A single pad never pairs and is never consumed", "[loop_range]")
{
    Gesture g;
    g.notePress(5);
    CHECK_FALSE(g.pairFor(5).valid);
    CHECK_FALSE(g.consumed(5));
    g.noteRelease(5);
    g.notePress(9);
    CHECK_FALSE(g.pairFor(9).valid); // the earlier anchor is gone
}

TEST_CASE("Pads on different voice banks do not pair", "[loop_range]")
{
    Gesture g;
    g.notePress(2);  // low bank
    g.notePress(20); // high bank
    CHECK_FALSE(g.pairFor(20).valid);
    CHECK_FALSE(g.consumed(2));
    CHECK_FALSE(g.consumed(20));
}

TEST_CASE("Further pads re-pair with the held anchor", "[loop_range]")
{
    Gesture g;
    g.notePress(3);
    g.notePress(7);
    REQUIRE(g.pairFor(7).valid);
    g.noteRelease(7);
    g.notePress(11);
    const Pair pair = g.pairFor(11);
    REQUIRE(pair.valid);
    CHECK(pair.anchorPad == 3);
    CHECK(pair.otherPad == 11);
}

TEST_CASE("Releasing the anchor ends pairing until all pads lift", "[loop_range]")
{
    Gesture g;
    g.notePress(3);
    g.notePress(7);
    REQUIRE(g.pairFor(7).valid);
    g.noteRelease(3);
    g.notePress(9);
    CHECK_FALSE(g.pairFor(9).valid);
    g.noteRelease(9);
    g.noteRelease(7);
    g.notePress(1);
    g.notePress(6);
    CHECK(g.pairFor(6).valid);
}
