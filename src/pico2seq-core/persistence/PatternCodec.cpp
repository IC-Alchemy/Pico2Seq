// PatternCodec: verbatim lane <-> snapshot moves (see header for layout).
// No quantization here — what the player made is what reloads.
#include "PatternCodec.h"
#include "../sequencer/Sequencer.h"
#include <algorithm>

namespace persistence
{

namespace
{
// Lane routing: first kPatternTrackCount lanes in the pattern block, the
// Sustain/Release tail in the envelope block. Keep in ParamId order.
template <typename Pattern, typename Envelopes>
auto &trackFor(Pattern &pattern, Envelopes &envelopes, uint8_t lane) noexcept
{
    return lane < kPatternTrackCount ? pattern.tracks[lane]
                                     : envelopes.tracks[lane - kPatternTrackCount];
}
} // namespace

void capturePattern(const Sequencer &sequencer, PatternSnapshot &out,
                    EnvelopeTracksSnapshot &envelopes) noexcept
{
    for (uint8_t t = 0; t < PARAM_ID_COUNT; ++t)
    {
        const ParamId id = static_cast<ParamId>(t);
        TrackSnapshot &track = trackFor(out, envelopes, t);
        // Zeroed for deterministic bytes on flash; loaders must not interpret.
        track.reserved[0] = 0;
        track.reserved[1] = 0;
        track.reserved[2] = 0;
        track.stepCount = sequencer.getParameterStepCount(id);
        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
            track.values[step] = sequencer.getRawStepValue(id, step);
    }
}

void applyPattern(const PatternSnapshot &in, const EnvelopeTracksSnapshot &envelopes,
                  Sequencer &sequencer, uint8_t maxStepCount) noexcept
{
    for (uint8_t t = 0; t < PARAM_ID_COUNT; ++t)
    {
        const ParamId id = static_cast<ParamId>(t);
        const TrackSnapshot &track = trackFor(in, envelopes, t);
        const uint8_t saved = (track.stepCount >= 1 && track.stepCount <= SequencerConstants::MAX_STEPS_COUNT)
                                  ? track.stepCount : SequencerConstants::DEFAULT_STEPS_COUNT;
        const uint8_t count = std::min(saved, maxStepCount);
        // Grow to MAX first: lane writes wrap at the active length, so loading
        // a short track's tail without growing would corrupt its head. The grow
        // fill is fully overwritten next, then the length shrinks to `count`.
        sequencer.setParameterStepCount(id, SequencerConstants::MAX_STEPS_COUNT);
        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
            sequencer.setRawStepValue(id, step, track.values[step]);
        sequencer.setParameterStepCount(id, count);
    }
}

} // namespace persistence
