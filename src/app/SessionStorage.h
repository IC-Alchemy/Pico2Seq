#ifndef PICO2SEQ_SESSION_STORAGE_H
#define PICO2SEQ_SESSION_STORAGE_H

#include "../pico2seq-core/persistence/ProjectSnapshot.h"
#include <cstdint>

namespace SessionStorage
{
// Mount LittleFS. First boot formats the partition (multi-second: must run
// BEFORE freezeWatchdogArm()). Returns false only if mount+format both fail.
bool begin();

enum class LoadResult { Ok, NoFile, IoError, BadFrame };
LoadResult load(persistence::ProjectSnapshotV1 &out);

// Atomic save: write /session.tmp, then rename over /session.p2s — a power
// cut mid-write leaves either the old file or the new one, never a torn file.
bool save(const persistence::ProjectSnapshotV1 &snap);
} // namespace SessionStorage

#endif
