#ifndef PICO2SEQ_SESSION_STORAGE_H
#define PICO2SEQ_SESSION_STORAGE_H

// SessionStorage: the song on flash (LittleFS) with power-cut safety.
// Musical role: the set survives a pulled plug mid-save. Technical role: mount +
// framed load plus atomic tmp-write-and-rename save. Core 0 only. The snapshot
// (~12.4 KB) is too big for the loop stack, so callers pass a file-static one and
// load() decodes straight into it: no second buffer lives here.

#include "../pico2seq-core/persistence/ProjectSnapshot.h"
#include <cstdint>

namespace SessionStorage
{
// Mount LittleFS. First boot formats (seconds-long: call BEFORE watchdog arm).
bool begin();

enum class LoadResult { Ok, NoFile, IoError, BadFrame };
// `out` is only meaningful after Ok; any other result leaves it as scratch.
LoadResult load(persistence::ProjectSnapshot &out);

// Atomic save: tmp file + rename, so a mid-write power cut keeps old or new.
bool save(const persistence::ProjectSnapshot &snap);
} // namespace SessionStorage

#endif
