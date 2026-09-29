#ifndef PICO2SEQ_SESSION_STORAGE_H
#define PICO2SEQ_SESSION_STORAGE_H

// SessionStorage: the song on flash (LittleFS) with power-cut safety.
// Musical role: the set survives a pulled plug mid-save. Technical role: mount +
// framed load plus atomic tmp-write-and-rename save. Core 0 only; buffers are
// static (~12.4 KB each) because the loop stack cannot hold them.

#include "../pico2seq-core/persistence/ProjectSnapshot.h"
#include <cstdint>

namespace SessionStorage
{
// Mount LittleFS. First boot formats (seconds-long: call BEFORE watchdog arm).
bool begin();

enum class LoadResult { Ok, NoFile, IoError, BadFrame };
LoadResult load(persistence::ProjectSnapshot &out);

// Atomic save: tmp file + rename, so a mid-write power cut keeps old or new.
bool save(const persistence::ProjectSnapshot &snap);
} // namespace SessionStorage

#endif
