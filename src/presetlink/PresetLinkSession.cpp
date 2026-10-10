// PresetLinkSession.cpp - request handlers.
#include "PresetLinkSession.h"

#include "../pico2seq-core/persistence/LittleEndian.h"
#include "../voice/PatchFields.h"
#include "../voice/VoicePresets.h"
#include "../voice/VoiceSystem.h"
#include <cstring>

namespace presetlink
{
using persistence::UserPresetRecord;

// A validation error's field byte goes onto the wire unchanged as ErrorPayload::aux.
static_assert(kNoField == usercodec::kNoField, "wire and codec must agree on 'no field'");

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

size_t PresetLinkSession::fail(const FrameParser::Frame &request, ErrorCode code, uint8_t *out,
                               size_t capacity) noexcept
{
    return error(request, code, 0, kNoField, out, capacity);
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
    case R::BadState: return fail(request, ErrorCode::BadState, out, capacity);
    case R::Range: return fail(request, ErrorCode::OutOfRange, out, capacity);
    case R::NoSpace: return fail(request, ErrorCode::NoSpace, out, capacity);
    case R::SlotTaken: return fail(request, ErrorCode::SlotTaken, out, capacity);
    case R::CountMismatch: return fail(request, ErrorCode::CountMismatch, out, capacity);
    default: return fail(request, ErrorCode::Storage, out, capacity);
    }
}

size_t PresetLinkSession::record(const FrameParser::Frame &request, const UserPresetRecord &rec,
                                 uint8_t *out, size_t capacity) noexcept
{
    // The record is its own wire format: fixed 256 bytes, layout locked by the
    // static_asserts beside UserPresetRecord, and both ends are little-endian.
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
            return fail(request, ErrorCode::BadPayload, out, capacity);
        if (store_.uploading())
            return fail(request, ErrorCode::Busy, out, capacity);
        const uint16_t count = persistence::getLe16(payload);
        host_.uploadStarted(); // quiet the transport before the first flash write
        const auto e = store_.begin(count);
        if (!e.ok())
        {
            endUpload(false);
            return uploadError(request, e, out, capacity);
        }
        lastUploadActivityMs_ = nowMs;
        uint8_t body[6];
        persistence::putLe16(body, count);
        persistence::putLe32(body + 2, static_cast<uint32_t>(persistence::userBankFileSize(count)));
        return reply(request, body, sizeof body, out, capacity);
    }

    case Command::BankPut:
    {
        if (length != sizeof(UserPresetRecord))
            return fail(request, ErrorCode::BadPayload, out, capacity);
        if (!store_.uploading())
            return fail(request, ErrorCode::BadState, out, capacity);
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
        persistence::putLe16(body, store_.received());
        body[2] = persistence::userSlotIndex(work_.page, work_.pad);
        return reply(request, body, sizeof body, out, capacity);
    }

    case Command::BankCommit:
    {
        if (length != 0)
            return fail(request, ErrorCode::BadPayload, out, capacity);
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
        persistence::putLe16(body, store_.fileRecords());
        persistence::putLe32(body + 2, crc);
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
            return fail(request, ErrorCode::BadPayload, out, capacity);
        if (store_.uploading())
            return fail(request, ErrorCode::Busy, out, capacity);
        const auto r = store_.read(persistence::getLe16(payload), work_);
        if (r == UserPresetStore::Result::Range)
            return fail(request, ErrorCode::OutOfRange, out, capacity);
        if (r != UserPresetStore::Result::Ok)
            return fail(request, ErrorCode::Storage, out, capacity);
        return record(request, work_, out, capacity);
    }

    case Command::Audition:
    {
        if (length != 1 + sizeof(UserPresetRecord))
            return fail(request, ErrorCode::BadPayload, out, capacity);
        if (store_.uploading())
            return fail(request, ErrorCode::Busy, out, capacity);
        if (payload[0] >= VoiceSystem::MAX_VOICES)
            return fail(request, ErrorCode::OutOfRange, out, capacity);
        std::memcpy(&work_, payload + 1, sizeof work_);
        usercodec::canonicalize(work_);
        const usercodec::Check check = usercodec::validate(work_);
        if (!check.ok())
            return error(request, ErrorCode::InvalidRecord, static_cast<uint8_t>(check.problem),
                         check.field, out, capacity);
        if (!host_.audition(payload[0], work_))
            return fail(request, ErrorCode::Storage, out, capacity);
        return reply(request, nullptr, 0, out, capacity);
    }

    case Command::FactoryRead:
    {
        if (length != 1)
            return fail(request, ErrorCode::BadPayload, out, capacity);
        if (payload[0] >= VoicePresets::getPresetCount())
            return fail(request, ErrorCode::OutOfRange, out, capacity);
        usercodec::fromFactory(payload[0], work_);
        return record(request, work_, out, capacity);
    }

    case Command::VoiceRead:
    {
        if (length != 1)
            return fail(request, ErrorCode::BadPayload, out, capacity);
        if (payload[0] >= VoiceSystem::MAX_VOICES)
            return fail(request, ErrorCode::OutOfRange, out, capacity);
        if (!host_.captureVoice(payload[0], work_))
            return fail(request, ErrorCode::Storage, out, capacity);
        return record(request, work_, out, capacity);
    }

    default:
        return fail(request, ErrorCode::UnknownCommand, out, capacity);
    }
}

} // namespace presetlink
