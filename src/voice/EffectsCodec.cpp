// EffectsCodec.cpp — field-by-field flatten/rebuild of the effect record.
#include "EffectsCodec.h"

#include <cstring>

namespace effectscodec
{
void captureEffects(const ReverbSettings &reverb, persistence::EffectsSnapshot &out) noexcept
{
    // Live values can only be in range (setters clamp), but a snapshot must never
    // carry anything the loader would reject.
    const ReverbSettings clean = reverb.sanitized();
    std::memset(&out, 0, sizeof out);
    out.reverbMix = clean.mix;
    out.reverbDecaySeconds = clean.decaySeconds;
    out.reverbDampingHz = clean.dampingHz;
    out.reverbLowCutHz = clean.lowCutHz;
    out.reverbDiffusion = clean.diffusion;
    out.reverbModDepth = clean.modDepth;
    out.reverbModRateHz = clean.modRateHz;
    out.reverbWidth = clean.width;
}

bool applyEffects(const persistence::EffectsSnapshot &in, ReverbSettings &out) noexcept
{
    if (!persistence::validateEffects(in))
        return false;
    ReverbSettings settings;
    settings.mix = in.reverbMix;
    settings.decaySeconds = in.reverbDecaySeconds;
    settings.dampingHz = in.reverbDampingHz;
    settings.lowCutHz = in.reverbLowCutHz;
    settings.diffusion = in.reverbDiffusion;
    settings.modDepth = in.reverbModDepth;
    settings.modRateHz = in.reverbModRateHz;
    settings.width = in.reverbWidth;
    settings.freeze = false; // performance state, never restored
    out = settings;
    return true;
}
} // namespace effectscodec
