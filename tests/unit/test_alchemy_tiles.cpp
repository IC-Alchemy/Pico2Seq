// Unit tests for src/AlchemyUI/src/AlchemyTiles.cpp, the real driver, driven
// against the scriptable I2C bus in tests/tile_stubs/Wire.h.
//
// AlchemyProto and SatelliteLink are tested on their own; what can only be
// tested here is the driver's behaviour as a bus master:
//
//   - one transaction per poll, the whole snapshot, never a STATUS probe
//     followed by a second read that could straddle two sample sweeps;
//   - a failing satellite never propagates garbage: faders hold their
//     last-known-good position (utility fader 2 is master volume) and held
//     buttons release rather than latching;
//   - nothing in the poll path retries, sleeps, or spins, so no amount of
//     bus trouble can turn into time the control loop spends waiting.

#include "AlchemyUI/src/AlchemyTiles.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace
{

constexpr std::uint8_t kSliderAddress = alchemy::kAddrSliderBase; // 0x08
constexpr std::uint8_t kButtonAddress = alchemy::kAddrButtonBase; // 0x0B

/** A bus carrying a slider tile, with the driver already past discovery. */
struct Rig
{
    TwoWire bus;
    AlchemyTiles tiles;
    std::uint32_t now = 0;

    explicit Rig(bool withButtonTile = false)
    {
        bus.attach(kSliderAddress,
                   FakeTile(alchemy::kTypeSliderButton, alchemy::kSliderDataLen));
        if (withButtonTile)
        {
            bus.attach(kButtonAddress,
                       FakeTile(alchemy::kTypeButton4, alchemy::kButtonDataLen));
        }
        tiles.begin(bus, nullptr, now);
    }

    FakeTile &slider() { return bus.tile(kSliderAddress); }
    FakeTile &buttonTile() { return bus.tile(kButtonAddress); }

    /** Advance time and run one update() pass. */
    void step(std::uint32_t deltaMs)
    {
        now += deltaMs;
        tiles.update(now);
    }

    /** Run passes until `ms` of simulated time has gone by. */
    void run(std::uint32_t ms, std::uint32_t deltaMs = 1)
    {
        const std::uint32_t until = now + ms;
        while (now < until) step(deltaMs);
    }

    /**
     * Exactly one poll. update() services at most one due tile per pass, and
     * on a single-tile rig that tile is always the one, so a single pass at
     * the poll interval is one transaction's worth of work — which is what
     * lets the transaction counts below mean anything.
     */
    void pollSlider() { step(AlchemyTiles::kPollIntervalMs); }
};

const std::uint16_t kMidFaders[4] = {1000, 2000, 3000, 4000};

} // namespace

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

TEST_CASE("the driver claims the tiles that answer the registry", "[alchemy_tiles]")
{
    Rig rig(/*withButtonTile=*/true);
    CHECK(rig.tiles.presentTileCount() == 2);
    CHECK(rig.tiles.hasSlider());
    CHECK(rig.tiles.sliderSlot() == 0); // the slider always claims slot 0
    CHECK(rig.tiles.firstSlotOfType(alchemy::kTypeButton4) == 1);
}

// ---------------------------------------------------------------------------
// One transaction per poll
// ---------------------------------------------------------------------------

TEST_CASE("a poll reads the whole snapshot in one transaction", "[alchemy_tiles]")
{
    Rig rig;
    rig.slider().setState(1, 0x00, kMidFaders);
    rig.pollSlider(); // settle the first frame

    rig.bus.resetCounters();
    rig.slider().setState(2, 0x03, kMidFaders);
    rig.pollSlider();

    // Exactly one pointer write and one block read, whatever the tile did:
    // no STATUS probe, no second pass to fetch DATA after SEQ moved.
    CHECK(rig.bus.pointerWrites() == 1);
    CHECK(rig.bus.blockReads() == 1);
    CHECK(rig.bus.lastReadLength() == 1 + alchemy::kSliderDataLen + 1);
}

TEST_CASE("an idle tile costs the same single transaction", "[alchemy_tiles]")
{
    // The old adaptive read spent one transaction while idle and two on every
    // poll that mattered. One snapshot, always, is both simpler and cheaper
    // exactly when the player is moving something.
    Rig rig;
    rig.slider().setState(4, 0x00, kMidFaders);
    rig.pollSlider();

    rig.bus.resetCounters();
    rig.pollSlider(); // SEQ unchanged

    CHECK(rig.bus.pointerWrites() == 1);
    CHECK(rig.bus.blockReads() == 1);
    CHECK(rig.bus.lastReadLength() == 1 + alchemy::kSliderDataLen + 1);
}

TEST_CASE("the driver services at most one tile per update pass", "[alchemy_tiles]")
{
    Rig rig(/*withButtonTile=*/true);
    rig.run(50);
    rig.bus.resetCounters();
    rig.step(AlchemyTiles::kPollIntervalMs);
    CHECK(rig.bus.blockReads() <= 1);
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

TEST_CASE("faders and buttons arrive from the snapshot", "[alchemy_tiles]")
{
    Rig rig;
    rig.slider().setState(1, 0x05, kMidFaders);
    rig.pollSlider();

    CHECK(rig.tiles.faderRaw(0) == 1000);
    CHECK(rig.tiles.faderRaw(1) == 2000);
    CHECK(rig.tiles.faderRaw(2) == 3000);
    CHECK(rig.tiles.faderRaw(3) == 4000);
    CHECK(rig.tiles.button(0, 0).held());
    CHECK_FALSE(rig.tiles.button(0, 1).held());
    CHECK(rig.tiles.button(0, 2).held());
}

TEST_CASE("a sticky edge is consumed once and never replayed", "[alchemy_tiles]")
{
    // A press and release that both land between two polls survive as sticky
    // bits. Caching them and re-feeding them would invent a second press.
    Rig rig;
    rig.slider().setState(1, 0x00, kMidFaders);
    rig.pollSlider();

    rig.slider().setEdges(/*pressed=*/0x01, /*released=*/0x01);
    rig.slider().setState(2, 0x00, kMidFaders);
    rig.pollSlider();
    // The frame carrying the sticky bits registers the press, even though the
    // level is already back down (TileButton resolves the release next poll).
    CHECK(rig.tiles.button(0, 0).pressEdge());
    CHECK(rig.tiles.button(0, 0).held());

    // The tile clears its sticky bytes now that a read cursor passed them.
    rig.slider().setEdges(0, 0);
    rig.slider().setState(3, 0x00, kMidFaders);
    rig.pollSlider();
    CHECK(rig.tiles.button(0, 0).releaseTap());
    CHECK_FALSE(rig.tiles.button(0, 0).held());

    // The edge is spent. Polling again must not re-deliver it from the cache.
    rig.slider().setState(4, 0x00, kMidFaders);
    rig.pollSlider();
    CHECK_FALSE(rig.tiles.button(0, 0).pressEdge());
    CHECK_FALSE(rig.tiles.button(0, 0).releaseTap());
}

// ---------------------------------------------------------------------------
// Last-known-good: the fader path
// ---------------------------------------------------------------------------

TEST_CASE("a dead satellite does not slam the faders to zero", "[alchemy_tiles]")
{
    // The regression this exists for: faderRaw() used to be gated on the
    // tile being present, so four consecutive NACKs returned 0 for every
    // channel. Utility fader 2 is master volume — a flaky bus silenced the
    // instrument.
    Rig rig;
    rig.slider().setState(1, 0x00, kMidFaders);
    rig.pollSlider();
    REQUIRE(rig.tiles.faderRaw(2) == 3000);

    rig.slider().setOffline(true);
    rig.run(500);

    REQUIRE_FALSE(rig.tiles.hasSlider()); // the driver knows it is gone...
    CHECK(rig.tiles.faderRaw(0) == 1000); // ...and still reports where the
    CHECK(rig.tiles.faderRaw(1) == 2000); //    physical faders actually are
    CHECK(rig.tiles.faderRaw(2) == 3000);
    CHECK(rig.tiles.faderRaw(3) == 4000);
}

TEST_CASE("a corrupt frame contributes nothing at all", "[alchemy_tiles]")
{
    Rig rig;
    rig.slider().setState(1, 0x0F, kMidFaders);
    rig.pollSlider();
    REQUIRE(rig.tiles.faderRaw(0) == 1000);

    const std::uint16_t garbage[4] = {4095, 4095, 4095, 4095};
    rig.slider().setCorruptChecksum(true);
    rig.slider().setState(2, 0x00, garbage);
    rig.pollSlider();

    CHECK(rig.tiles.faderRaw(0) == 1000); // the checksum caught it
    CHECK(rig.tiles.link(0).rejectedReads() >= 1);
    CHECK(rig.tiles.info(0).checksumErrors >= 1);
}

TEST_CASE("a short read is refused rather than decoded", "[alchemy_tiles]")
{
    Rig rig;
    rig.slider().setState(1, 0x00, kMidFaders);
    rig.pollSlider();

    rig.slider().setShortRead(true);
    rig.pollSlider();

    CHECK(rig.tiles.faderRaw(3) == 4000);
    CHECK(rig.tiles.link(0).rejectedReads() >= 1);
}

// ---------------------------------------------------------------------------
// Timeout: the button path
// ---------------------------------------------------------------------------

TEST_CASE("a button held when the satellite dies does not stay held", "[alchemy_tiles]")
{
    // A frozen level bitmap is a stuck key: it pins Shift, latches parameter
    // recording, keeps a transport chord armed. Releasing is the safe
    // failure.
    Rig rig;
    rig.slider().setState(1, 0x01, kMidFaders);
    rig.pollSlider();
    REQUIRE(rig.tiles.button(0, 0).held());

    rig.slider().setOffline(true);
    rig.run(AlchemyTiles::kLinkTimeoutMs + 50);

    CHECK_FALSE(rig.tiles.button(0, 0).held());
    // And the release is not a tap: nothing the player did ended that press,
    // so no action may fire from it.
    CHECK_FALSE(rig.tiles.button(0, 0).releaseTap());
}

TEST_CASE("the timeout runs on the clock, not on the polling rotation", "[alchemy_tiles]")
{
    // Five slots share the rotation one tile per pass. If staleness were only
    // evaluated on a slot's turn, a satellite that stopped answering would
    // hold its buttons for as long as the rotation took to come back round.
    Rig rig(/*withButtonTile=*/true);
    rig.buttonTile().setState(1, 0x80, nullptr); // Shift held
    rig.run(50);
    REQUIRE(rig.tiles.button(1, 7).held());

    rig.buttonTile().setOffline(true);
    rig.run(AlchemyTiles::kLinkTimeoutMs + 50);
    CHECK_FALSE(rig.tiles.button(1, 7).held());
}

TEST_CASE("a satellite that comes back republishes its real state", "[alchemy_tiles]")
{
    Rig rig;
    rig.slider().setState(1, 0x02, kMidFaders);
    rig.pollSlider();
    REQUIRE(rig.tiles.button(0, 1).held());

    rig.slider().setOffline(true);
    rig.run(AlchemyTiles::kLinkTimeoutMs + 50);
    REQUIRE_FALSE(rig.tiles.button(0, 1).held());

    // Back on the bus with the same snapshot it had before: SEQ never moved,
    // but the button really is still down and has to be reported again.
    rig.slider().setOffline(false);
    rig.run(AlchemyTiles::kReprobeIntervalMs + 50);

    CHECK(rig.tiles.hasSlider());
    CHECK(rig.tiles.button(0, 1).held());
    CHECK(rig.tiles.link(0).staleEvents() >= 1);
}

TEST_CASE("a burst of failures inside the timeout is invisible", "[alchemy_tiles]")
{
    // One flaky transaction must not produce a visible glitch. This is the
    // whole point of a cached satellite: the hub rides it out.
    Rig rig;
    rig.slider().setState(1, 0x01, kMidFaders);
    rig.pollSlider();

    rig.slider().setCorruptChecksum(true);
    rig.run(AlchemyTiles::kLinkTimeoutMs / 2);

    CHECK(rig.tiles.button(0, 0).held());
    CHECK(rig.tiles.faderRaw(1) == 2000);
    CHECK(rig.tiles.link(0).fresh());
}

TEST_CASE("an empty bus never claims a tile or reports a value", "[alchemy_tiles]")
{
    TwoWire bus;
    AlchemyTiles tiles;
    tiles.begin(bus, nullptr, 0);

    CHECK(tiles.presentTileCount() == 0);
    CHECK_FALSE(tiles.hasSlider());
    CHECK(tiles.sliderSlot() == -1);
    CHECK(tiles.faderRaw(0) == 0); // no slider ever answered: zero is the truth

    for (std::uint32_t t = 1; t <= 200; ++t) tiles.update(t);
    CHECK(tiles.presentTileCount() == 0);
    CHECK(tiles.faderRaw(0) == 0);
}
