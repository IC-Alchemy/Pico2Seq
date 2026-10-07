// UserPresetBank: bank file header and the browser's slot directory.
#include "UserPresetBank.h"

#include <cstring>

namespace persistence
{

void writeUserBankHeader(uint8_t out[kUserBankHeaderSize], uint16_t count) noexcept
{
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<uint8_t>(kUserBankMagic >> (8 * i));
    out[4] = static_cast<uint8_t>(kUserBankVersion);
    out[5] = static_cast<uint8_t>(kUserBankVersion >> 8);
    out[6] = static_cast<uint8_t>(sizeof(UserPresetRecord));
    out[7] = static_cast<uint8_t>(sizeof(UserPresetRecord) >> 8);
    out[8] = static_cast<uint8_t>(count);
    out[9] = static_cast<uint8_t>(count >> 8);
    out[10] = out[11] = 0;
}

BankStatus readUserBankHeader(const uint8_t in[kUserBankHeaderSize], uint16_t &count) noexcept
{
    const uint32_t magic = in[0] | (uint32_t(in[1]) << 8) | (uint32_t(in[2]) << 16) |
                           (uint32_t(in[3]) << 24);
    if (magic != kUserBankMagic)
        return BankStatus::BadMagic;
    if ((in[4] | (uint16_t(in[5]) << 8)) != kUserBankVersion)
        return BankStatus::BadVersion;
    if ((in[6] | (uint16_t(in[7]) << 8)) != sizeof(UserPresetRecord))
        return BankStatus::BadRecordSize;
    const uint16_t n = static_cast<uint16_t>(in[8] | (uint16_t(in[9]) << 8));
    if (n > kUserSlotCount)
        return BankStatus::BadCount;
    count = n;
    return BankStatus::Ok;
}

void UserPresetDirectory::clear() noexcept
{
    for (auto &e : entries_)
        e = UserPresetEntry{};
    count_ = 0;
}

bool UserPresetDirectory::add(const UserPresetRecord &record, uint16_t fileIndex) noexcept
{
    if (!validUserPlace(record.page, record.pad))
        return false;
    UserPresetEntry &e = entries_[userSlotIndex(record.page, record.pad)];
    if (e.used)
        return false;
    e.used = true;
    e.baseIndex = record.baseIndex;
    e.r = record.colorR;
    e.g = record.colorG;
    e.b = record.colorB;
    e.fileIndex = fileIndex;
    std::memcpy(e.name, record.name, kUserPresetNameSize);
    e.name[kUserPresetNameSize - 1] = '\0';
    ++count_;
    return true;
}

const UserPresetEntry *UserPresetDirectory::entry(uint8_t page, uint8_t pad) const noexcept
{
    if (!validUserPlace(page, pad))
        return nullptr;
    const UserPresetEntry &e = entries_[userSlotIndex(page, pad)];
    return e.used ? &e : nullptr;
}

const UserPresetEntry *UserPresetDirectory::slot(uint8_t slotIndex) const noexcept
{
    if (slotIndex >= kUserSlotCount)
        return nullptr;
    const UserPresetEntry &e = entries_[slotIndex];
    return e.used ? &e : nullptr;
}

uint16_t UserPresetDirectory::pageCount(uint8_t page) const noexcept
{
    if (page < 1 || page > kUserPageCount)
        return 0;
    uint16_t n = 0;
    for (uint8_t pad = 0; pad < kUserPadsPerPage; ++pad)
        n += entries_[userSlotIndex(page, pad)].used ? 1 : 0;
    return n;
}

uint8_t UserPresetDirectory::nextPage(uint8_t page) const noexcept
{
    for (uint8_t step = 1; step <= kBrowserPageCount; ++step)
    {
        const uint8_t candidate = static_cast<uint8_t>((page + step) % kBrowserPageCount);
        if (candidate == 0 || pageCount(candidate) != 0)
            return candidate;
    }
    return page;
}

uint8_t UserPresetDirectory::clampPage(uint8_t page) const noexcept
{
    return page != 0 && page < kBrowserPageCount && pageCount(page) != 0 ? page : 0;
}

} // namespace persistence
