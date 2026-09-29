// Host tests for tiles/SliderModule/SliderModule.ino — the real sketch,
// compiled unmodified against tests/py32_stubs/ and driven through its I2C
// slave ISR by a modelled master.
//
// What can only be tested here is the tile's behaviour as a slave and as a
// publisher: that a frame read in one transaction is internally coherent, that
// HEARTBEAT keeps moving (the hub's only proof the tile is alive), that a
// transaction killed mid-flight does not leave the tile publishing a frozen
// snapshot forever, and that a sticky edge is cleared only once a master has
// actually received it.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "py32_stubs/TileHarness.h"

// The sketch's UID read must not dereference a flash address that only exists
// on the part; point it at the harness buffer instead.
#define PY32F030_UID_BASE ((uintptr_t)py32::state().uid)

// The sketch itself. Everything it declares static stays private to this TU.
#include "SliderModule/SliderModule.ino"  // NOLINT(bugprone-suspicious-include)

namespace {

constexpr std::uint8_t kDataLen = 11;              // slider tile DATA_LEN
constexpr std::uint8_t kFrameLen = kDataLen + 2;   // STATUS + DATA + SUM
constexpr std::uint8_t kBtnLevel = 8;              // DATA offsets
constexpr std::uint8_t kBtnPressed = 9;
constexpr std::uint8_t kBtnReleased = 10;

/**
 * Return the sketch's own statics to their power-on values.
 *
 * Catch2 runs every case in one process and the sketch's file-scope statics
 * outlive a test, so a latched fault or a settled SEQ would leak into the next
 * one. Including the sketch in this translation unit is what makes them
 * reachable. The clock deliberately keeps running: loop() and i2cBusWatchdog()
 * hold statics of their own that no one can reach by name, and they only stay
 * coherent if time never goes backwards.
 */
void resetTileState()
{
    seqCounter = 0;
    heartbeatState = 0;
    firstSweepDone = false;
    localFault = false;
    stickyPressed = 0;
    stickyReleased = 0;
    stickyClearReq = 0;
    stickyConsumed = 0;
    stableButtons = 0;
    sampleCount = 0;
    for (auto &count : buttonDebounceCount) count = 0;
    for (std::uint8_t ch = 0; ch < NUM_CHANNELS; ++ch)
    {
        rawValue[ch] = 0;
        rawFilt[ch] = 0;
        publishedValue[ch] = 0;
        sampleAccum[ch] = 0;
    }
    for (std::uint8_t b = 0; b < FRAME_BUFFERS; ++b)
    {
        for (std::uint8_t i = 0; i < FRAME_LEN; ++i) frameBuf[b][i] = 0;
    }
    activeFrame = 0;
    cmdResetQueued = false;
    cmdStrapRereadQueued = false;
    i2cResetTransactionState();
}

/** Boot one tile with a known strap, faders and buttons. */
struct Rig {
    tile::Master master;
    int targets[4] = {0, 0, 0, 0};   // where the faders really are, in ADC counts

    /** Boot with the faders already at `a`..`d`: setup() primes its smoothing from them. */
    explicit Rig(int a = 0, int b = 0, int c = 0, int d = 0)
    {
        const uint32_t clock = py32::state().millis + 1000u; // never rewind
        py32::state() = py32::State{};
        py32::setMillis(clock);
        py32::state().strap = py32::Strap::Floating;
        for (std::uint8_t pin : {PA4, PA5, PA6, PF0}) py32::setDigital(pin, HIGH); // released
        setFaders(a, b, c, d);
        resetTileState();
        setup();
    }

    /**
     * True when STATUS moves across `sweeps` sample sweeps. Sampling two points
     * an even number of sweeps apart proves nothing: HEARTBEAT is one bit and
     * lands back where it started.
     */
    bool heartbeatMoves(int sweeps = 6)
    {
        const std::uint8_t first = readFrame()[0] & tile::kStatusHeartbeat;
        for (int i = 0; i < sweeps; ++i)
        {
            run(SWEEP_INTERVAL_MS);
            if ((readFrame()[0] & tile::kStatusHeartbeat) != first) return true;
        }
        return false;
    }

    void setFaders(int a, int b, int c, int d)
    {
        targets[0] = a;
        targets[1] = b;
        targets[2] = c;
        targets[3] = d;
        py32::setAnalog(PA0, a);
        py32::setAnalog(PA1, b);
        py32::setAnalog(PA2, c);
        py32::setAnalog(PA3, d);
    }

    /**
     * Run until the sketch's pre-deadband smoothing has caught up with the
     * faders. A step is not visible on the wire at once: rawFilt closes on it
     * geometrically (a full-scale step needs ~34 sweeps at RAW_EMA_SHIFT 2,
     * ~68 at 3), and the published value then rests within the deadband of
     * where the smoothing landed. 600 ms is ~150 sweeps, and the REQUIRE says
     * so out loud if a retune ever makes that too short.
     */
    void settle()
    {
        run(600);
        for (std::uint8_t ch = 0; ch < NUM_CHANNELS; ++ch)
        {
            REQUIRE(std::abs(int(rawValue[ch]) - targets[ch]) <= 1);
        }
    }

    /** The deadband in force, in ADC counts — the most a settled fader may rest from the truth. */
    int deadband() const
    {
        return kDeadbandCounts[regMap[Proto::REG_CFG_FILTER] & 0x0F];
    }

    /** Buttons are active-low through internal pull-ups. */
    void pressButton(std::uint8_t index, bool down)
    {
        static const std::uint8_t pins[4] = {PA4, PA5, PA6, PF0};
        py32::setDigital(pins[index], down ? LOW : HIGH);
    }

    /** Run the tile's loop() across `ms` of its own time. */
    void run(std::uint32_t ms)
    {
        for (std::uint32_t i = 0; i < ms; ++i)
        {
            py32::advanceMillis(1);
            loop();
        }
    }

    std::vector<std::uint8_t> readFrame() { return master.readFrom(tile::kRegStatus, kFrameLen); }
};

std::uint16_t faderOf(const std::vector<std::uint8_t> &frame, std::uint8_t ch)
{
    const std::uint8_t i = 1 + ch * 2;
    return static_cast<std::uint16_t>(frame[i] | (frame[i + 1] << 8));
}

} // namespace

// ---------------------------------------------------------------------------
// Identity and bus configuration
// ---------------------------------------------------------------------------

TEST_CASE("the tile answers its identity block", "[py32][slider]")
{
    Rig rig;
    const std::vector<std::uint8_t> id = rig.master.readFrom(tile::kRegWhoAmI, 0x1A);
    REQUIRE(id.size() == 0x1A);
    CHECK(id[0x00] == 0x5A);     // WHO_AM_I
    CHECK(id[0x01] == 0x01);     // TYPE_ID: slider
    CHECK(id[0x02] == 0x02);     // PROTO_VER v2
    CHECK(id[tile::kRegDataLen] == kDataLen);
    CHECK(id[0x06] == 2);        // floating strap
}

TEST_CASE("both tiles are programmed for the bus rate the hub drives", "[py32][slider]")
{
    // A tile configured for standard-mode timing on a bank clocked at fast
    // mode is the documented stall. One rate, everywhere.
    Rig rig;
    CHECK(kBusClockHz == 400000u);
    // Slider registry base 0x08 plus the floating strap's offset 2.
    CHECK(I2C1->OAR1 == (std::uint32_t)((0x08 + 2) << 1));
}

// ---------------------------------------------------------------------------
// The snapshot
// ---------------------------------------------------------------------------

TEST_CASE("a frame read in one transaction is coherent and checksummed", "[py32][slider]")
{
    Rig rig;
    rig.setFaders(100, 2000, 3000, 4095);
    rig.pressButton(0, true);
    rig.pressButton(3, true);
    rig.settle();

    const std::vector<std::uint8_t> frame = rig.readFrame();
    REQUIRE(frame.size() == kFrameLen);
    REQUIRE(tile::frameOk(frame, kDataLen));

    // Each fader in its own slot. Within the deadband of the truth rather than
    // equal to it: the tile republishes a value only once it has moved further
    // than the deadband, and the smoothing floors to a whole count. The four
    // targets are thousands of counts apart, so the tolerance cannot blur them.
    const int tolerance = rig.deadband() + 1;
    CHECK(std::abs(faderOf(frame, 0) - 100) <= tolerance);
    CHECK(std::abs(faderOf(frame, 1) - 2000) <= tolerance);
    CHECK(std::abs(faderOf(frame, 2) - 3000) <= tolerance);
    CHECK(std::abs(faderOf(frame, 3) - 4095) <= tolerance);
    CHECK(frame[1 + kBtnLevel] == 0x09); // buttons 0 and 3
    CHECK_FALSE(frame[0] & tile::kStatusNotReady);
}

TEST_CASE("NOT_READY clears once the first real sweep lands", "[py32][slider]")
{
    Rig rig;
    CHECK(rig.readFrame()[0] & tile::kStatusNotReady);
    rig.run(20);
    CHECK_FALSE(rig.readFrame()[0] & tile::kStatusNotReady);
}

TEST_CASE("SEQ advances only when DATA changed", "[py32][slider]")
{
    Rig rig;
    rig.setFaders(1000, 1000, 1000, 1000);
    rig.settle(); // SEQ keeps moving until the smoothing has arrived

    const std::uint8_t settled = tile::seqOf(rig.readFrame()[0]);
    rig.run(40); // nothing touched
    CHECK(tile::seqOf(rig.readFrame()[0]) == settled);

    rig.setFaders(3000, 1000, 1000, 1000);
    rig.run(40);
    CHECK(tile::seqOf(rig.readFrame()[0]) != settled);
}

// ---------------------------------------------------------------------------
// Fader conditioning — smoothing, then deadband, in front of the snapshot
// ---------------------------------------------------------------------------

TEST_CASE("a resting fader's ADC dither does not churn the snapshot", "[py32][slider][filter]")
{
    // The deadband exists so an untouched fader is byte-stable. Dither that
    // republished every sweep would advance SEQ every sweep, put jitter
    // straight into the DSP, and make every poll look like news to the hub.
    Rig rig;
    rig.setFaders(2000, 2000, 2000, 2000);
    rig.settle();

    // +/-3 counts, alternating every sweep, opposite phase on alternate channels.
    auto dither = [&rig](int sweep) {
        const int wobble = (sweep & 1) ? 3 : -3;
        rig.setFaders(2000 + wobble, 2000 - wobble, 2000 + wobble, 2000 - wobble);
        rig.run(SWEEP_INTERVAL_MS);
    };

    // A fader that settled at the very edge of its deadband may republish once
    // when the dither first arrives. That is a step, not churn, so let it land.
    for (int sweep = 0; sweep < 20; ++sweep) dither(sweep);
    const std::uint8_t settled = tile::seqOf(rig.readFrame()[0]);

    int seqMoves = 0;
    for (int sweep = 20; sweep < 120; ++sweep)
    {
        dither(sweep);
        if (tile::seqOf(rig.readFrame()[0]) != settled) ++seqMoves;
    }
    CHECK(seqMoves == 0);
}

TEST_CASE("a deliberate fader move passes the deadband and is published", "[py32][slider][filter]")
{
    // The deadband's other half: gate the noise without eating the smallest
    // deliberate move, which was measured at 30-50 counts on this hardware.
    // "Moved" is judged from what the hub last saw. The tile compares against
    // its published value, which may rest up to a deadband from the truth.
    Rig rig;
    rig.setFaders(2000, 2000, 2000, 2000);
    rig.settle();
    const std::vector<std::uint8_t> before = rig.readFrame();
    const int rest = faderOf(before, 0);

    rig.setFaders(rest + 30, 2000, 2000, 2000);
    rig.settle();
    const std::vector<std::uint8_t> frame = rig.readFrame();

    CHECK(tile::seqOf(frame[0]) != tile::seqOf(before[0]));
    CHECK(faderOf(frame, 0) > rest);
    CHECK(faderOf(frame, 1) == faderOf(before, 1)); // an untouched channel does not budge
}

TEST_CASE("a settled fader rests within the deadband of where it really is", "[py32][slider][filter]")
{
    // The price of a byte-stable rest is that the published value may sit up
    // to one deadband away from the truth. Pin the bound rather than exactness
    // so a retuned deadband moves the bound with it. The targets include both
    // end stops, reached from the far side each time.
    Rig rig;
    for (int target : {4095, 0, 1, 2048, 4094, 137, 4000, 0, 4095})
    {
        rig.setFaders(target, target, target, target);
        rig.settle();

        const std::vector<std::uint8_t> frame = rig.readFrame();
        for (std::uint8_t ch = 0; ch < NUM_CHANNELS; ++ch)
        {
            INFO("target " << target << ", channel " << int(ch));
            CHECK(std::abs(faderOf(frame, ch) - target) <= rig.deadband() + 1);
        }
    }
}

TEST_CASE("the first published frame carries the real fader positions", "[py32][slider][filter]")
{
    // setup() primes the smoothing with a live sample. Without that it starts
    // from zero, and a tile powered up with a fader at 3000 feeds the hub a
    // slow fake sweep up to it: five sweeps is nowhere near enough to ramp.
    Rig rig(3000, 1500, 500, 4095);
    rig.run(5 * SWEEP_INTERVAL_MS);

    const std::vector<std::uint8_t> frame = rig.readFrame();
    REQUIRE(tile::frameOk(frame, kDataLen));
    CHECK(std::abs(faderOf(frame, 0) - 3000) <= rig.deadband() + 1);
    CHECK(std::abs(faderOf(frame, 1) - 1500) <= rig.deadband() + 1);
    CHECK(std::abs(faderOf(frame, 2) - 500) <= rig.deadband() + 1);
    CHECK(std::abs(faderOf(frame, 3) - 4095) <= rig.deadband() + 1);
}

TEST_CASE("every deadband index is addressable and none aliases another", "[py32][slider][filter]")
{
    // The lookup used to take the low two bits, so indices 4..15 silently
    // behaved as 0..3 and a hub asking for a wide deadband got a narrow one.
    // Index 9 is 20 counts; index 1 is 2. The test picks its own deadband up
    // front so it does not depend on whatever the power-on default is.
    Rig rig;
    rig.master.writeRegister(Proto::REG_CFG_FILTER, {Proto::cfgFilter(9, 0)});
    REQUIRE(rig.deadband() == 20);

    rig.setFaders(2000, 2000, 2000, 2000);
    rig.settle();

    // A settled fader rests anywhere within the deadband of the truth, so
    // measure the step from what was actually published, not from 2000.
    const int rest = faderOf(rig.readFrame(), 0);
    const int moved = rest + 15;

    rig.setFaders(moved, 2000, 2000, 2000);
    rig.settle();
    CHECK(faderOf(rig.readFrame(), 0) == rest); // 15 counts sits inside a 20-count deadband

    rig.master.writeRegister(Proto::REG_CFG_FILTER, {Proto::cfgFilter(1, 0)});
    REQUIRE(rig.deadband() == 2);
    rig.run(4 * SWEEP_INTERVAL_MS);
    CHECK(std::abs(faderOf(rig.readFrame(), 0) - moved) <= 2 + 1); // and clears a 2-count one
}

// ---------------------------------------------------------------------------
// HEARTBEAT — the hub's only proof of life
// ---------------------------------------------------------------------------

TEST_CASE("HEARTBEAT toggles on every sweep even when nothing changes", "[py32][slider]")
{
    // The hub treats a STATUS byte that never moves as a dead link, whatever
    // the checksum says. An idle tile must therefore keep this bit moving or
    // an untouched panel drops its controls.
    Rig rig;
    rig.run(40);

    int toggles = 0;
    std::uint8_t previous = rig.readFrame()[0] & tile::kStatusHeartbeat;
    for (int sweep = 0; sweep < 20; ++sweep)
    {
        rig.run(4); // one sweep interval
        const std::uint8_t now = rig.readFrame()[0] & tile::kStatusHeartbeat;
        if (now != previous) ++toggles;
        previous = now;
    }
    CHECK(toggles >= 15); // one per sweep, allowing for loop/poll phasing
}

TEST_CASE("a read in flight never stalls the next publish", "[py32][slider]")
{
    // The regression this exists for. A publish landing during a read has
    // nowhere to go and is skipped; if the read never ends, the skip repeats
    // forever, activeFrame stops moving, and SEQ, DATA and HEARTBEAT freeze
    // together while the slave goes on answering perfectly. That is
    // indistinguishable on the wire from a dead panel.
    Rig rig;
    rig.run(40);

    // Open a read and walk away from it: no NACK, no STOP, so the tile is
    // left holding the buffer it latched.
    rig.master.readAndAbandon(4);
    REQUIRE(servingBuf != 0xFF);

    // Observed from inside the sketch on purpose. Reading the frame over the
    // bus would open a fresh transaction, re-latch servingBuf and end it
    // cleanly, which is exactly what hid this freeze: the act of looking for
    // it made it go away.
    int toggles = 0;
    std::uint8_t previous = frameBuf[activeFrame][0] & ST_HEARTBEAT;
    for (int sweep = 0; sweep < 8; ++sweep)
    {
        rig.run(SWEEP_INTERVAL_MS);
        const std::uint8_t now = frameBuf[activeFrame][0] & ST_HEARTBEAT;
        if (now != previous) ++toggles;
        previous = now;
    }

    // A transaction lasts microseconds, so a latch still held sweeps later is
    // stale: the tile takes the buffer back rather than publishing nothing.
    CHECK(servingBuf == 0xFF);
    CHECK(toggles >= 6);
    CHECK(rig.readFrame()[0] & tile::kStatusLocalFault); // and says it happened
}

TEST_CASE("the tile keeps publishing after its peripheral is rebuilt", "[py32][slider]")
{
    // The bus watchdog force-resets I2C1 when BUSY sticks with no interrupt
    // activity. The transaction it kills never reaches endTransaction(), so
    // every scrap of transaction state has to be cleared by the rebuild.
    Rig rig;
    rig.run(40);

    rig.master.readAndAbandon(4);     // BUSY stays set, ISR goes quiet
    rig.run(I2C_STUCK_MS + 20);       // watchdog notices and rebuilds

    CHECK(py32::state().nvicDisables > 0); // it really did rebuild
    CHECK(rig.readFrame()[0] & tile::kStatusLocalFault); // and said so
    CHECK(rig.heartbeatMoves());

    // And the frame it serves afterwards is still a valid one.
    const std::vector<std::uint8_t> frame = rig.readFrame();
    CHECK(tile::frameOk(frame, kDataLen));
}

// ---------------------------------------------------------------------------
// Sticky edges
// ---------------------------------------------------------------------------

TEST_CASE("a tap between polls survives as sticky bits", "[py32][slider]")
{
    Rig rig;
    rig.run(40);

    rig.pressButton(1, true);
    rig.run(12);
    rig.pressButton(1, false);
    rig.run(12);

    const std::vector<std::uint8_t> frame = rig.readFrame();
    CHECK(frame[1 + kBtnLevel] == 0x00);     // already back up
    CHECK(frame[1 + kBtnPressed] & 0x02);    // but the press was latched
    CHECK(frame[1 + kBtnReleased] & 0x02);
}

TEST_CASE("sticky bits clear only after a full frame has been delivered", "[py32][slider]")
{
    Rig rig;
    rig.run(40);
    rig.pressButton(2, true);
    rig.run(12);
    REQUIRE(rig.readFrame()[1 + kBtnPressed] & 0x04);

    rig.run(8);
    CHECK_FALSE(rig.readFrame()[1 + kBtnPressed] & 0x04); // consumed by that read
}

TEST_CASE("a STATUS-only read consumes no edge", "[py32][slider]")
{
    // A master may poll STATUS alone to decide whether to read the rest. Its
    // cursor stops before the sticky bytes, so it must not clear them.
    Rig rig;
    rig.run(40);
    rig.pressButton(0, true);
    rig.run(12);

    for (int poll = 0; poll < 5; ++poll)
    {
        rig.master.readFrom(tile::kRegStatus, 1);
        rig.run(8);
    }
    CHECK(rig.readFrame()[1 + kBtnPressed] & 0x01); // still there to be read
}

TEST_CASE("a read that stops short does not clear the byte it never reached", "[py32][slider]")
{
    // Losing a press is worse than repeating one, so a byte written into DR
    // but never clocked out must not count as delivered.
    Rig rig;
    rig.run(40);
    rig.readFrame();   // drain whatever the boot sweeps latched
    rig.run(8);

    // A tap, with no full read in between: both sticky bits are now live.
    rig.pressButton(3, true);
    rig.run(12);
    rig.pressButton(3, false);
    rig.run(12);

    // Read STATUS plus DATA offsets 0..9 — through the pressed byte, stopping
    // one short of the released byte at offset 10.
    rig.master.readFrom(tile::kRegStatus, 1 + kBtnPressed + 1);
    rig.run(8);

    const std::vector<std::uint8_t> frame = rig.readFrame();
    CHECK(frame[1 + kBtnPressed] == 0x00);   // that one reached the master
    CHECK(frame[1 + kBtnReleased] & 0x08);   // this one never left the tile
}

// ---------------------------------------------------------------------------
// ISR edge cases
// ---------------------------------------------------------------------------

TEST_CASE("TXE and BTF in one snapshot send exactly one byte", "[py32][slider]")
{
    // Both flags mean "the peripheral wants the next byte". Servicing them as
    // two independent writes put two bytes into one slot and skewed every
    // byte after it, which showed up as checksum failures under load.
    Rig rig;
    rig.setFaders(0x111, 0x222, 0x333, 0x444);
    rig.run(40);

    const std::vector<std::uint8_t> reference = rig.readFrame();
    REQUIRE(tile::frameOk(reference, kDataLen));

    rig.master.writePointer(tile::kRegStatus);
    std::vector<std::uint8_t> viaBtf;
    I2C1->SR2 = I2C_SR2_BUSY | I2C_SR2_TRA;
    I2C1->SR1 = I2C_SR1_ADDR;
    I2C1_IRQHandler();
    viaBtf.push_back(static_cast<std::uint8_t>(I2C1->DR));
    for (std::uint8_t i = 1; i < kFrameLen; ++i)
    {
        viaBtf.push_back(rig.master.takeNextByteWithBtf());
    }
    I2C1->SR1 = I2C_SR1_AF;
    I2C1_IRQHandler();
    I2C1->SR2 &= ~I2C_SR2_BUSY;

    CHECK(tile::frameOk(viaBtf, kDataLen));
    CHECK(viaBtf == reference); // same bytes, same order, nothing skipped
}

TEST_CASE("an over-read pads with zeroes and never NACKs", "[py32][slider]")
{
    Rig rig;
    rig.run(40);
    const std::vector<std::uint8_t> frame = rig.master.readFrom(tile::kRegStatus, kFrameLen + 6);
    REQUIRE(frame.size() == std::size_t(kFrameLen) + 6u);
    for (std::size_t i = kFrameLen; i < frame.size(); ++i) CHECK(frame[i] == 0x00);
}

TEST_CASE("an out-of-range pointer parks instead of landing on SOFT_CMD", "[py32][slider]")
{
    // REG_MAP_SIZE - 1 is REG_SOFT_CMD. Clamping there turned every stray
    // payload byte of an over-long write into a soft command — including the
    // reset the tile answers by starving its own watchdog.
    Rig rig;
    rig.run(40);
    rig.master.writeRegister(0xF0, {0x01, 0x01, 0x01}); // CMD_RESET, three times
    rig.run(40);

    // Still sweeping, still publishing, no latched fault from a bogus command.
    CHECK(rig.heartbeatMoves());
    CHECK_FALSE(rig.readFrame()[0] & tile::kStatusLocalFault);
}

TEST_CASE("the config page stays writable and is honoured", "[py32][slider]")
{
    Rig rig;
    rig.run(40);
    rig.master.writeRegister(0x42, {16}); // CFG_DEBOUNCE = 16 ms
    const std::vector<std::uint8_t> cfg = rig.master.readFrom(0x42, 1);
    CHECK(cfg[0] == 16);

    // Four sweeps of a stable level are now needed before an edge is accepted.
    rig.pressButton(0, true);
    rig.run(6);
    CHECK(rig.readFrame()[1 + kBtnLevel] == 0x00);
    rig.run(20);
    CHECK(rig.readFrame()[1 + kBtnLevel] == 0x01);
}
