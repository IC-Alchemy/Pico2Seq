// UserPresetBank: presets the player (or the PC editor) made, as plain data.
// Musical role: extra pages on the preset browser. Page 0 is the factory bank
// (VoicePresets); pages 1..kUserPageCount hold user presets, 31 pads each, with pad
// 31 reserved as the page key. Each user preset carries its own name, LED colour and
// grid position.
// Technical role: one fixed-size record per preset, a CRC-framed bank file and a small
// in-RAM directory, so the browser never needs the whole bank in memory.
// Layout is flash-stable and versioned; the field list of the patch inside a record is
// locked by PatchFields (src/voice/PatchFields.h) and mirrored by the PC editor.
// Portable C++ - no Arduino/hardware includes here.
#ifndef PICO2SEQ_USER_PRESET_BANK_H
#define PICO2SEQ_USER_PRESET_BANK_H

#include "ProjectSnapshot.h"
#include <cstddef>
#include <cstdint>

namespace persistence
{

// ---- Browser geometry -------------------------------------------------------------

constexpr uint8_t kUserPadsPerPage = 31;  // pads 0..30 hold presets
constexpr uint8_t kPageKeyPad = 31;       // pad 31 flips to the next page
constexpr uint8_t kFirstUserPage = 1;     // browser page 0 is the factory bank
constexpr uint8_t kUserPageCount = 2;     // browser pages 1 and 2
constexpr uint8_t kBrowserPageCount = 1 + kUserPageCount;
constexpr uint16_t kUserSlotCount = static_cast<uint16_t>(kUserPadsPerPage) * kUserPageCount;
constexpr uint8_t kNoSlot = 0xFF; // "no user preset" in a UI slot variable

// A user slot is (browser page 1.., pad 0..30). Slot indexes run page by page.
constexpr bool validUserPlace(uint8_t page, uint8_t pad) noexcept
{
    return page >= 1 && page <= kUserPageCount && pad < kUserPadsPerPage;
}
constexpr uint8_t userSlotIndex(uint8_t page, uint8_t pad) noexcept
{
    return static_cast<uint8_t>((page - 1) * kUserPadsPerPage + pad);
}
constexpr uint8_t slotPage(uint8_t slot) noexcept { return static_cast<uint8_t>(1 + slot / kUserPadsPerPage); }
constexpr uint8_t slotPad(uint8_t slot) noexcept { return static_cast<uint8_t>(slot % kUserPadsPerPage); }

// ---- Record -----------------------------------------------------------------------

constexpr uint8_t kUserPresetNameSize = 16; // 15 printable ASCII characters + NUL

// One user preset: 24 identity bytes + the 232-byte patch the song file also uses.
// Little-endian, no padding. Every reserved byte is written as zero and must stay zero
// so records compare and hash stably.
struct UserPresetRecord
{
    char name[kUserPresetNameSize]; // NUL-terminated, trailing bytes zero
    uint8_t page;                   // browser page 1..kUserPageCount
    uint8_t pad;                    // 0..30
    uint8_t baseIndex;              // factory preset supplying the flash-resident layout/recipe
    uint8_t colorR, colorG, colorB; // LED colour
    uint8_t flags;                  // zero
    uint8_t reserved;               // zero
    PatchSnapshot patch;            // the sound; patch.presetIndex mirrors baseIndex
};
static_assert(sizeof(UserPresetRecord) == 256, "locked layout");
static_assert(offsetof(UserPresetRecord, patch) == 24, "locked layout");

// ---- Bank file --------------------------------------------------------------------
// [12-byte header][count x 256-byte record][4-byte CRC-32 of header + records]
// The CRC trails, so a writer can stream records to flash without buffering the bank.

constexpr uint32_t kUserBankMagic = 0x42553250u; // bytes 'P','2','U','B'
constexpr uint16_t kUserBankVersion = 1;
constexpr size_t kUserBankHeaderSize = 12;
constexpr size_t kUserBankTrailerSize = 4;
constexpr size_t kUserBankMaxFileSize =
    kUserBankHeaderSize + sizeof(UserPresetRecord) * kUserSlotCount + kUserBankTrailerSize;

constexpr size_t userBankFileSize(uint16_t count) noexcept
{
    return kUserBankHeaderSize + sizeof(UserPresetRecord) * count + kUserBankTrailerSize;
}
constexpr size_t userBankRecordOffset(uint16_t index) noexcept
{
    return kUserBankHeaderSize + sizeof(UserPresetRecord) * index;
}

void writeUserBankHeader(uint8_t out[kUserBankHeaderSize], uint16_t count) noexcept;

enum class BankStatus : uint8_t { Ok, BadMagic, BadVersion, BadRecordSize, BadCount };
// Magic, version, record size and a count the browser can hold. Size and CRC are
// checked by the caller against the whole file.
BankStatus readUserBankHeader(const uint8_t in[kUserBankHeaderSize], uint16_t &count) noexcept;

// ---- Directory --------------------------------------------------------------------
// What the browser needs to draw a page and apply a pad without touching flash: which
// slots are taken, their colours and names, and where each record sits in the file.

struct UserPresetEntry
{
    bool used = false;
    uint8_t baseIndex = 0;
    uint8_t r = 0, g = 0, b = 0;
    uint16_t fileIndex = 0;
    char name[kUserPresetNameSize] = {};
};

class UserPresetDirectory
{
public:
    void clear() noexcept;
    // False when the place is invalid or already taken (the directory is unchanged).
    bool add(const UserPresetRecord &record, uint16_t fileIndex) noexcept;

    const UserPresetEntry *entry(uint8_t page, uint8_t pad) const noexcept;
    const UserPresetEntry *slot(uint8_t slot) const noexcept;
    uint16_t count() const noexcept { return count_; }
    uint16_t pageCount(uint8_t page) const noexcept;

    // The browser page after `page`, wrapping. Page 0 is always reachable; a user page
    // is skipped while it holds no preset. Returns `page` itself when nothing else exists.
    uint8_t nextPage(uint8_t page) const noexcept;
    // `page` if the browser can show it, else page 0 (a user page that has emptied).
    uint8_t clampPage(uint8_t page) const noexcept;

private:
    UserPresetEntry entries_[kUserSlotCount];
    uint16_t count_ = 0;
};

} // namespace persistence

#endif
