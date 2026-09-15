// Unit tests for src/AlchemyUI/src/SatelliteLink.h — the three robustness
// mechanisms that stand between a flaky PY32 satellite and the audio engine:
// sequence counter, timeout, last-known-good.
//
// The failures these exist for, all of which the hub used to have:
//   - a tile that dropped off the bus kept its buttons latched "held",
//     pinning Shift or a parameter-record arm indefinitely;
//   - a tile that dropped off the bus reported its faders as 0, and utility
//     fader 2 is master volume, so a handful of NACKs silenced the
//     instrument;
//   - a frame that failed its checksum could still contribute a field.
//
// Pure C++, no Arduino, no Wire: milliseconds arrive as arguments.

#include "AlchemyUI/src/SatelliteLink.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace ap = alchemy;

namespace
{

/** A packet as a healthy satellite would serve it. */
ap::StatePacket packet(std::uint8_t seq, std::uint8_t buttons,
                       std::uint16_t slider0 = 0)
{
    ap::StatePacket p;
    p.seq = seq;
    p.buttons = buttons;
    p.sliders[0] = slider0;
    p.status = ap::kStatusHeartbeat;
    return p;
}

ap::SatelliteLink freshLink(std::uint32_t timeoutMs = 100)
{
    ap::SatelliteLink link;
    ap::SatelliteLink::Options opt;
    opt.timeoutMs = timeoutMs;
    link.begin(opt, 0);
    return link;
}

} // namespace

// ---------------------------------------------------------------------------
// Mechanism 1: the sequence counter
// ---------------------------------------------------------------------------

TEST_CASE("a link publishes nothing before its first packet", "[satellite_link]")
{
    ap::SatelliteLink link = freshLink();
    CHECK(link.state() == ap::SatelliteLink::State::Init);
    CHECK(link.buttons() == 0);
    CHECK(link.slider(0) == 0);
    CHECK(link.goodPackets() == 0);

    // Init never ages into Stale: there is nothing cached to distrust.
    link.tick(10'000);
    CHECK(link.state() == ap::SatelliteLink::State::Init);
}

TEST_CASE("the first packet always publishes, whatever its SEQ", "[satellite_link]")
{
    ap::SatelliteLink link = freshLink();
    // SEQ 0 matches the zero-initialized cache, so only the "first packet"
    // rule makes this publish. Without it a satellite that boots at SEQ 0 is
    // invisible until its state changes.
    CHECK(link.onPacket(packet(0, 0x05, 2048), 1));
    CHECK(link.buttons() == 0x05);
    CHECK(link.slider(0) == 2048);
    CHECK(link.fresh());
}

TEST_CASE("an unchanged SEQ is liveness, not new state", "[satellite_link]")
{
    ap::SatelliteLink link = freshLink();
    REQUIRE(link.onPacket(packet(4, 0x01, 100), 0));

    // Same snapshot, re-read. Nothing to publish.
    CHECK_FALSE(link.onPacket(packet(4, 0x01, 100), 5));
    CHECK(link.duplicatePackets() == 1);
    CHECK(link.goodPackets() == 2);
    CHECK(link.fresh());

    // But it did refresh the timeout: the satellite is demonstrably alive.
    link.tick(99);
    CHECK(link.fresh());
}

TEST_CASE("SEQ is compared for equality, so the wrap is just another change", "[satellite_link]")
{
    ap::SatelliteLink link = freshLink();
    REQUIRE(link.onPacket(packet(15, 0x01), 0));
    CHECK(link.onPacket(packet(0, 0x02), 4)); // 15 -> 0 is a change
    CHECK(link.buttons() == 0x02);
}

TEST_CASE("a duplicate SEQ still takes the satellite's status flags", "[satellite_link]")
{
    // HEARTBEAT toggles every sweep and a fault can raise without the DATA
    // block changing, so status must track even when the snapshot does not.
    ap::SatelliteLink link = freshLink();
    REQUIRE(link.onPacket(packet(7, 0x03, 500), 0));
    CHECK_FALSE(link.localFault());

    ap::StatePacket faulted = packet(7, 0x03, 500);
    faulted.status = ap::kStatusLocalFault;
    CHECK_FALSE(link.onPacket(faulted, 4)); // no new state to publish...
    CHECK(link.localFault());               // ...but the fault is visible
    CHECK(link.slider(0) == 500);           // and the cache is untouched
}

// ---------------------------------------------------------------------------
// Mechanism 2: the timeout
// ---------------------------------------------------------------------------

TEST_CASE("a link goes stale exactly at the timeout", "[satellite_link]")
{
    ap::SatelliteLink link = freshLink(100);
    REQUIRE(link.onPacket(packet(1, 0x0F, 3000), 0));

    link.tick(99);
    CHECK(link.fresh());

    link.tick(100);
    CHECK(link.stale());
    CHECK(link.staleEvents() == 1);

    // Staying stale is not a new event; the counter is for diagnostics.
    link.tick(5000);
    CHECK(link.staleEvents() == 1);
}

TEST_CASE("failed reads do not themselves make a link stale", "[satellite_link]")
{
    // Only the clock decides. A burst of NACKs inside the timeout leaves the
    // cache trusted, which is what lets a single flaky transaction pass
    // without a visible glitch.
    ap::SatelliteLink link = freshLink(100);
    REQUIRE(link.onPacket(packet(1, 0x0F, 3000), 0));

    for (std::uint32_t t = 10; t < 100; t += 10) link.onFailure(t);
    CHECK(link.fresh());
    CHECK(link.buttons() == 0x0F);
    CHECK(link.rejectedReads() == 9);

    link.onFailure(100);
    CHECK(link.stale());
}

TEST_CASE("a stale link releases its buttons but holds its faders", "[satellite_link]")
{
    ap::SatelliteLink link = freshLink(100);
    ap::StatePacket p = packet(1, 0xFF, 4095);
    p.sliders[1] = 2048;
    p.sliders[2] = 1024;
    p.sliders[3] = 7;
    REQUIRE(link.onPacket(p, 0));

    link.tick(100);
    REQUIRE(link.stale());

    // Buttons are momentary: a bitmap frozen by a dead link is a stuck key.
    CHECK(link.buttons() == 0);
    // Faders are positions: the physical control has not moved, and zeroing
    // it would slam master volume to silence on a dropped transaction.
    CHECK(link.slider(0) == 4095);
    CHECK(link.slider(1) == 2048);
    CHECK(link.slider(2) == 1024);
    CHECK(link.slider(3) == 7);
    // The cache itself is intact; only the view of it changed.
    CHECK(link.lastKnownGood().buttons == 0xFF);
}

TEST_CASE("holdButtonsWhileStale is available for a latching surface", "[satellite_link]")
{
    ap::SatelliteLink link;
    ap::SatelliteLink::Options opt;
    opt.timeoutMs = 100;
    opt.holdButtonsWhileStale = true;
    link.begin(opt, 0);

    REQUIRE(link.onPacket(packet(1, 0x81), 0));
    link.tick(100);
    REQUIRE(link.stale());
    CHECK(link.buttons() == 0x81);
}

TEST_CASE("recovery republishes even when the satellite's SEQ never moved", "[satellite_link]")
{
    // The outage told consumers the buttons were up. When the link comes
    // back with the same snapshot, they have to be told the truth again or a
    // button that was held throughout stays invisible until the next press.
    ap::SatelliteLink link = freshLink(100);
    REQUIRE(link.onPacket(packet(6, 0x40, 1234), 0));

    link.tick(100);
    REQUIRE(link.stale());
    REQUIRE(link.buttons() == 0);

    CHECK(link.onPacket(packet(6, 0x40, 1234), 150)); // same SEQ, publishes
    CHECK(link.fresh());
    CHECK(link.buttons() == 0x40);

    // And the re-sync is one-shot: the next identical read is a duplicate.
    CHECK_FALSE(link.onPacket(packet(6, 0x40, 1234), 154));
}

// ---------------------------------------------------------------------------
// Mechanism 3: last-known-good
// ---------------------------------------------------------------------------

TEST_CASE("a rejected read never partially overwrites the cache", "[satellite_link]")
{
    ap::SatelliteLink link = freshLink(100);
    REQUIRE(link.onPacket(packet(2, 0x33, 2000), 0));

    link.onFailure(4);
    link.onFailure(8);

    CHECK(link.buttons() == 0x33);
    CHECK(link.slider(0) == 2000);
    CHECK(link.lastKnownGood().seq == 2);
    CHECK(link.goodPackets() == 1);
}

TEST_CASE("the cache follows the satellite while the link is healthy", "[satellite_link]")
{
    ap::SatelliteLink link = freshLink(100);
    REQUIRE(link.onPacket(packet(1, 0x01, 100), 0));
    CHECK(link.onPacket(packet(2, 0x02, 200), 4));
    CHECK(link.slider(0) == 200);
    CHECK(link.buttons() == 0x02);
    CHECK(link.onPacket(packet(3, 0x00, 300), 8));
    CHECK(link.buttons() == 0x00);
    CHECK(link.slider(0) == 300);
}

TEST_CASE("an out-of-range slider channel reads zero, not past the array", "[satellite_link]")
{
    ap::SatelliteLink link = freshLink();
    REQUIRE(link.onPacket(packet(1, 0, 4095), 0));
    CHECK(link.slider(ap::kPacketSlidersPerTile) == 0);
    CHECK(link.slider(200) == 0);
}

TEST_CASE("millisecondsSinceGood reports the age of the cache", "[satellite_link]")
{
    ap::SatelliteLink link = freshLink(100);
    CHECK(link.millisecondsSinceGood(500) == 0); // nothing cached yet

    REQUIRE(link.onPacket(packet(1, 0x01), 1000));
    CHECK(link.millisecondsSinceGood(1000) == 0);
    CHECK(link.millisecondsSinceGood(1080) == 80);

    link.onFailure(1200);
    CHECK(link.millisecondsSinceGood(1200) == 200);
}

// ---------------------------------------------------------------------------
// The whole cycle
// ---------------------------------------------------------------------------

TEST_CASE("a satellite dropping out and returning never emits garbage", "[satellite_link]")
{
    ap::SatelliteLink link = freshLink(100);
    std::uint32_t now = 0;

    // Healthy: a fader sweep and a button held down.
    for (std::uint8_t seq = 1; seq <= 5; ++seq)
    {
        REQUIRE(link.onPacket(packet(seq, 0x08,
                                     static_cast<std::uint16_t>(seq * 800)),
                              now));
        now += 4;
    }
    REQUIRE(link.fresh());
    const std::uint16_t lastGoodFader = link.slider(0);
    CHECK(lastGoodFader == 4000);

    // The satellite falls off the bus. Every poll fails.
    for (; now <= 400; now += 4) link.onFailure(now);

    CHECK(link.stale());
    CHECK(link.buttons() == 0);               // the hold is dropped, not stuck
    CHECK(link.slider(0) == lastGoodFader);   // the fader has not moved
    CHECK(link.goodPackets() == 5);           // nothing bogus was accepted

    // It comes back mid-move, with a SEQ that wrapped past where it was.
    CHECK(link.onPacket(packet(2, 0x08, 1500), now));
    CHECK(link.fresh());
    CHECK(link.buttons() == 0x08);
    CHECK(link.slider(0) == 1500);
    CHECK(link.staleEvents() == 1);
}
