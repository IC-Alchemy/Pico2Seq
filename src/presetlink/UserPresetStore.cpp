// UserPresetStore.cpp - streaming bank writer and boot-time scanner.
#include "UserPresetStore.h"

#include "../pico2seq-core/persistence/LittleEndian.h"

#include <cstring>

namespace presetlink
{
using persistence::UserPresetRecord;

uint16_t UserPresetStore::load() noexcept
{
    live_.clear();
    fileRecords_ = 0;
    bankBytes_ = 0;
    const int32_t size = file_.size();
    if (size < static_cast<int32_t>(persistence::kUserBankHeaderSize + persistence::kUserBankTrailerSize))
        return 0;

    uint8_t header[persistence::kUserBankHeaderSize];
    uint16_t count = 0;
    if (!file_.read(0, header, sizeof header) ||
        persistence::readUserBankHeader(header, count) != persistence::BankStatus::Ok ||
        static_cast<size_t>(size) != persistence::userBankFileSize(count))
        return 0;

    // Whole-file CRC first, so a torn file shows nothing rather than half a bank.
    persistence::Crc32 crc;
    crc.update(header, sizeof header);
    for (uint16_t i = 0; i < count; ++i)
    {
        if (!file_.read(persistence::userBankRecordOffset(i), reinterpret_cast<uint8_t *>(&scratch_),
                        sizeof scratch_))
            return 0;
        crc.update(reinterpret_cast<const uint8_t *>(&scratch_), sizeof scratch_);
    }
    uint8_t trailer[persistence::kUserBankTrailerSize];
    if (!file_.read(persistence::userBankRecordOffset(count), trailer, sizeof trailer))
        return 0;
    if (crc.value() != persistence::getLe32(trailer))
        return 0;

    fileRecords_ = count;
    bankBytes_ = static_cast<uint32_t>(size);
    for (uint16_t i = 0; i < count; ++i)
    {
        if (!file_.read(persistence::userBankRecordOffset(i), reinterpret_cast<uint8_t *>(&scratch_),
                        sizeof scratch_))
            continue;
        if (usercodec::validate(scratch_).ok())
            live_.add(scratch_, i);
    }
    return live_.count();
}

UserPresetStore::Result UserPresetStore::read(uint16_t fileIndex, UserPresetRecord &out) noexcept
{
    if (fileIndex >= fileRecords_)
        return Result::Range;
    return file_.read(persistence::userBankRecordOffset(fileIndex), reinterpret_cast<uint8_t *>(&out),
                      sizeof out)
               ? Result::Ok
               : Result::Storage;
}

UserPresetStore::Result UserPresetStore::readSlot(uint8_t slot, UserPresetRecord &out) noexcept
{
    const persistence::UserPresetEntry *entry = live_.slot(slot);
    return entry ? read(entry->fileIndex, out) : Result::Range;
}

UserPresetStore::UploadError UserPresetStore::begin(uint16_t count) noexcept
{
    if (uploading_)
        return {Result::BadState, {}};
    if (count > persistence::kUserSlotCount)
        return {Result::Range, {}};
    if (!file_.canWrite(persistence::userBankFileSize(count)))
        return {Result::NoSpace, {}};
    if (!file_.beginWrite())
        return {Result::Storage, {}};

    uint8_t header[persistence::kUserBankHeaderSize];
    persistence::writeUserBankHeader(header, count);
    if (!file_.write(header, sizeof header))
    {
        file_.abortWrite();
        return {Result::Storage, {}};
    }
    crc_ = persistence::Crc32{};
    crc_.update(header, sizeof header);
    staging_.clear();
    expected_ = count;
    received_ = 0;
    uploading_ = true;
    return {};
}

UserPresetStore::UploadError UserPresetStore::put(UserPresetRecord &record) noexcept
{
    if (!uploading_ || received_ >= expected_)
        return {Result::BadState, {}};
    usercodec::canonicalize(record);
    const usercodec::Check check = usercodec::validate(record);
    if (!check.ok())
        return {Result::Invalid, check};
    if (staging_.entry(record.page, record.pad))
        return {Result::SlotTaken, {}};
    if (!file_.write(reinterpret_cast<const uint8_t *>(&record), sizeof record))
    {
        abort();
        return {Result::Storage, {}};
    }
    crc_.update(reinterpret_cast<const uint8_t *>(&record), sizeof record);
    staging_.add(record, received_);
    ++received_;
    return {};
}

UserPresetStore::UploadError UserPresetStore::commit(uint32_t *crcOut) noexcept
{
    if (!uploading_)
        return {Result::BadState, {}};
    if (received_ != expected_)
        return {Result::CountMismatch, {}};
    const uint32_t value = crc_.value();
    uint8_t trailer[persistence::kUserBankTrailerSize];
    persistence::putLe32(trailer, value);
    if (!file_.write(trailer, sizeof trailer) || !file_.commitWrite())
    {
        abort();
        return {Result::Storage, {}};
    }
    uploading_ = false;
    live_ = staging_;
    fileRecords_ = expected_;
    bankBytes_ = static_cast<uint32_t>(persistence::userBankFileSize(expected_));
    if (crcOut)
        *crcOut = value;
    return {};
}

void UserPresetStore::abort() noexcept
{
    if (uploading_)
        file_.abortWrite();
    uploading_ = false;
    expected_ = received_ = 0;
}

} // namespace presetlink
