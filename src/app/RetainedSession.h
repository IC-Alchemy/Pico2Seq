#ifndef PICO2SEQ_RETAINED_SESSION_H
#define PICO2SEQ_RETAINED_SESSION_H

// RetainedSession: the crash-mat under the performer's song.
// Musical role: after a watchdog reboot the groove resumes where it froze instead
// of factory-resetting mid-set. Technical role: thin wrapper over portable logic
// in RetainedSessionLogic.h plus a NOLOAD RAM store (see .cpp). Core 0 only.

#include "../pico2seq-core/persistence/RetainedSessionLogic.h"

namespace RetainedSession
{
void bootInit();
bool resumeAllowed();
void refresh(const persistence::ProjectSnapshot &snap);
// Call ONLY from update() after a proven-healthy loop — never from begin().
// Resetting at the end of begin() would let a freeze that recurs right after
// every boot resume forever instead of parking for a power cycle.
void markBootCompleted();
bool takeResumeSnapshot(persistence::ProjectSnapshot &out);
} // namespace RetainedSession

#endif
