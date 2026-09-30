#ifndef PICO2SEQ_EFFECTS_CODEC_H
#define PICO2SEQ_EFFECTS_CODEC_H

// EffectsCodec — session save/load for the master-bus effect settings (the
// reverb today), the counterpart of PatchCodec for the format-3 effect record.
// captureEffects() flattens ReverbSettings; applyEffects() validates a loaded
// record and rebuilds the settings from it. A restored project always starts
// unfrozen: freeze is performance state and is never stored. Control/session
// thread only; no allocation.
#include "../pico2seq-core/persistence/ProjectSnapshot.h"
#include "ReverbSettings.h"

namespace effectscodec
{
// Every persisted field written, reserved words zeroed. Freeze is not stored.
void captureEffects(const ReverbSettings &reverb, persistence::EffectsSnapshot &out) noexcept;

// False (and `out` untouched) when a field is non-finite or out of range; a loaded
// snapshot has normally been validated already, so this is the last line of defence
// before values are published to the audio thread. On success freeze is off.
bool applyEffects(const persistence::EffectsSnapshot &in, ReverbSettings &out) noexcept;
} // namespace effectscodec

#endif
