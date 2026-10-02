// Unit tests for the Alchemy tile wire format (src/AlchemyUI/src/AlchemyProto.h)
// and the hub-side button state machine (TileButton.h). Both are pure C++ —
// no Arduino, no Wire — so the host suite drives them directly.
//
// The regression these exist for: a Button tile (TYPE 0x02) carries its
// button block at DATA offset 0, not at the slider tile's offset 8. Decoding
// one at the slider's offsets reads past its 3-byte DATA block and yields a
// permanently-zero bitmap, i.e. a button tile whose presses never arrive.

#include "AlchemyUI/src/AlchemyProto.h"
#include "AlchemyUI/src/TileButton.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace ap = alchemy;

namespace
{

/** Build a STATUS+DATA+SUM frame for a slider tile (4 faders + button block). */
void buildSliderFrame(std::uint8_t status, const std::uint16_t faders[4],
                      std::uint8_t level, std::uint8_t pressed,
                      std::uint8_t released, std::uint8_t *out)
{
    out[0] = status;
    std::uint8_t *data = out + 1;
    for (std::uint8_t ch = 0; ch < ap::kFadersPerTile; ++ch)
    {
        data[ch * 2] = static_cast<std::uint8_t>(faders[ch] & 0xFF);
        data[ch * 2 + 1] = static_cast<std::uint8_t>(faders[ch] >> 8);
    }
    data[ap::kDataBtnLevel] = level;
    data[ap::kDataBtnPressed] = pressed;
    data[ap::kDataBtnReleased] = released;
    out[1 + ap::kSliderDataLen] = ap::frameSum(status, data, ap::kSliderDataLen);
}

/** Build a STATUS+DATA+SUM frame for a button tile (button block only). */
void buildButtonFrame(std::uint8_t status, std::uint8_t level,
                      std::uint8_t pressed, std::uint8_t released,
                      std::uint8_t *out)
{
    out[0] = status;
    std::uint8_t *data = out + 1;
    data[0] = level;
    data[1] = pressed;
    data[2] = released;
    out[1 + ap::kButtonDataLen] = ap::frameSum(status, data, ap::kButtonDataLen);
}

/** A v2 identity block as a tile answers it from register 0x00. */
void buildIdentity(std::uint8_t typeId, std::uint8_t dataLen, std::uint8_t *raw)
{
    for (std::uint8_t i = 0; i < ap::kIdentityReadLength; ++i) raw[i] = 0;
    raw[ap::kRegWhoAmI] = ap::kWhoAmIMagic;
    raw[ap::kRegTypeId] = typeId;
    raw[ap::kRegProtoVer] = ap::kProtoVerV2;
    raw[ap::kRegFwVer] = 1;
    raw[ap::kRegFwVer + 1] = 0;
    raw[ap::kRegHwRev] = 1;
    raw[ap::kRegAddrOffset] = 2; // floating strap
    raw[ap::kRegCaps] = ap::kCapAnalogIn | ap::kCapDigitalIn;
    raw[ap::kRegDataLen] = dataLen;
}

} // namespace

// ---------------------------------------------------------------------------
// Button block placement — the tile-type bug
// ---------------------------------------------------------------------------

TEST_CASE("the button block sits at a different DATA offset per tile type", "[alchemy_proto]")
{
    CHECK(ap::kButtonDataLen == 3);
    CHECK(ap::kButtonsPerTile == 8);
    // A slider tile prefixes the block with four fader words...
    CHECK(ap::buttonBlockOffset(ap::kTypeSliderButton) == ap::kDataBtnLevel);
    CHECK(ap::kDataBtnLevel == ap::kSliderDataLen - ap::kButtonDataLen);
    // ...a button tile does not, so its block starts at DATA byte 0.
    CHECK(ap::buttonBlockOffset(ap::kTypeButton4) == 0);
}

TEST_CASE("a button tile frame decodes at its own offset", "[alchemy_proto]")
{
    std::uint8_t frame[1 + ap::kSliderDataLen + 1] = {0};
    buildButtonFrame(0x10, /*level=*/0x81, /*pressed=*/0x80, /*released=*/0x01, frame);

    REQUIRE(ap::frameChecksumOk(frame, ap::kButtonDataLen));

    const std::uint8_t *data = frame + 1;
    const std::uint8_t offset = ap::buttonBlockOffset(ap::kTypeButton4);
    const ap::ButtonBlock block =
        ap::decodeButtonBlock(data[offset], data[offset + 1], data[offset + 2]);

    CHECK(block.level == 0x81);    // buttons 0 and 7 (Shift) held
    CHECK(block.pressed == 0x80);  // button 7 pressed since the last read
    CHECK(block.released == 0x01); // button 0 released since the last read

    // The regression: reading it at the slider's offsets lands in bytes the
    // tile never sent, so every button reads as un-pressed forever.
    const ap::ButtonBlock wrong = ap::decodeButtonBlock(
        data[ap::kDataBtnLevel], data[ap::kDataBtnPressed], data[ap::kDataBtnReleased]);
    CHECK(wrong.level == 0);
    CHECK(wrong.pressed == 0);
    CHECK(wrong.released == 0);
}

TEST_CASE("a slider tile frame decodes faders and its own button block", "[alchemy_proto]")
{
    std::uint8_t frame[1 + ap::kSliderDataLen + 1] = {0};
    const std::uint16_t faders[4] = {0x0000, 0x00FF, 0x0F0F, 0x0FFF};
    buildSliderFrame(0x20, faders, /*level=*/0x0A, /*pressed=*/0x08, /*released=*/0x02, frame);

    REQUIRE(ap::frameChecksumOk(frame, ap::kSliderDataLen));

    const std::uint8_t *data = frame + 1;
    CHECK(ap::decodeFader(data, 0) == 0x0000);
    CHECK(ap::decodeFader(data, 1) == 0x00FF); // the high byte really is second
    CHECK(ap::decodeFader(data, 2) == 0x0F0F);
    CHECK(ap::decodeFader(data, 3) == 0x0FFF);

    const std::uint8_t offset = ap::buttonBlockOffset(ap::kTypeSliderButton);
    const ap::ButtonBlock block =
        ap::decodeButtonBlock(data[offset], data[offset + 1], data[offset + 2]);
    CHECK(block.level == 0x0A);
    CHECK(block.pressed == 0x08);
    CHECK(block.released == 0x02);
}

// ---------------------------------------------------------------------------
// STATUS, checksum, identity
// ---------------------------------------------------------------------------

TEST_CASE("STATUS splits into heartbeat, faults and a wrapping SEQ", "[alchemy_proto]")
{
    const ap::FrameStatus idle = ap::decodeStatus(0x31); // SEQ 3, heartbeat set
    CHECK(idle.seq == 3);
    CHECK(idle.heartbeat);
    CHECK_FALSE(idle.localFault);
    CHECK_FALSE(idle.notReady);

    // SEQ is compared for equality only: 15 -> 0 is a change, not a decrease.
    CHECK(ap::seqChanged(15, 0));
    CHECK_FALSE(ap::seqChanged(7, 7));
}

TEST_CASE("a corrupted frame fails its checksum", "[alchemy_proto]")
{
    std::uint8_t frame[1 + ap::kSliderDataLen + 1] = {0};
    buildButtonFrame(0x10, 0x0F, 0x0F, 0x00, frame);
    REQUIRE(ap::frameChecksumOk(frame, ap::kButtonDataLen));

    frame[1] ^= 0x01; // one flipped DATA bit
    CHECK_FALSE(ap::frameChecksumOk(frame, ap::kButtonDataLen));
}

TEST_CASE("identity accepts v2 tiles and rejects everything else", "[alchemy_proto]")
{
    std::uint8_t raw[ap::kIdentityReadLength];

    SECTION("a slider tile")
    {
        buildIdentity(ap::kTypeSliderButton, ap::kSliderDataLen, raw);
        const ap::Identity id = ap::decodeIdentity(raw, ap::kIdentityReadLength);
        REQUIRE(id.valid);
        CHECK(id.typeId == ap::kTypeSliderButton);
        CHECK(id.dataLen == ap::kSliderDataLen);
    }
    SECTION("a button tile")
    {
        buildIdentity(ap::kTypeButton4, ap::kButtonDataLen, raw);
        const ap::Identity id = ap::decodeIdentity(raw, ap::kIdentityReadLength);
        REQUIRE(id.valid);
        CHECK(id.typeId == ap::kTypeButton4);
        CHECK(id.dataLen == ap::kButtonDataLen);
    }
    SECTION("wrong magic is not an Alchemy module")
    {
        buildIdentity(ap::kTypeButton4, ap::kButtonDataLen, raw);
        raw[ap::kRegWhoAmI] = 0x00; // over-read padding, or no device
        CHECK_FALSE(ap::decodeIdentity(raw, ap::kIdentityReadLength).valid);
    }
    SECTION("a short read decodes to invalid, not garbage")
    {
        buildIdentity(ap::kTypeButton4, ap::kButtonDataLen, raw);
        CHECK_FALSE(ap::decodeIdentity(raw, 4).valid);
    }
}

// ---------------------------------------------------------------------------
// TileButton
// ---------------------------------------------------------------------------

TEST_CASE("TileButton turns level plus sticky edges into press, hold and tap", "[alchemy_proto]")
{
    TileButton button;
    TileButton::Options opt;
    opt.holdMilliseconds = 400;
    button.begin(opt, 1000);

    button.update(true, true, false, 1000);
    CHECK(button.pressEdge());
    CHECK(button.held());

    // Held but short of the threshold: no long-press yet.
    button.update(true, false, false, 1300);
    CHECK_FALSE(button.longPress());
    CHECK(button.heldMilliseconds(1300) == 300);

    button.update(true, false, false, 1400);
    CHECK(button.longPress());

    // A spent press produces no tap on release.
    button.update(false, false, true, 1500);
    CHECK_FALSE(button.held());
    CHECK_FALSE(button.releaseTap());
}

TEST_CASE("TileButton needs a poll every pass to age a hold", "[alchemy_proto]")
{
    // The driver feeds the last known levels on idle polls precisely so this
    // works: without an update() crossing the threshold, longPress() never
    // fires no matter how long the button is actually down.
    TileButton button;
    TileButton::Options opt;
    opt.holdMilliseconds = 400;
    button.begin(opt, 0);

    button.update(true, true, false, 0);
    REQUIRE(button.held());
    CHECK(button.heldMilliseconds(5000) == 5000); // level is still down...
    CHECK_FALSE(button.longPress());              // ...but nothing aged it

    button.update(true, false, false, 5000);
    CHECK(button.longPress());
}

TEST_CASE("TileButton catches a press that lands entirely between polls", "[alchemy_proto]")
{
    TileButton button;
    TileButton::Options opt;
    opt.holdMilliseconds = 400;
    button.begin(opt, 0);

    // Level is already back to 0, but the tile's sticky bits recorded both
    // edges — the press must still register and then release as a tap.
    button.update(false, true, true, 100);
    CHECK(button.pressEdge());
    CHECK(button.held());

    button.update(false, false, false, 104);
    CHECK(button.releaseTap());
    CHECK_FALSE(button.held());
}

// ---------------------------------------------------------------------------
// The standardized state packet
// ---------------------------------------------------------------------------

TEST_CASE("the state packet is the agreed 11-byte layout", "[alchemy_proto]")
{
    // Byte-for-byte the format the satellites and the hub agreed on:
    //   0 seq | 1 buttons | 2-3 slider0 | 4-5 slider1 | 6-7 slider2 |
    //   8-9 slider3 | 10 status. A change here is a wire-format change and
    //   breaks every satellite in the field, so it is pinned by a test.
    CHECK(ap::kStatePacketLen == 11);
    CHECK(ap::kPacketSeq == 0);
    CHECK(ap::kPacketButtons == 1);
    CHECK(ap::kPacketSliders == 2);
    CHECK(ap::kPacketStatus == 10);
    CHECK(ap::kPacketSlidersPerTile == 4);
}

TEST_CASE("a state packet round-trips through its wire bytes", "[alchemy_proto]")
{
    ap::StatePacket in;
    in.seq = 0x2A;
    in.buttons = 0x81; // buttons 0 and 7
    in.sliders[0] = 0x0000;
    in.sliders[1] = 0x0123;
    in.sliders[2] = 0x0ABC;
    in.sliders[3] = 0x0FFF;
    in.status = ap::kStatusHeartbeat;

    std::uint8_t wire[ap::kStatePacketLen] = {0};
    ap::encodeStatePacket(in, wire);

    CHECK(wire[0] == 0x2A);
    CHECK(wire[1] == 0x81);
    CHECK(wire[2] == 0x00); // slider 0 occupies bytes 2-3...
    CHECK(wire[3] == 0x00);
    CHECK(wire[4] == 0x23); // ...slider 1 bytes 4-5, low byte first
    CHECK(wire[5] == 0x01);
    CHECK(wire[8] == 0xFF); // slider 3 bytes 8-9
    CHECK(wire[9] == 0x0F);
    CHECK(wire[10] == ap::kStatusHeartbeat);

    const ap::StatePacket out = ap::decodeStatePacket(wire);
    CHECK(out.seq == in.seq);
    CHECK(out.buttons == in.buttons);
    for (std::uint8_t ch = 0; ch < ap::kPacketSlidersPerTile; ++ch)
    {
        CHECK(out.sliders[ch] == in.sliders[ch]);
    }
    CHECK(out.status == in.status);
}

TEST_CASE("the packet status byte carries flags only, never the SEQ nibble", "[alchemy_proto]")
{
    // SEQ has its own byte. Leaving it in the status byte too would mean two
    // fields that can disagree after a partial update.
    ap::StatePacket in;
    in.seq = 9;
    in.status = 0xF0 | ap::kStatusLocalFault; // a v2 STATUS byte's seq nibble

    std::uint8_t wire[ap::kStatePacketLen] = {0};
    ap::encodeStatePacket(in, wire);
    CHECK(wire[ap::kPacketStatus] == ap::kStatusLocalFault);
    CHECK(wire[ap::kPacketSeq] == 9);
}

TEST_CASE("a v2 slider frame maps onto the canonical packet", "[alchemy_proto]")
{
    std::uint8_t frame[1 + ap::kSliderDataLen + 1] = {0};
    const std::uint16_t faders[4] = {0x0001, 0x0100, 0x0800, 0x0FFF};
    const std::uint8_t status = (5u << ap::kStatusSeqShift) | ap::kStatusHeartbeat;
    buildSliderFrame(status, faders, /*level=*/0x0C, /*pressed=*/0x04,
                     /*released=*/0x08, frame);

    const ap::DecodedFrame decoded =
        ap::decodeFrame(ap::kTypeSliderButton, frame, ap::kSliderDataLen);
    REQUIRE(decoded.valid);

    CHECK(decoded.packet.seq == 5);
    CHECK(decoded.packet.buttons == 0x0C);
    CHECK(decoded.packet.status == ap::kStatusHeartbeat);
    for (std::uint8_t ch = 0; ch < 4; ++ch)
    {
        CHECK(decoded.packet.sliders[ch] == faders[ch]);
    }

    // The sticky edges ride alongside the packet and are never part of it:
    // they are true for this one read only.
    CHECK(decoded.edges.pressed == 0x04);
    CHECK(decoded.edges.released == 0x08);
}

TEST_CASE("a v2 button frame maps onto the same packet with no sliders", "[alchemy_proto]")
{
    std::uint8_t frame[1 + ap::kSliderDataLen + 1] = {0};
    buildButtonFrame((3u << ap::kStatusSeqShift), /*level=*/0x81,
                     /*pressed=*/0x80, /*released=*/0x01, frame);

    const ap::DecodedFrame decoded =
        ap::decodeFrame(ap::kTypeButton4, frame, ap::kButtonDataLen);
    REQUIRE(decoded.valid);

    CHECK(decoded.packet.seq == 3);
    CHECK(decoded.packet.buttons == 0x81);
    for (std::uint8_t ch = 0; ch < 4; ++ch)
    {
        CHECK(decoded.packet.sliders[ch] == 0); // a button tile has none
    }
    CHECK(decoded.edges.pressed == 0x80);
    CHECK(decoded.edges.released == 0x01);
}

TEST_CASE("a frame that does not verify decodes to nothing at all", "[alchemy_proto]")
{
    std::uint8_t frame[1 + ap::kSliderDataLen + 1] = {0};
    const std::uint16_t faders[4] = {0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF};
    buildSliderFrame(0x10, faders, 0x0F, 0, 0, frame);
    REQUIRE(ap::decodeFrame(ap::kTypeSliderButton, frame, ap::kSliderDataLen).valid);

    SECTION("a flipped bit takes the whole frame with it")
    {
        frame[3] ^= 0x40; // corrupt a fader byte
        const ap::DecodedFrame decoded =
            ap::decodeFrame(ap::kTypeSliderButton, frame, ap::kSliderDataLen);
        CHECK_FALSE(decoded.valid);
        // No field survives: a plausible-looking fader word out of a corrupt
        // frame is exactly what must not reach the audio path.
        CHECK(decoded.packet.buttons == 0);
        CHECK(decoded.packet.sliders[0] == 0);
    }

    SECTION("a DATA_LEN too short for the tile type is refused")
    {
        // A slider tile that claims a 3-byte DATA block has no room for the
        // button bytes its type puts at offset 8.
        CHECK_FALSE(ap::decodeFrame(ap::kTypeSliderButton, frame, 3).valid);
    }
}
