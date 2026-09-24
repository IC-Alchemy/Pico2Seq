#include "RetainedSession.h"
#include <Arduino.h>

#if defined(ARDUINO)
#include "pico/platform.h"
// NOLOAD RAM: survives watchdog/warm resets (power stays on), lost on power-off.
// Never constructed or cleared by crt0; magic+CRC says if it is still valid.
// Macro wraps the NAME only (SDK form: `static T __uninitialized_ram(name);`).
static persistence::RetainedStore __uninitialized_ram(s_store);
#else
static persistence::RetainedStore s_store; // Host build: plain RAM for tests
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
    s_store.header.resumeAttempts += 1; // Retained across reboots: rapid re-freezes give up
    return true;
}

void RetainedSession::refresh(const persistence::ProjectSnapshot &snap)
{
    const uint8_t attempts = s_store.header.resumeAttempts;
    persistence::retainedRefresh(s_store, snap);
    s_store.header.resumeAttempts = attempts; // A refresh must never clear the give-up count
    s_valid = true;
}

void RetainedSession::markBootCompleted()
{
    s_store.header.flags |= persistence::RETAINED_FLAG_BOOT_COMPLETED;
    // A loop that stayed healthy proves the boot good: grant three fresh resumes.
    s_store.header.resumeAttempts = 0;
}

bool RetainedSession::takeResumeSnapshot(persistence::ProjectSnapshot &out)
{
    if (!persistence::retainedValid(s_store))
        return false;
    out = s_store.snapshot;
    return true;
}
