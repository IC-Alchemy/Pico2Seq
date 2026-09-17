// Host tests for tiles/ButtonModule8/ButtonModule8.ino — the real sketch,
// compiled unmodified against tests/py32_stubs/ and driven through its I2C
// slave ISR by a modelled master.
//
// The two tiles share an I2C layer line for line, so the cases that matter
// here are the ones that could drift apart: the same bus rate, the same
// publish guarantees, and the 8-bit button bitmap in a three-byte DATA block
// that a slider-tile parser has to read identically.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "py32_stubs/TileHarness.h"

#define PY32F030_UID_BASE ((uintptr_t)py32::state().uid)

#include "ButtonModule8/ButtonModule8.ino"  // NOLINT(bugprone-suspicious-include)

namespace {

constexpr std::uint8_t kDataLen = 3;               // button tile DATA_LEN
constexpr std::uint8_t kFrameLen = kDataLen + 2;   // STATUS + DATA + SUM
constexpr std::uint8_t kBtnLevel = 0;              // DATA offsets — no faders
constexpr std::uint8_t kBtnPressed = 1;
constexpr std::uint8_t kBtnReleased = 2;

/** See the slider suite: the sketch's statics outlive a Catch2 test case. */
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
    for (auto &count : buttonDebounceCount) count = 0;
    for (std::uint8_t b = 0; b < FRAME_BUFFERS; ++b)
    {
        for (std::uint8_t i = 0; i < FRAME_LEN; ++i) frameBuf[b][i] = 0;
    }
    activeFrame = 0;
    cmdResetQueued = false;
    cmdStrapRereadQueued = false;
    i2cResetTransactionState();
}

struct Rig {
    tile::Master master;

    Rig()
    {
        const uint32_t clock = py32::state().millis + 1000u; // never rewind
        py32::state() = py32::State{};
        py32::setMillis(clock);
        py32::state().strap = py32::Strap::Floating;
        for (std::uint8_t pin = PA0; pin <= PA7; ++pin) py32::setDigital(pin, HIGH);
        resetTileState();
        setup();
    }

    /** Buttons are active-low through internal pull-ups. */
    void pressButton(std::uint8_t index, bool down)
    {
        py32::setDigital(static_cast<std::uint8_t>(PA0 + index), down ? LOW : HIGH);
    }

    void run(std::uint32_t ms)
    {
        for (std::uint32_t i = 0; i < ms; ++i)
        {
            py32::advanceMillis(1);
            loop();
        }
    }

    std::vector<std::uint8_t> readFrame() { return master.readFrom(tile::kRegStatus, kFrameLen); }

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
};

} // namespace

TEST_CASE("the button tile answers its own identity and registry slot", "[py32][button]")
{
    Rig rig;
    const std::vector<std::uint8_t> id = rig.master.readFrom(tile::kRegWhoAmI, 0x1A);
    REQUIRE(id.size() == 0x1A);
    CHECK(id[0x00] == 0x5A);
    CHECK(id[0x01] == 0x02);     // TYPE_ID: button
    CHECK(id[0x02] == 0x02);     // PROTO_VER v2
    CHECK(id[tile::kRegDataLen] == kDataLen);

    // Button registry base 0x0B plus the floating strap's offset 2. The two
    // types occupy separate blocks precisely so they cannot collide.
    CHECK(I2C1->OAR1 == (std::uint32_t)((0x0B + 2) << 1));
}

TEST_CASE("both tile types are programmed for the same bus rate", "[py32][button]")
{
    // They share one Qwiic bank. A tile left at standard-mode timing on a bank
    // the hub clocks at fast mode is the documented stall.
    CHECK(kBusClockHz == 400000u);
}

TEST_CASE("all eight buttons land in the shared three-byte block", "[py32][button]")
{
    // The 8x PCB drives bits 4..7 of the same bytes a 4x tile uses, so DATA_LEN
    // and every offset stay put and one hub parser serves both types.
    Rig rig;
    rig.run(40);

    rig.pressButton(0, true);
    rig.pressButton(7, true);
    rig.run(20);

    const std::vector<std::uint8_t> frame = rig.readFrame();
    REQUIRE(tile::frameOk(frame, kDataLen));
    CHECK(frame[1 + kBtnLevel] == 0x81);
}

TEST_CASE("a button frame is coherent and checksummed", "[py32][button]")
{
    Rig rig;
    rig.run(40);
    rig.pressButton(3, true);
    rig.run(20);

    const std::vector<std::uint8_t> frame = rig.readFrame();
    REQUIRE(frame.size() == kFrameLen);
    CHECK(tile::frameOk(frame, kDataLen));
    CHECK(frame[1 + kBtnLevel] == 0x08);
    CHECK_FALSE(frame[0] & tile::kStatusNotReady);
}

TEST_CASE("SEQ advances only when a button actually moved", "[py32][button]")
{
    Rig rig;
    rig.run(40);
    const std::uint8_t settled = tile::seqOf(rig.readFrame()[0]);

    rig.run(40); // nobody touches anything
    CHECK(tile::seqOf(rig.readFrame()[0]) == settled);

    rig.pressButton(5, true);
    rig.run(20);
    CHECK(tile::seqOf(rig.readFrame()[0]) != settled);
}

TEST_CASE("HEARTBEAT keeps toggling on an untouched button tile", "[py32][button]")
{
    Rig rig;
    rig.run(40);
    CHECK(rig.heartbeatMoves());
}

TEST_CASE("an abandoned read never stalls the button tile's publishing", "[py32][button]")
{
    Rig rig;
    rig.run(40);

    rig.master.readAndAbandon(3);
    REQUIRE(servingBuf != 0xFF);

    int toggles = 0;
    std::uint8_t previous = frameBuf[activeFrame][0] & ST_HEARTBEAT;
    for (int sweep = 0; sweep < 8; ++sweep)
    {
        rig.run(SWEEP_INTERVAL_MS);
        const std::uint8_t now = frameBuf[activeFrame][0] & ST_HEARTBEAT;
        if (now != previous) ++toggles;
        previous = now;
    }

    // Stale latch reclaimed rather than publishing nothing for ever.
    CHECK(servingBuf == 0xFF);
    CHECK(toggles >= 6);
    CHECK(rig.readFrame()[0] & tile::kStatusLocalFault);
}

TEST_CASE("the button tile keeps publishing after its peripheral is rebuilt", "[py32][button]")
{
    Rig rig;
    rig.run(40);

    rig.master.readAndAbandon(3);
    rig.run(I2C_STUCK_MS + 20);

    CHECK(py32::state().nvicDisables > 0);
    CHECK(rig.readFrame()[0] & tile::kStatusLocalFault);
    CHECK(rig.heartbeatMoves());
    CHECK(tile::frameOk(rig.readFrame(), kDataLen));
}

TEST_CASE("a tap between polls survives as sticky bits", "[py32][button]")
{
    Rig rig;
    rig.run(40);
    rig.readFrame();
    rig.run(8);

    rig.pressButton(6, true);
    rig.run(12);
    rig.pressButton(6, false);
    rig.run(12);

    const std::vector<std::uint8_t> frame = rig.readFrame();
    CHECK(frame[1 + kBtnLevel] == 0x00);
    CHECK(frame[1 + kBtnPressed] & 0x40);
    CHECK(frame[1 + kBtnReleased] & 0x40);
}

TEST_CASE("a STATUS-only read consumes no button edge", "[py32][button]")
{
    Rig rig;
    rig.run(40);
    rig.readFrame();
    rig.run(8);

    rig.pressButton(2, true);
    rig.run(12);

    for (int poll = 0; poll < 5; ++poll)
    {
        rig.master.readFrom(tile::kRegStatus, 1);
        rig.run(8);
    }
    CHECK(rig.readFrame()[1 + kBtnPressed] & 0x04);
}

TEST_CASE("TXE and BTF in one snapshot send exactly one byte", "[py32][button]")
{
    Rig rig;
    rig.pressButton(1, true);
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
    CHECK(viaBtf == reference);
}

TEST_CASE("an out-of-range pointer parks instead of landing on SOFT_CMD", "[py32][button]")
{
    Rig rig;
    rig.run(40);
    rig.master.writeRegister(0xF0, {0x01, 0x01, 0x01}); // CMD_RESET, three times
    rig.run(40);

    CHECK(rig.heartbeatMoves());
    CHECK_FALSE(rig.readFrame()[0] & tile::kStatusLocalFault);
}

TEST_CASE("an over-read pads with zeroes and never NACKs", "[py32][button]")
{
    Rig rig;
    rig.run(40);
    const std::vector<std::uint8_t> frame = rig.master.readFrom(tile::kRegStatus, kFrameLen + 6);
    REQUIRE(frame.size() == std::size_t(kFrameLen) + 6u);
    for (std::size_t i = kFrameLen; i < frame.size(); ++i) CHECK(frame[i] == 0x00);
}
