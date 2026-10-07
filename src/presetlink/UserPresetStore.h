// UserPresetStore - the user preset bank on flash, behind a seam the host tests can fake.
// Musical role: your own presets survive power-off, and a bank that was half-sent or torn
// by a pulled cable never replaces the one you have.
// Technical role: streams an uploaded bank record by record into a temp file (CRC trailing),
// swaps it in atomically on commit, and rebuilds the browser directory by scanning the live
// file at boot. Holds one 256-byte scratch record and two small directories - never the bank.
// Core 0 control thread only. Portable C++; storage is injected (LittleFS in the firmware).
#ifndef PICO2SEQ_USER_PRESET_STORE_H
#define PICO2SEQ_USER_PRESET_STORE_H

#include "../pico2seq-core/persistence/SnapshotFormat.h"
#include "../pico2seq-core/persistence/UserPresetBank.h"
#include "../voice/UserPresetCodec.h"
#include <cstddef>
#include <cstdint>

namespace presetlink
{

// The bank file as the store needs it. One live file, one temp file being written.
class UserPresetFile
{
public:
    virtual ~UserPresetFile() = default;
    // Live file size in bytes, or -1 when there is none.
    virtual int32_t size() = 0;
    virtual bool read(size_t offset, uint8_t *data, size_t length) = 0;
    // Flash left for a file of `bytes` (includes whatever slack the filesystem needs while
    // the old file still exists).
    virtual bool canWrite(size_t bytes) = 0;
    virtual uint32_t freeBytes() = 0;
    // Temp file: begin truncates, write appends, commit renames it over the live file,
    // abort deletes it. A write that fails leaves the live file untouched.
    virtual bool beginWrite() = 0;
    virtual bool write(const uint8_t *data, size_t length) = 0;
    virtual bool commitWrite() = 0;
    virtual void abortWrite() = 0;
};

class UserPresetStore
{
public:
    explicit UserPresetStore(UserPresetFile &file) : file_(file) {}

    enum class Result : uint8_t
    {
        Ok,
        BadState,  // begin while uploading, put/commit while idle
        Range,     // count above the browser's capacity, or a read past the end
        NoSpace,
        Storage,
        Invalid,   // record failed validation: see UploadError::check
        SlotTaken,
        CountMismatch
    };
    struct UploadError
    {
        Result result = Result::Ok;
        usercodec::Check check; // Invalid only
        bool ok() const noexcept { return result == Result::Ok; }
    };

    // Scans the live file and rebuilds the directory. Records that fail validation are
    // left out (a firmware update may tighten a limit); a damaged file leaves the bank empty.
    // Returns the number of presets the browser can show.
    uint16_t load() noexcept;

    const persistence::UserPresetDirectory &directory() const noexcept { return live_; }
    uint16_t count() const noexcept { return live_.count(); }
    // Size of the live bank file in bytes (0 when none).
    uint32_t bankBytes() const noexcept { return bankBytes_; }
    uint32_t freeBytes() noexcept { return file_.freeBytes(); }
    // How many records the live file holds (>= count() when some failed validation).
    uint16_t fileRecords() const noexcept { return fileRecords_; }

    Result read(uint16_t fileIndex, persistence::UserPresetRecord &out) noexcept;
    Result readSlot(uint8_t slot, persistence::UserPresetRecord &out) noexcept;

    UploadError begin(uint16_t count) noexcept;
    // `record` is canonicalised in place, then validated; the stored bytes are the canonical ones.
    UploadError put(persistence::UserPresetRecord &record) noexcept;
    UploadError commit(uint32_t *crcOut = nullptr) noexcept;
    void abort() noexcept;

    bool uploading() const noexcept { return uploading_; }
    uint16_t received() const noexcept { return received_; }
    uint16_t expected() const noexcept { return expected_; }

private:
    UserPresetFile &file_;
    persistence::UserPresetDirectory live_;
    persistence::UserPresetDirectory staging_;
    persistence::UserPresetRecord scratch_{};
    persistence::Crc32 crc_;
    uint16_t fileRecords_ = 0;
    uint32_t bankBytes_ = 0;
    uint16_t expected_ = 0;
    uint16_t received_ = 0;
    bool uploading_ = false;
};

} // namespace presetlink

#endif
