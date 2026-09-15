#include "RetainedSession.h"
#include <Arduino.h>

#if defined(ARDUINO)
#include "pico/platform.h"
// NOLOAD section: survives watchdog/warm resets (RAM stays powered), is NOT
// cleared or initialized by crt0, loses content on power-on (validated by
// magic+CRC). The store has no constructor on purpose. Macro wraps the NAME
// only and follows the type (SDK usage: `static T __uninitialized_ram(name);`).
static persistence::RetainedStore __uninitialized_ram(s_store);
#else
static persistence::RetainedStore s_store; // host fallback: ordinary zeroed RAM
#endif

namespace
{
bool s_valid = false;
bool s_consumedResume = false;
} // namespace

void RetainedSession::bootInit()
{
    s_valid = persistence::retainedValid(s_store);
    s_consumedResume = false;
}

bool RetainedSession::resumeAllowed()
{
    if (s_consumedResume)
        return false; // one decision per boot
    s_consumedResume = true;
    if (!s_valid)
        return false;
    if (persistence::decideResume(true, true, s_store.header.resumeAttempts) !=
        persistence::ResumeDecision::ResumeRetained)
        return false;
    s_store.header.resumeAttempts += 1; // retained: escalates across rapid re-freezes
    return true;
}

void RetainedSession::refresh(const persistence::ProjectSnapshotV1 &snap)
{
    const uint8_t attempts = s_store.header.resumeAttempts;
    persistence::retainedRefresh(s_store, snap);
    s_store.header.resumeAttempts = attempts; // refresh never resets the counter
    s_valid = true;
}

void RetainedSession::markBootCompleted()
{
    s_store.header.flags |= persistence::RETAINED_FLAG_BOOT_COMPLETED;
    // A boot that ran the control loop healthy for kHealthyLoopIntervalMs is
    // a good boot: the next watchdog reset gets three fresh resume attempts.
    s_store.header.resumeAttempts = 0;
}

bool RetainedSession::takeResumeSnapshot(persistence::ProjectSnapshotV1 &out)
{
    if (!persistence::retainedValid(s_store))
        return false;
    out = s_store.snapshot;
    return true;
}
