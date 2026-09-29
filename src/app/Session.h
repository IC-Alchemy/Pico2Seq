#ifndef PICO2SEQ_SESSION_H
#define PICO2SEQ_SESSION_H

// Session: the performer's song as data (patterns + patches + groove settings).
// Musical role: save keeps tonight's set; load brings it back on stage. Technical
// role: capture/apply a portable ProjectSnapshot in three ordered stages (see .cpp).
// Core 0 only; flash I/O itself lives in SessionStorage.

#include "../pico2seq-core/persistence/ProjectSnapshot.h"

namespace Session
{
enum class Source { Defaults, Flash, RetainedRam };

// True once a valid snapshot was applied at boot (flash or retained RAM).
extern bool g_bootLoadedOk;

void captureSession(persistence::ProjectSnapshot &out);
void applyBeforeVoices(const persistence::ProjectSnapshot &s);
// Non-const: a format-1 snapshot's offset lanes convert in place before the sequencers.
void applyAfterVoices(persistence::ProjectSnapshot &s);
void applyAfterClock(const persistence::ProjectSnapshot &s);

// Save/load requests: UI sets, Application::update() runs — never from scan/ISR.
enum class PendingAction { None, Save, Load };
void requestSave();
void requestLoad();
PendingAction consumePendingAction();
uint32_t lastSavedCrc();
void setLastSavedCrc(uint32_t crc);
} // namespace Session

#endif
