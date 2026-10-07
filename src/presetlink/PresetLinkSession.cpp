// PresetLinkSession.cpp - request handlers.
#include "PresetLinkSession.h"

#include "../voice/PatchFields.h"
#include "../voice/VoicePresets.h"
#include <cstring>

namespace presetlink
{
using persistence::UserPresetRecord;

namespace
{
uint16_t le16(const uint8_t *p) noexcept { return static_cast<uint16_t>(p[0] | (uint16_t(p[1]) << 8)); }
void put16(uint8_t *p, uint16_t v) noexcept
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t *p, uint32_t v) noexcept
{
    for (int i = 0; i < 4; ++i)
        p[i] = static_cast<uint8_t>(v >> (8 * i));
}
} // namespace

size_t PresetLinkSession::reply(const FrameParser::Frame &request, const uint8_t *payload,
                                size_t length, uint8_t *out, size_t capacity) noexcept
{
    return encodeFrame(static_cast<uint8_t>(request.type | kReplyBit), request.seq, payload, length,
                       out, capacity);
}

size_t PresetLinkSession::error(const FrameParser::Frame &request, ErrorCode code, uint8_t detail,
                                uint8_t aux, uint8_t *out, size_t capacity) noexcept
{
    const uint8_t payload[sizeof(ErrorPayload)] = {request.type, static_cast<uint8_t>(code), detail, aux};
    return encodeFrame(kErrorType, request.seq, payload, sizeof payload, out, capacity);
}

size_t PresetLinkSession::uploadError(const FrameParser::Frame &request,
                                      const UserPresetStore::UploadError &e, uint8_t *out,
                                      size_t capacity) noexcept
{
    using R = UserPresetStore::Result;
    switch (e.result)
    {
    case R::Invalid:
        return error(request, ErrorCode::InvalidRecord, static_cast<uint8_t>(e.check.problem),
                     e.check.field, out, capacity);
    case R::BadState: return error(request, ErrorCode::BadState, 0, 0xFF, out, capacity);
    case R::Range: return error(request, ErrorCode::OutOfRange, 0, 0xFF, out, capacity);
    case R::NoSpace: return error(request, ErrorCode::NoSpace, 0, 0xFF, out, capacity);
    case R::SlotTaken: return error(request, ErrorCode::SlotTaken, 0, 0xFF, out, capacity);
    case R::CountMismatch: return error(request, ErrorCode::CountMismatch, 0, 0xFF, out, capacity);
    default: return error(request, ErrorCode::Storage, 0, 0xFF, out, capacity);
    }
}

size_t PresetLinkSession::record(const FrameParser::Frame &request, const UserPresetRecord &rec,
                                 uint8_t *out, size_t capacity) noexcept
{
    return reply(request, reinterpret_cast<const uint8_t *>(&rec), sizeof rec, out, capacity);
}

void PresetLinkSession::endUpload(bool committed) noexcept
{
    host_.uploadFinished(committed);
}

void PresetLinkSession::poll(uint32_t nowMs) noexcept
{
    if (store_.uploading() && nowMs - lastUploadActivityMs_ > kUploadIdleTimeoutMs)
    {
        store_.abort();
        endUpload(false);
    }
}

size_t PresetLinkSession::handle(const FrameParser::Frame &request, uint32_t nowMs, uint8_t *out,
                                 size_t capacity) noexcept
{
    lastRequestMs_ = nowMs;
    spoke_ = true;
    const uint8_t *payload = request.payload;
    const size_t length = request.length;

    switch (request.type)
    {
    case Command::Hello:
    {
        HelloReply hello{};
        hello.protocol = kProtocolVersion;
        hello.flags = static_cast<uint8_t>((host_.transportRunning() ? kHelloTransportRunning : 0) |
                                           (store_.uploading() ? kHelloUploading : 0));
        hello.layoutVersion = patchfields::kLayoutVersion;
        hello.recordSize = sizeof(UserPresetRecord);
        hello.patchSize = sizeof(persistence::PatchSnapshot);
        hello.factoryCount = VoicePresets::getPresetCount();
        hello.userPageCount = persistence::kUserPageCount;
        hello.padsPerPage = persistence::kUserPadsPerPage;
        hello.maxPresets = persistence::kUserSlotCount;
        hello.presetCount = store_.fileRecords();
        hello.freeBytes = store_.freeBytes();
        hello.bankBytes = store_.bankBytes();
        hello.tableHash = patchfields::tableHash();
        uint8_t bytes[sizeof hello];
        std::memcpy(bytes, &hello, sizeof hello);
        return reply(request, bytes, sizeof bytes, out, capacity);
    }

    case Command::BankBegin:
    {
        if (length != 2)
            return error(request, ErrorCode::BadPayload, 0, 0xFF, out, capacity);
        if (store_.uploading())
            return error(request, ErrorCode::Busy, 0, 0xFF, out, capacity);
        const uint16_t count = le16(payload);
        host_.uploadStarted(); // quiet the transport before the first flash write
        const auto e = store_.begin(count);
        if (!e.ok())
        {
            endUpload(false);
            return uploadError(request, e, out, capacity);
        }
        lastUploadActivityMs_ = nowMs;
        uint8_t body[6];
        put16(body, count);
        put32(body + 2, static_cast<uint32_t>(persistence::userBankFileSize(count)));
        return reply(request, body, sizeof body, out, capacity);
    }

    case Command::BankPut:
    {
        if (length != sizeof(UserPresetRecord))
            return error(request, ErrorCode::BadPayload, 0, 0xFF, out, capacity);
        if (!store_.uploading())
            return error(request, ErrorCode::BadState, 0, 0xFF, out, capacity);
        std::memcpy(&work_, payload, sizeof work_);
        lastUploadActivityMs_ = nowMs;
        const auto e = store_.put(work_);
        if (!e.ok())
        {
            // A refused record leaves the upload open (the editor may resend or abort),
            // but a storage failure has already closed it.
            if (e.result == UserPresetStore::Result::Storage)
                endUpload(false);
            return uploadError(request, e, out, capacity);
        }
        uint8_t body[3];
        put16(body, store_.received());
        body[2] = persistence::userSlotIndex(work_.page, work_.pad);
        return reply(request, body, sizeof body, out, capacity);
    }

    case Command::BankCommit:
    {
        if (length != 0)
            return error(request, ErrorCode::BadPayload, 0, 0xFF, out, capacity);
        uint32_t crc = 0;
        const auto e = store_.commit(&crc);
        if (!e.ok())
        {
            if (e.result == UserPresetStore::Result::Storage)
                endUpload(false);
            return uploadError(request, e, out, capacity);
        }
        endUpload(true);
        uint8_t body[6];
        put16(body, store_.fileRecords());
        put32(body + 2, crc);
        return reply(request, body, sizeof body, out, capacity);
    }

    case Command::BankAbort:
    {
        const bool was = store_.uploading();
        store_.abort();
        if (was)
            endUpload(false);
        return reply(request, nullptr, 0, out, capacity);
    }

    case Command::BankRead:
    {
        if (length != 2)
            return error(request, ErrorCode::BadPayload, 0, 0xFF, out, capacity);
        if (store_.uploading())
            return error(request, ErrorCode::Busy, 0, 0xFF, out, capacity);
        const auto r = store_.read(le16(payload), work_);
        if (r == UserPresetStore::Result::Range)
            return error(request, ErrorCode::OutOfRange, 0, 0xFF, out, capacity);
        if (r != UserPresetStore::Result::Ok)
            return error(request, ErrorCode::Storage, 0, 0xFF, out, capacity);
        return record(request, work_, out, capacity);
    }

    case Command::Audition:
    {
        if (length != 1 + sizeof(UserPresetRecord))
            return error(request, ErrorCode::BadPayload, 0, 0xFF, out, capacity);
        if (store_.uploading())
            return error(request, ErrorCode::Busy, 0, 0xFF, out, capacity);
        if (payload[0] >= 4)
            return error(request, ErrorCode::OutOfRange, 0, 0xFF, out, capacity);
        std::memcpy(&work_, payload + 1, sizeof work_);
        usercodec::canonicalize(work_);
        const usercodec::Check check = usercodec::validate(work_);
        if (!check.ok())
            return error(request, ErrorCode::InvalidRecord, static_cast<uint8_t>(check.problem),
                         check.field, out, capacity);
        if (!host_.audition(payload[0], work_))
            return error(request, ErrorCode::Storage, 0, 0xFF, out, capacity);
        return reply(request, nullptr, 0, out, capacity);
    }

    case Command::FactoryRead:
    {
        if (length != 1)
            return error(request, ErrorCode::BadPayload, 0, 0xFF, out, capacity);
        if (payload[0] >= VoicePresets::getPresetCount())
            return error(request, ErrorCode::OutOfRange, 0, 0xFF, out, capacity);
        usercodec::fromFactory(payload[0], work_);
        return record(request, work_, out, capacity);
    }

    case Command::VoiceRead:
    {
        if (length != 1)
            return error(request, ErrorCode::BadPayload, 0, 0xFF, out, capacity);
        if (payload[0] >= 4)
            return error(request, ErrorCode::OutOfRange, 0, 0xFF, out, capacity);
        if (!host_.captureVoice(payload[0], work_))
            return error(request, ErrorCode::Storage, 0, 0xFF, out, capacity);
        return record(request, work_, out, capacity);
    }

    default:
        return error(request, ErrorCode::UnknownCommand, 0, 0xFF, out, capacity);
    }
}

} // namespace presetlink
