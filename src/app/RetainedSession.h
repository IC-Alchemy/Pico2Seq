#ifndef PICO2SEQ_RETAINED_SESSION_H
#define PICO2SEQ_RETAINED_SESSION_H

#include "../pico2seq-core/persistence/RetainedSessionLogic.h"

namespace RetainedSession
{
void bootInit();
bool resumeAllowed();
void refresh(const persistence::ProjectSnapshot &snap);
// Call ONLY from update() after the control loop has run healthy for
// kHealthyLoopIntervalMs — never from begin(). Resetting the attempts
// counter at the end of begin() would let a freeze that reliably recurs
// shortly after boot resume forever and never reach the park.
void markBootCompleted();
bool takeResumeSnapshot(persistence::ProjectSnapshot &out);
} // namespace RetainedSession

#endif
