// RetainedSessionLogic: crash recovery for the live song (watchdog reboots).
// A CRC-guarded retained copy replays the song after a glitch; after 3 failed
// resumes it halts for inspection instead of boot-looping. Header-only logic.
#ifndef PICO2SEQ_RETAINED_SESSION_LOGIC_H
#define PICO2SEQ_RETAINED_SESSION_LOGIC_H

#include "ProjectSnapshot.h"
#include "SnapshotFormat.h"
#include <cstring>

namespace persistence
{

constexpr uint32_t RETAINED_MAGIC = 0x52455431u; // 'RET1': guards against stale RAM
constexpr uint16_t RETAINED_VERSION = 2; // Tracks snapshot format 2; bump together
constexpr uint8_t MAX_RESUME_ATTEMPTS = 3; // Past this, halt: the song itself may crash boot
constexpr uint16_t RETAINED_FLAG_BOOT_COMPLETED = 1u << 0;

struct RetainedHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t generation; // Monotonic save count; detects torn updates
    uint8_t resumeAttempts; // Watchdog resumes so far; reset on a clean boot
    uint8_t reserved[3];
};

struct RetainedStore
{
    RetainedHeader header;
    ProjectSnapshot snapshot;
    uint32_t crc32; // Over snapshot only; header has its own magic/version
};

// Trust only magic + version + CRC; a half-written song must never replay.
inline bool retainedValid(const RetainedStore &store) noexcept
{
    if (store.header.magic != RETAINED_MAGIC || store.header.version != RETAINED_VERSION)
        return false;
    return crc32(reinterpret_cast<const uint8_t *>(&store.snapshot), sizeof(store.snapshot)) ==
           store.crc32;
}

enum class ResumeDecision { HaltRecovery, ResumeRetained, NormalBoot };

// Clean boot -> start fresh; watchdog + valid copy -> replay; watchdog with
// corrupt/exhausted copy -> halt for inspection instead of looping.
inline ResumeDecision decideResume(bool watchdogReset, bool retainedValidFlag,
                                   uint8_t resumeAttempts) noexcept
{
    if (!watchdogReset)
        return ResumeDecision::NormalBoot;
    if (retainedValidFlag && resumeAttempts < MAX_RESUME_ATTEMPTS)
        return ResumeDecision::ResumeRetained;
    return ResumeDecision::HaltRecovery;
}

// Checkpoint the live song: stamp, copy, re-CRC. Call on clean saves only.
inline void retainedRefresh(RetainedStore &store, const ProjectSnapshot &snap) noexcept
{
    store.header.magic = RETAINED_MAGIC;
    store.header.version = RETAINED_VERSION;
    store.header.generation += 1u;
    store.snapshot = snap;
    store.crc32 = crc32(reinterpret_cast<const uint8_t *>(&store.snapshot), sizeof(store.snapshot));
}

} // namespace persistence

#endif
