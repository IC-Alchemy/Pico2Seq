// Preset link: the serial frame format and the command session behind it.
// The session is exercised exactly as the firmware drives it - request frames in, reply
// frames out - against an in-memory bank file and a recording host.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>
#include <vector>

#include "../support/MemoryBankFile.h"
#include "presetlink/PresetLinkProtocol.h"
#include "presetlink/PresetLinkSession.h"
#include "voice/PatchFields.h"
#include "voice/UserPresetCodec.h"
#include "voice/VoiceConfig.h"
#include "voice/VoicePresets.h"

using namespace presetlink;
using persistence::UserPresetRecord;

namespace
{
std::vector<uint8_t> frameBytes(uint8_t type, uint8_t seq, const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> out(kFrameOverhead + payload.size());
    const size_t n = encodeFrame(type, seq, payload.data(), payload.size(), out.data(), out.size());
    REQUIRE(n == out.size());
    return out;
}

// Feeds bytes, collects the frames the parser yields and the bytes it hands back as console text.
struct Wire
{
    FrameParser parser;
    uint32_t now = 1000;
    std::vector<FrameParser::Frame> frames;
    std::vector<std::vector<uint8_t>> payloads;
    std::string console;
    int dropped = 0;

    void feed(const std::vector<uint8_t> &bytes, uint32_t stepMs = 0)
    {
        for (uint8_t b : bytes)
        {
            now += stepMs;
            switch (parser.push(b, now))
            {
            case FrameParser::Push::Console: console.push_back(static_cast<char>(b)); break;
            case FrameParser::Push::Frame:
                frames.push_back(parser.frame());
                payloads.emplace_back(parser.frame().payload, parser.frame().payload + parser.frame().length);
                frames.back().payload = payloads.back().data();
                break;
            case FrameParser::Push::Dropped: ++dropped; break;
            case FrameParser::Push::Pending: break;
            }
        }
    }
};

struct TestHost : PresetLinkHost
{
    bool running = true;
    int started = 0, finished = 0;
    bool lastCommitted = false;
    int auditions = 0;
    uint8_t auditionVoice = 0xFF;
    UserPresetRecord auditioned{};
    bool failAudition = false;
    VoiceConfig voices[4];

    TestHost()
    {
        for (int i = 0; i < 4; ++i)
            voices[i] = VoicePresets::getPresetConfig(static_cast<uint8_t>(i + 2));
    }
    bool transportRunning() override { return running; }
    void uploadStarted() override { ++started; }
    void uploadFinished(bool committed) override
    {
        ++finished;
        lastCommitted = committed;
    }
    bool audition(uint8_t voice, const UserPresetRecord &r) override
    {
        if (failAudition)
            return false;
        ++auditions;
        auditionVoice = voice;
        auditioned = r;
        return true;
    }
    bool captureVoice(uint8_t voice, UserPresetRecord &out) override
    {
        usercodec::fromConfig(voices[voice], static_cast<uint8_t>(voice + 2), out);
        std::strncpy(out.name, "Captured", sizeof out.name - 1);
        usercodec::canonicalize(out);
        return true;
    }
};

struct Reply
{
    bool ok = false;      // a normal reply
    bool isError = false;
    uint8_t type = 0;
    uint8_t seq = 0;
    std::vector<uint8_t> payload;
    ErrorPayload error{};
    uint16_t u16(size_t at) const { return static_cast<uint16_t>(payload.at(at) | (payload.at(at + 1) << 8)); }
    uint32_t u32(size_t at) const { return u16(at) | (uint32_t(u16(at + 2)) << 16); }
};

struct Rig
{
    testsupport::MemoryBankFile file;
    UserPresetStore store{file};
    TestHost host;
    PresetLinkSession session{store, host};
    uint32_t now = 5000;
    uint8_t seq = 0;

    Reply call(uint8_t command, const std::vector<uint8_t> &payload = {})
    {
        ++seq;
        Wire in;
        in.feed(frameBytes(command, seq, payload));
        REQUIRE(in.frames.size() == 1);
        uint8_t out[kMaxFrame];
        const size_t n = session.handle(in.frames[0], now, out, sizeof out);
        REQUIRE(n >= kFrameOverhead);
        Wire back;
        back.feed(std::vector<uint8_t>(out, out + n));
        REQUIRE(back.frames.size() == 1);
        Reply r;
        r.type = back.frames[0].type;
        r.seq = back.frames[0].seq;
        r.payload = back.payloads[0];
        REQUIRE(r.seq == seq); // replies echo the sequence number
        if (r.type == kErrorType)
        {
            r.isError = true;
            REQUIRE(r.payload.size() == sizeof(ErrorPayload));
            std::memcpy(&r.error, r.payload.data(), sizeof r.error);
            REQUIRE(r.error.command == command);
        }
        else
        {
            REQUIRE(r.type == (command | kReplyBit));
            r.ok = true;
        }
        return r;
    }
    Reply call(uint8_t command, const UserPresetRecord &record, int prefix = -1)
    {
        std::vector<uint8_t> payload;
        if (prefix >= 0)
            payload.push_back(static_cast<uint8_t>(prefix));
        const auto *p = reinterpret_cast<const uint8_t *>(&record);
        payload.insert(payload.end(), p, p + sizeof record);
        return call(command, payload);
    }
    static std::vector<uint8_t> u16(uint16_t v) { return {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)}; }
};

UserPresetRecord record(uint8_t base, uint8_t page, uint8_t pad, const char *name)
{
    UserPresetRecord r;
    usercodec::fromFactory(base, r);
    std::memset(r.name, 0, sizeof r.name);
    std::strncpy(r.name, name, sizeof r.name - 1);
    r.page = page;
    r.pad = pad;
    r.colorR = 200;
    r.colorG = 100;
    r.colorB = 50;
    usercodec::canonicalize(r);
    return r;
}

UserPresetRecord toRecord(const Reply &r)
{
    REQUIRE(r.payload.size() == sizeof(UserPresetRecord));
    UserPresetRecord out;
    std::memcpy(&out, r.payload.data(), sizeof out);
    return out;
}
} // namespace

TEST_CASE("frame layout is the documented one", "[presetlink]")
{
    const auto f = frameBytes(0x03, 7, {0xAA, 0xBB, 0xCC});
    REQUIRE(f.size() == 13);
    REQUIRE(f[0] == 0xA5);
    REQUIRE(f[1] == 0x5A);
    REQUIRE(f[2] == 0x03);
    REQUIRE(f[3] == 7);
    REQUIRE(f[4] == 3);
    REQUIRE(f[5] == 0);
    REQUIRE(f[6] == 0xAA);
    // CRC-32 over type..payload, little-endian.
    const uint32_t crc = persistence::crc32(f.data() + 2, 7);
    REQUIRE(f[9] == (crc & 0xFF));
    REQUIRE(f[12] == (crc >> 24));
    STATIC_REQUIRE(kFrameOverhead == 10);
    STATIC_REQUIRE(kMaxPayload >= sizeof(UserPresetRecord) + 1);
    STATIC_REQUIRE(sizeof(HelloReply) == 28);
}

TEST_CASE("encoder refuses frames that do not fit", "[presetlink]")
{
    uint8_t out[kMaxFrame];
    uint8_t payload[kMaxPayload + 1] = {};
    REQUIRE(encodeFrame(1, 1, payload, kMaxPayload + 1, out, sizeof out) == 0);
    REQUIRE(encodeFrame(1, 1, payload, 4, out, 13) == 0);
    REQUIRE(encodeFrame(1, 1, payload, 4, out, 14) == 14);
    REQUIRE(encodeFrame(1, 1, nullptr, 4, out, sizeof out) == 0);
    REQUIRE(encodeFrame(1, 1, nullptr, 0, out, sizeof out) == kFrameOverhead);
}

TEST_CASE("parser accepts frames fed one byte at a time", "[presetlink]")
{
    Wire w;
    w.feed(frameBytes(0x01, 1, {}));
    w.feed(frameBytes(0x02, 2, {1, 2, 3, 4}));
    std::vector<uint8_t> big(kMaxPayload);
    for (size_t i = 0; i < big.size(); ++i)
        big[i] = static_cast<uint8_t>(i * 7);
    w.feed(frameBytes(0x03, 3, big));
    REQUIRE(w.frames.size() == 3);
    REQUIRE(w.frames[0].length == 0);
    REQUIRE(w.frames[1].type == 0x02);
    REQUIRE(w.payloads[1] == std::vector<uint8_t>({1, 2, 3, 4}));
    REQUIRE(w.payloads[2] == big);
    REQUIRE(w.console.empty());
    REQUIRE(w.dropped == 0);
}

TEST_CASE("log text and stray bytes come back as console input", "[presetlink]")
{
    Wire w;
    const std::string text = "[DIAG C0] ids=1,2,3,4 W\n";
    w.feed(std::vector<uint8_t>(text.begin(), text.end()));
    w.feed(frameBytes(0x01, 1, {0x57 /* 'W' inside a frame must not leak */}));
    w.feed({'W'});
    REQUIRE(w.console == text + "W");
    REQUIRE(w.frames.size() == 1);
    REQUIRE(w.payloads[0][0] == 0x57);
}

TEST_CASE("a corrupted frame is dropped and the next one still arrives", "[presetlink]")
{
    Wire w;
    auto bad = frameBytes(0x03, 1, {9, 9, 9});
    bad[7] ^= 0x01; // payload bit flip
    w.feed(bad);
    REQUIRE(w.dropped == 1);
    REQUIRE(w.frames.empty());
    w.feed(frameBytes(0x01, 2, {}));
    REQUIRE(w.frames.size() == 1);
    REQUIRE(w.frames[0].seq == 2);

    // An impossible length is rejected at once, not after waiting for 65 KB.
    Wire x;
    x.feed({0xA5, 0x5A, 0x03, 0x01, 0xFF, 0xFF});
    REQUIRE(x.dropped == 1);
}

TEST_CASE("a duplicated sync byte does not lose the frame", "[presetlink]")
{
    Wire w;
    auto f = frameBytes(0x01, 4, {1});
    f.insert(f.begin(), 0xA5);
    w.feed(f);
    REQUIRE(w.frames.size() == 1);
    REQUIRE(w.frames[0].seq == 4);
}

TEST_CASE("a half-received frame times out instead of eating the next one", "[presetlink]")
{
    Wire w;
    const auto half = frameBytes(0x03, 1, std::vector<uint8_t>(100, 1));
    w.feed(std::vector<uint8_t>(half.begin(), half.begin() + 50));
    REQUIRE(w.parser.inFrame());
    w.now += kByteTimeoutMs + 1;
    w.feed(frameBytes(0x01, 2, {}));
    REQUIRE(w.frames.size() == 1);
    REQUIRE(w.frames[0].seq == 2);

    // Slow but steady bytes (each inside the timeout) still assemble.
    Wire slow;
    slow.feed(frameBytes(0x03, 5, {1, 2, 3}), kByteTimeoutMs - 10);
    REQUIRE(slow.frames.size() == 1);
}

TEST_CASE("hello describes the device", "[presetlink][session]")
{
    Rig rig;
    const Reply r = rig.call(Command::Hello);
    REQUIRE(r.ok);
    REQUIRE(r.payload.size() == sizeof(HelloReply));
    HelloReply h;
    std::memcpy(&h, r.payload.data(), sizeof h);
    REQUIRE(h.protocol == kProtocolVersion);
    REQUIRE((h.flags & kHelloTransportRunning) != 0);
    REQUIRE((h.flags & kHelloUploading) == 0);
    REQUIRE(h.layoutVersion == patchfields::kLayoutVersion);
    REQUIRE(h.recordSize == sizeof(UserPresetRecord));
    REQUIRE(h.patchSize == sizeof(persistence::PatchSnapshot));
    REQUIRE(h.factoryCount == VoicePresets::getPresetCount());
    REQUIRE(h.userPageCount == persistence::kUserPageCount);
    REQUIRE(h.padsPerPage == persistence::kUserPadsPerPage);
    REQUIRE(h.maxPresets == persistence::kUserSlotCount);
    REQUIRE(h.presetCount == 0);
    REQUIRE(h.tableHash == patchfields::tableHash());
    REQUIRE(h.freeBytes == rig.file.capacity);
}

TEST_CASE("a bank uploads, commits and reads back", "[presetlink][session]")
{
    Rig rig;
    std::vector<UserPresetRecord> bank = {record(2, 1, 0, "First"), record(9, 1, 5, "Second"),
                                          record(6, 2, 30, "Third")};
    const Reply begin = rig.call(Command::BankBegin, Rig::u16(3));
    REQUIRE(begin.ok);
    REQUIRE(begin.u16(0) == 3);
    REQUIRE(begin.u32(2) == persistence::userBankFileSize(3));
    REQUIRE(rig.host.started == 1);

    for (size_t i = 0; i < bank.size(); ++i)
    {
        const Reply put = rig.call(Command::BankPut, bank[i]);
        REQUIRE(put.ok);
        REQUIRE(put.u16(0) == i + 1);
        REQUIRE(put.payload[2] == persistence::userSlotIndex(bank[i].page, bank[i].pad));
    }
    const Reply hello = rig.call(Command::Hello);
    HelloReply h;
    std::memcpy(&h, hello.payload.data(), sizeof h);
    REQUIRE((h.flags & kHelloUploading) != 0);

    const Reply commit = rig.call(Command::BankCommit);
    REQUIRE(commit.ok);
    REQUIRE(commit.u16(0) == 3);
    REQUIRE(rig.host.finished == 1);
    REQUIRE(rig.host.lastCommitted);
    // The CRC in the reply is the CRC of the file the device wrote.
    REQUIRE(commit.u32(2) == persistence::crc32(rig.file.live.data(), rig.file.live.size() - 4));
    REQUIRE(rig.store.count() == 3);

    for (size_t i = 0; i < bank.size(); ++i)
    {
        const Reply read = rig.call(Command::BankRead, Rig::u16(static_cast<uint16_t>(i)));
        REQUIRE(read.ok);
        const UserPresetRecord got = toRecord(read);
        REQUIRE(std::memcmp(&got, &bank[i], sizeof got) == 0);
    }
    REQUIRE(rig.call(Command::BankRead, Rig::u16(3)).error.code == uint8_t(ErrorCode::OutOfRange));
}

TEST_CASE("an invalid record is refused with the offending field and the upload stays open", "[presetlink][session]")
{
    Rig rig;
    REQUIRE(rig.call(Command::BankBegin, Rig::u16(2)).ok);
    UserPresetRecord bad = record(2, 1, 0, "Bad");
    const int res = patchfields::indexOfKey("filter.resonance");
    patchfields::write(bad.patch, patchfields::field(static_cast<size_t>(res)), 4.0f);
    const Reply r = rig.call(Command::BankPut, bad);
    REQUIRE(r.isError);
    REQUIRE(r.error.code == uint8_t(ErrorCode::InvalidRecord));
    REQUIRE(r.error.detail == uint8_t(usercodec::Problem::BadField));
    REQUIRE(r.error.aux == res);

    // The editor fixes it and carries on.
    REQUIRE(rig.call(Command::BankPut, record(2, 1, 0, "Good")).ok);
    const Reply dup = rig.call(Command::BankPut, record(3, 1, 0, "Same Pad"));
    REQUIRE(dup.error.code == uint8_t(ErrorCode::SlotTaken));
    REQUIRE(rig.call(Command::BankCommit).error.code == uint8_t(ErrorCode::CountMismatch));
    REQUIRE(rig.call(Command::BankPut, record(3, 1, 1, "Two")).ok);
    REQUIRE(rig.call(Command::BankCommit).ok);
    REQUIRE(rig.store.count() == 2);
}

TEST_CASE("abort and timeout keep the old bank and give the transport back", "[presetlink][session]")
{
    Rig rig;
    REQUIRE(rig.call(Command::BankBegin, Rig::u16(1)).ok);
    REQUIRE(rig.call(Command::BankPut, record(2, 1, 0, "Old")).ok);
    REQUIRE(rig.call(Command::BankCommit).ok);
    const auto oldFile = rig.file.live;

    SECTION("abort")
    {
        REQUIRE(rig.call(Command::BankBegin, Rig::u16(1)).ok);
        REQUIRE(rig.call(Command::BankPut, record(3, 1, 4, "New")).ok);
        REQUIRE(rig.call(Command::BankAbort).ok);
        REQUIRE(rig.host.started == 2);
        REQUIRE(rig.host.finished == 2);
        REQUIRE_FALSE(rig.host.lastCommitted);
        REQUIRE(rig.file.live == oldFile);
        // Aborting with nothing open is harmless and does not notify the host again.
        REQUIRE(rig.call(Command::BankAbort).ok);
        REQUIRE(rig.host.finished == 2);
    }
    SECTION("the editor goes quiet")
    {
        REQUIRE(rig.call(Command::BankBegin, Rig::u16(1)).ok);
        rig.session.poll(rig.now + PresetLinkSession::kUploadIdleTimeoutMs - 1);
        REQUIRE(rig.store.uploading());
        rig.session.poll(rig.now + PresetLinkSession::kUploadIdleTimeoutMs + 1);
        REQUIRE_FALSE(rig.store.uploading());
        REQUIRE(rig.host.finished == 2);
        REQUIRE_FALSE(rig.host.lastCommitted);
        REQUIRE(rig.file.live == oldFile);
    }
    SECTION("activity keeps the upload alive")
    {
        REQUIRE(rig.call(Command::BankBegin, Rig::u16(2)).ok);
        rig.now += 4000;
        REQUIRE(rig.call(Command::BankPut, record(3, 1, 4, "A")).ok);
        rig.session.poll(rig.now + 4000);
        REQUIRE(rig.store.uploading());
    }
}

TEST_CASE("requests that do not fit the session state are refused", "[presetlink][session]")
{
    Rig rig;
    REQUIRE(rig.call(Command::BankPut, record(2, 1, 0, "Early")).error.code == uint8_t(ErrorCode::BadState));
    REQUIRE(rig.call(Command::BankCommit).error.code == uint8_t(ErrorCode::BadState));
    REQUIRE(rig.call(Command::BankBegin, Rig::u16(63)).error.code == uint8_t(ErrorCode::OutOfRange));
    REQUIRE(rig.host.finished == rig.host.started); // a refused begin never leaves the transport paused
    rig.file.capacity = 100;
    REQUIRE(rig.call(Command::BankBegin, Rig::u16(1)).error.code == uint8_t(ErrorCode::NoSpace));
    REQUIRE(rig.host.finished == rig.host.started);
    rig.file.capacity = 20000;

    REQUIRE(rig.call(Command::BankBegin, std::vector<uint8_t>{1}).error.code == uint8_t(ErrorCode::BadPayload));
    REQUIRE(rig.call(Command::BankBegin, Rig::u16(1)).ok);
    REQUIRE(rig.call(Command::BankBegin, Rig::u16(1)).error.code == uint8_t(ErrorCode::Busy));
    REQUIRE(rig.call(Command::BankRead, Rig::u16(0)).error.code == uint8_t(ErrorCode::Busy));
    REQUIRE(rig.call(Command::Audition, record(2, 1, 0, "X"), 0).error.code == uint8_t(ErrorCode::Busy));
    REQUIRE(rig.call(Command::BankPut, std::vector<uint8_t>(10)).error.code == uint8_t(ErrorCode::BadPayload));
    REQUIRE(rig.call(0x42).error.code == uint8_t(ErrorCode::UnknownCommand));
    REQUIRE(rig.call(Command::BankCommit).error.code == uint8_t(ErrorCode::CountMismatch));
}

TEST_CASE("a flash failure ends the upload and reports it", "[presetlink][session]")
{
    Rig rig;
    rig.file.failWriteAfter = 1; // header ok, first record fails
    REQUIRE(rig.call(Command::BankBegin, Rig::u16(2)).ok);
    const Reply r = rig.call(Command::BankPut, record(2, 1, 0, "A"));
    REQUIRE(r.error.code == uint8_t(ErrorCode::Storage));
    REQUIRE_FALSE(rig.store.uploading());
    REQUIRE(rig.host.finished == 1);
    REQUIRE_FALSE(rig.host.lastCommitted);
}

TEST_CASE("audition plays a record on a voice", "[presetlink][session]")
{
    Rig rig;
    UserPresetRecord r = record(5, 1, 3, "Try Me");
    const Reply ok = rig.call(Command::Audition, r, 2);
    REQUIRE(ok.ok);
    REQUIRE(rig.host.auditions == 1);
    REQUIRE(rig.host.auditionVoice == 2);
    REQUIRE(std::string(rig.host.auditioned.name) == "Try Me");

    // The device canonicalises before playing, so a sloppy editor still gets a legal patch.
    UserPresetRecord sloppy = r;
    sloppy.patch.presetIndex = 0;
    REQUIRE(rig.call(Command::Audition, sloppy, 1).ok);
    REQUIRE(rig.host.auditioned.patch.presetIndex == sloppy.baseIndex);

    REQUIRE(rig.call(Command::Audition, r, 4).error.code == uint8_t(ErrorCode::OutOfRange));
    UserPresetRecord bad = r;
    patchfields::write(bad.patch, patchfields::field(static_cast<size_t>(patchfields::indexOfKey("out.level"))), 9.0f);
    const Reply refused = rig.call(Command::Audition, bad, 0);
    REQUIRE(refused.error.code == uint8_t(ErrorCode::InvalidRecord));
    REQUIRE(rig.host.auditions == 2);
    rig.host.failAudition = true;
    REQUIRE(rig.call(Command::Audition, r, 0).error.code == uint8_t(ErrorCode::Storage));
    REQUIRE(rig.call(Command::Audition, std::vector<uint8_t>{0, 1, 2}).error.code == uint8_t(ErrorCode::BadPayload));
}

TEST_CASE("factory presets can be read as records", "[presetlink][session]")
{
    Rig rig;
    for (uint8_t i = 0; i < VoicePresets::getPresetCount(); ++i)
    {
        const Reply r = rig.call(Command::FactoryRead, std::vector<uint8_t>{i});
        REQUIRE(r.ok);
        const UserPresetRecord got = toRecord(r);
        REQUIRE(std::string(got.name) == VoicePresets::getPresetName(i));
        REQUIRE(got.baseIndex == i);
        UserPresetRecord placed = got;
        placed.page = 1;
        placed.pad = 0;
        REQUIRE(usercodec::validate(placed).ok());
    }
    REQUIRE(rig.call(Command::FactoryRead, std::vector<uint8_t>{VoicePresets::getPresetCount()}).error.code ==
            uint8_t(ErrorCode::OutOfRange));
    REQUIRE(rig.call(Command::FactoryRead).error.code == uint8_t(ErrorCode::BadPayload));
}

TEST_CASE("a voice can be captured into the editor", "[presetlink][session]")
{
    Rig rig;
    rig.host.voices[1].filterRes = 0.66f;
    const Reply r = rig.call(Command::VoiceRead, std::vector<uint8_t>{1});
    REQUIRE(r.ok);
    const UserPresetRecord got = toRecord(r);
    REQUIRE(got.baseIndex == 3);
    REQUIRE(std::string(got.name) == "Captured");
    const int res = patchfields::indexOfKey("filter.resonance");
    REQUIRE(patchfields::read(got.patch, patchfields::field(static_cast<size_t>(res))) == Catch::Approx(0.66f));
    REQUIRE(rig.call(Command::VoiceRead, std::vector<uint8_t>{4}).error.code == uint8_t(ErrorCode::OutOfRange));
}
