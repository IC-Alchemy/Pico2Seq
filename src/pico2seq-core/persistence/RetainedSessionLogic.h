#ifndef PICO2SEQ_RETAINED_SESSION_LOGIC_H
#define PICO2SEQ_RETAINED_SESSION_LOGIC_H

#include "ProjectSnapshot.h"
#include "SnapshotFormat.h"
#include <cstring>

namespace persistence
{

constexpr uint32_t RETAINED_MAGIC = 0x52455431u; // 'RET1'
constexpr uint16_t RETAINED_VERSION = 2; // 2: snapshot format 2
constexpr uint8_t MAX_RESUME_ATTEMPTS = 3;
constexpr uint16_t RETAINED_FLAG_BOOT_COMPLETED = 1u << 0;

struct RetainedHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t generation;
    uint8_t resumeAttempts;
    uint8_t reserved[3];
};

struct RetainedStore
{
    RetainedHeader header;
    ProjectSnapshot snapshot;
    uint32_t crc32; // over snapshot only
};

inline bool retainedValid(const RetainedStore &store) noexcept
{
    if (store.header.magic != RETAINED_MAGIC || store.header.version != RETAINED_VERSION)
        return false;
    return crc32(reinterpret_cast<const uint8_t *>(&store.snapshot), sizeof(store.snapshot)) ==
           store.crc32;
}

enum class ResumeDecision { HaltRecovery, ResumeRetained, NormalBoot };

inline ResumeDecision decideResume(bool watchdogReset, bool retainedValidFlag,
                                   uint8_t resumeAttempts) noexcept
{
    if (!watchdogReset)
        return ResumeDecision::NormalBoot;
    if (retainedValidFlag && resumeAttempts < MAX_RESUME_ATTEMPTS)
        return ResumeDecision::ResumeRetained;
    return ResumeDecision::HaltRecovery;
}

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
