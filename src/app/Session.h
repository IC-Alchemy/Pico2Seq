#ifndef PICO2SEQ_SESSION_H
#define PICO2SEQ_SESSION_H

#include "../pico2seq-core/persistence/ProjectSnapshot.h"

namespace Session
{
enum class Source { Defaults, Flash, RetainedRam };

// True when a valid snapshot was applied at boot (flash or retained RAM).
extern bool g_bootLoadedOk;

void captureSession(persistence::ProjectSnapshotV1 &out);
void applyBeforeVoices(const persistence::ProjectSnapshotV1 &s);
void applyAfterVoices(const persistence::ProjectSnapshotV1 &s);
void applyAfterClock(const persistence::ProjectSnapshotV1 &s);
} // namespace Session

#endif
