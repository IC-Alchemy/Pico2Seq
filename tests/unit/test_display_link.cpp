// Unit tests for the round-display wire protocol
// (src/RoundDisplay/display_link.h). It is pure C++ — no Arduino, no Wire —
// so the host suite drives it directly.
//
// The regressions these exist for: this header is manually mirrored into the
// PY32 repo, so every wire constant, body size and byte order here is a
// cross-repo contract. The suite pins the register map verbatim, the packed
// body sizes, the little-endian theme words and the derived-bodyLen frame
// shape, so a mirror that drifted cannot pass.

#include "RoundDisplay/display_link.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>

namespace rd = rdisplay;

// The body sizes are cross-repo contract: pin them at compile time too, not
// just as runtime CHECKs, so a packing mistake cannot even build the suite.
static_assert(sizeof(rd::VoiceEditorBody) == 30, "mirror contract");
static_assert(sizeof(rd::ModeBannerBody) == 1, "mirror contract");
static_assert(sizeof(rd::NoticeBody) == 28, "mirror contract");
static_assert(sizeof(rd::HeldParamBody) == 39, "mirror contract");
static_assert(sizeof(rd::SettingsTogglesBody) == 25, "mirror contract");
static_assert(sizeof(rd::SettingsPresetsBody) == 26, "mirror contract");
static_assert(sizeof(rd::GateLengthBody) == 2, "mirror contract");
static_assert(sizeof(rd::StepEnvBody) == 11, "mirror contract");
static_assert(sizeof(rd::StatusBody) == 40, "mirror contract");
static_assert(sizeof(rd::ThemeBlock) == 14, "mirror contract");

namespace
{

/** A deterministic theme: distinct, non-zero words, both RGB565 channels mixed. */
void fillTestTheme(rd::ThemeBlock& theme)
{
    theme.voiceHue[0] = rd::toRgb565(200, 30, 30);
    theme.voiceHue[1] = rd::toRgb565(30, 200, 30);
    theme.voiceHue[2] = rd::toRgb565(30, 30, 200);
    theme.voiceHue[3] = rd::toRgb565(180, 180, 40);
    theme.playheadAccent = rd::toRgb565(255, 255, 255);
    theme.backgroundBase = rd::toRgb565(8, 8, 16);
    theme.textAccent = rd::toRgb565(240, 240, 240);
}

/** Wire size of the body each page carries. ParamEdit shares HeldParam's. */
size_t bodyLenFor(rd::PageId page)
{
    return rd::bodySizeForPage(page);
}

/** Fill frame.page's body with a byte pattern derived from the page id, so two
 * pages can never round-trip into each other's bytes unnoticed. */
void fillPatternedBody(rd::PageFrame& frame)
{
    frame.bodyLen = static_cast<uint8_t>(bodyLenFor(frame.page));
    for (size_t i = 0; i < frame.bodyLen; ++i)
        frame.body[i] = static_cast<uint8_t>(31 * i + 11 * static_cast<int>(frame.page));
}

/** A fully filled frame for one page: header fields, test theme, patterned body. */
rd::PageFrame makeTestFrame(rd::PageId page)
{
    rd::PageFrame frame = {};
    frame.protoVer = rd::kProtoVerV1;
    frame.seq = 7;
    frame.page = page;
    frame.themeIdx = 3;
    fillTestTheme(frame.theme);
    fillPatternedBody(frame);
    return frame;
}

/** Wire length of a frame with the given body length: header + theme + SUM + body. */
size_t wireLen(size_t bodyLen)
{
    return 4 + sizeof(rd::ThemeBlock) + 1 + bodyLen;
}

} // namespace

// ---------------------------------------------------------------------------
// The byte-stable mirror contract
// ---------------------------------------------------------------------------

TEST_CASE("register map and identity constants are byte-stable", "[display_link]")
{
    CHECK(rd::kDisplayAddress == 0x3E);
    CHECK(rd::kWhoAmIMagic == 0x5A);
    CHECK(rd::kTypeDisplay == 0x03);
    CHECK(rd::kProtoVerV1 == 0x01);

    CHECK(rd::kRegWhoAmI == 0x00);
    CHECK(rd::kRegTypeId == 0x01);
    CHECK(rd::kRegProtoVer == 0x02);
    CHECK(rd::kRegFwVer == 0x03);
    CHECK(rd::kRegStatus == 0x20);
    CHECK(rd::kRegFrame == 0x21);
    CHECK(rd::kRegStatsFramesOk == 0x24);
    CHECK(rd::kRegStatsSumRejects == 0x25);
    CHECK(rd::kRegStatsFramesRendered == 0x26);
    CHECK(rd::kRegStatsLastSeq == 0x27);

    CHECK(rd::kStatusHeartbeat == 0x01);
    CHECK(rd::kStatusLocalFault == 0x02);
    CHECK(rd::kStatusNotReady == 0x04);
    CHECK(rd::kStatusSeqShift == 4);
    CHECK(rd::kStatusSeqMask == 0x0F);

    CHECK(rd::kMaxBodyBytes == 40);
    CHECK(rd::kMaxFrameBytes == 64);
    CHECK(rd::kMaxFrameBytes >= wireLen(rd::kMaxBodyBytes));
}

TEST_CASE("every page body keeps its packed wire size", "[display_link]")
{
    CHECK(sizeof(rd::VoiceEditorBody) == 30);
    CHECK(sizeof(rd::ModeBannerBody) == 1);
    CHECK(sizeof(rd::NoticeBody) == 28);
    CHECK(sizeof(rd::HeldParamBody) == 39);
    CHECK(sizeof(rd::SettingsTogglesBody) == 25);
    CHECK(sizeof(rd::SettingsPresetsBody) == 26);
    CHECK(sizeof(rd::GateLengthBody) == 2);
    CHECK(sizeof(rd::StepEnvBody) == 11);
    CHECK(sizeof(rd::StatusBody) == 40);
    CHECK(sizeof(rd::ThemeBlock) == 14);
    // Every page's body fits the budget — a page that outgrew it would
    // silently truncate at serialize time otherwise.
    for (int p = 1; p <= 10; ++p)
        CHECK(bodyLenFor(static_cast<rd::PageId>(p)) <= rd::kMaxBodyBytes);
    CHECK(bodyLenFor(static_cast<rd::PageId>(0)) == 0);
    CHECK(bodyLenFor(static_cast<rd::PageId>(11)) == 0);
}

// ---------------------------------------------------------------------------
// SUM and SEQ
// ---------------------------------------------------------------------------

TEST_CASE("frameSum is a truncating uint8 sum", "[display_link]")
{
    const uint8_t wraps[2] = {0xFF, 0x02}; // 0x101 -> low byte
    CHECK(rd::frameSum(wraps, 2) == 0x01);

    const uint8_t big[3] = {0x80, 0x80, 0x80}; // 0x180 -> low byte
    CHECK(rd::frameSum(big, 3) == 0x80);

    CHECK(rd::frameSum(big, 0) == 0); // nothing summed, nothing added
}

TEST_CASE("flipping any single frame byte breaks the SUM", "[display_link]")
{
    const rd::PageFrame frame = makeTestFrame(rd::PageId::Status);
    uint8_t bytes[rd::kMaxFrameBytes] = {0};
    const size_t len = rd::serializeFrame(frame, bytes, sizeof(bytes));
    REQUIRE(len == wireLen(frame.bodyLen));

    const size_t sumIndex = len - 1;
    for (size_t i = 0; i < sumIndex; ++i)
    {
        bytes[i] ^= 0x01;
        CHECK(rd::frameSum(bytes, sumIndex) != bytes[sumIndex]);
        bytes[i] ^= 0x01; // restore for the next flip
    }
}

TEST_CASE("SEQ compares for equality only, never magnitude", "[display_link]")
{
    // 15 -> 0 is a change, not a decrease: the counter wraps at 15.
    CHECK(rd::seqChanged(15, 0));
    CHECK(rd::seqChanged(0, 15));
    CHECK_FALSE(rd::seqChanged(7, 7)); // an identical-SEQ re-send is idempotent
}

// ---------------------------------------------------------------------------
// Theme words
// ---------------------------------------------------------------------------

TEST_CASE("toRgb565 packs the three channels by hand-computed vectors", "[display_link]")
{
    CHECK(rd::toRgb565(255, 255, 255) == 0xFFFF);
    CHECK(rd::toRgb565(0, 0, 0) == 0x0000);
    CHECK(rd::toRgb565(255, 0, 0) == 0xF800);  // red: 31 << 11
    CHECK(rd::toRgb565(0, 255, 0) == 0x07E0);  // green: 63 << 5
    CHECK(rd::toRgb565(0, 0, 255) == 0x001F);  // blue: 31
    // CRGB(60,170,235): 60>>3=7, 170>>2=42, 235>>3=29 -> 7<<11 | 42<<5 | 29.
    CHECK(rd::toRgb565(60, 170, 235) == 0x3D5D);
}

// ---------------------------------------------------------------------------
// Body layout spot check — the mixed-width struct
// ---------------------------------------------------------------------------

TEST_CASE("HeldParamBody lands its fields at the packed wire offsets", "[display_link]")
{
    // The richest body: mixed 8/16-bit and signed fields are where a packing
    // mistake would hide, so pin its offsets byte by byte.
    rd::HeldParamBody held;
    std::memset(&held, 0, sizeof(held));
    held.paramId = 0x5E;
    held.voice = 2;
    held.mode = 1; // LIVE
    held.step = -1; // no step selected
    held.distanceMm = -1; // no sensor reading
    held.handPresent = 0;
    held.value[0] = '7';
    held.base[0] = '0';

    rd::PageFrame frame = makeTestFrame(rd::PageId::HeldParam);
    std::memcpy(frame.body, &held, sizeof(held));

    CHECK(frame.body[0] == 0x5E);
    CHECK(frame.body[1] == 2);
    CHECK(frame.body[2] == 1);
    CHECK(frame.body[3] == 0xFF); // int8_t -1
    CHECK(frame.body[4] == 0xFF); // int16_t -1, low byte first
    CHECK(frame.body[5] == 0xFF); // int16_t -1, high byte
    CHECK(frame.body[6] == 0);
    CHECK(frame.body[7] == '7');  // value[]
    CHECK(frame.body[23] == '0'); // base[]
}

// ---------------------------------------------------------------------------
// Round-trip: every page
// ---------------------------------------------------------------------------

TEST_CASE("every page round-trips through serialize and decode", "[display_link]")
{
    for (int p = 1; p <= 10; ++p)
    {
        const rd::PageId page = static_cast<rd::PageId>(p);
        DYNAMIC_SECTION("page " << p)
        {
            const rd::PageFrame frame = makeTestFrame(page);

            uint8_t bytes[rd::kMaxFrameBytes] = {0};
            const size_t len = rd::serializeFrame(frame, bytes, sizeof(bytes));
            REQUIRE(len == wireLen(frame.bodyLen));
            REQUIRE(len == 4 + 14 + frame.bodyLen + 1);

            rd::PageFrame out = {};
            REQUIRE(rd::decodeFrame(bytes, len, out));
            CHECK(out.protoVer == frame.protoVer);
            CHECK(out.seq == frame.seq);
            CHECK(out.page == frame.page);
            CHECK(out.themeIdx == frame.themeIdx);
            CHECK(out.bodyLen == frame.bodyLen);

            const size_t themeWords = sizeof(rd::ThemeBlock) / sizeof(uint16_t);
            for (size_t w = 0; w < themeWords; ++w)
                CHECK(rd::themeWord(out.theme, w) == rd::themeWord(frame.theme, w));

            // decode zero-fills the tail, and makeTestFrame started from a
            // zeroed frame, so the whole 40-byte body must compare equal.
            CHECK(std::memcmp(out.body, frame.body, rd::kMaxBodyBytes) == 0);
        }
    }
}

TEST_CASE("the theme words hit the wire little-endian", "[display_link]")
{
    const rd::PageFrame frame = makeTestFrame(rd::PageId::VoiceEditor);
    uint8_t bytes[rd::kMaxFrameBytes] = {0};
    REQUIRE(rd::serializeFrame(frame, bytes, sizeof(bytes)) ==
            wireLen(frame.bodyLen));

    // voiceHue[0] = toRgb565(200,30,30) = 25<<11 | 7<<5 | 3 = 0xC8E3:
    // low byte first at offset 4, high byte at 5.
    CHECK(rd::themeWord(frame.theme, 0) == 0xC8E3);
    CHECK(bytes[4] == 0xE3);
    CHECK(bytes[5] == 0xC8);
    // The trailing SUM covers every byte before it, header and theme included.
    CHECK(bytes[wireLen(frame.bodyLen) - 1] == rd::frameSum(bytes, wireLen(frame.bodyLen) - 1));
}

// ---------------------------------------------------------------------------
// Rejection paths
// ---------------------------------------------------------------------------

TEST_CASE("decodeFrame rejects a truncated frame", "[display_link]")
{
    const rd::PageFrame frame = makeTestFrame(rd::PageId::Notice);
    uint8_t bytes[rd::kMaxFrameBytes] = {0};
    const size_t len = rd::serializeFrame(frame, bytes, sizeof(bytes));
    REQUIRE(len == wireLen(frame.bodyLen));

    rd::PageFrame out = {};
    CHECK_FALSE(rd::decodeFrame(bytes, len - 1, out)); // SUM byte swallowed
    CHECK_FALSE(rd::decodeFrame(bytes, 0, out));
    CHECK_FALSE(rd::decodeFrame(bytes, 19, out)); // header + theme + SUM, no body
}

TEST_CASE("decodeFrame rejects a corrupted body byte", "[display_link]")
{
    const rd::PageFrame frame = makeTestFrame(rd::PageId::Notice);
    uint8_t bytes[rd::kMaxFrameBytes] = {0};
    const size_t len = rd::serializeFrame(frame, bytes, sizeof(bytes));
    REQUIRE(len == wireLen(frame.bodyLen));

    bytes[25] ^= 0x01; // one flipped body bit, SUM not updated
    rd::PageFrame out = {};
    CHECK_FALSE(rd::decodeFrame(bytes, len, out));
}

TEST_CASE("decodeFrame rejects a wrong protoVer", "[display_link]")
{
    rd::PageFrame frame = makeTestFrame(rd::PageId::Status);
    frame.protoVer = rd::kProtoVerV1 + 1; // a v2 the mirror must not accept

    uint8_t bytes[rd::kMaxFrameBytes] = {0};
    const size_t len = rd::serializeFrame(frame, bytes, sizeof(bytes));
    REQUIRE(len == wireLen(frame.bodyLen));

    rd::PageFrame out = {};
    CHECK_FALSE(rd::decodeFrame(bytes, len, out));
}

TEST_CASE("decodeFrame rejects an impossible bodyLen", "[display_link]")
{
    // bodyLen is derived from the length, so an over-long frame is an
    // impossible body: 61 bytes claims a 42-byte body, past kMaxBodyBytes.
    uint8_t bytes[61];
    std::memset(bytes, 0, sizeof(bytes));
    bytes[0] = rd::kProtoVerV1;
    bytes[60] = rd::frameSum(bytes, 60); // SUM consistent on purpose

    rd::PageFrame out = {};
    CHECK_FALSE(rd::decodeFrame(bytes, sizeof(bytes), out));
}

TEST_CASE("decodeFrame enforces the page-specific body schema", "[display_link]")
{
    rd::PageFrame frame = makeTestFrame(rd::PageId::ModeBanner);
    uint8_t bytes[rd::kMaxFrameBytes] = {0};
    const size_t len = rd::serializeFrame(frame, bytes, sizeof(bytes));
    REQUIRE(len == wireLen(sizeof(rd::ModeBannerBody)));

    // Change the page id without changing the body length, then repair SUM.
    bytes[2] = static_cast<uint8_t>(rd::PageId::Status);
    bytes[len - 1] = rd::frameSum(bytes, len - 1);
    rd::PageFrame out = {};
    CHECK_FALSE(rd::decodeFrame(bytes, len, out));
}

TEST_CASE("serializeFrame returns 0 instead of writing past the cap", "[display_link]")
{
    const rd::PageFrame frame = makeTestFrame(rd::PageId::Status); // the biggest body
    const size_t need = wireLen(frame.bodyLen);
    REQUIRE(need == 59);

    uint8_t bytes[rd::kMaxFrameBytes] = {0};
    CHECK(rd::serializeFrame(frame, bytes, need - 1) == 0);
    CHECK(rd::serializeFrame(frame, bytes, 0) == 0);
    CHECK(rd::serializeFrame(frame, bytes, need) == need);

    // A body past the budget is rejected outright, never truncated.
    rd::PageFrame oversized = frame;
    oversized.bodyLen = rd::kMaxBodyBytes + 1;
    CHECK(rd::serializeFrame(oversized, bytes, sizeof(bytes)) == 0);

    rd::PageFrame wrongPageBody = frame;
    wrongPageBody.page = rd::PageId::ModeBanner;
    CHECK(rd::serializeFrame(wrongPageBody, bytes, sizeof(bytes)) == 0);
}
