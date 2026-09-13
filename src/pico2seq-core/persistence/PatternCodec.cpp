#include "PatternCodec.h"
#include "../sequencer/Sequencer.h"

namespace persistence
{

void capturePattern(const Sequencer &sequencer, PatternSnapshot &out) noexcept
{
    for (uint8_t t = 0; t < PARAM_ID_COUNT; ++t)
    {
        const ParamId id = static_cast<ParamId>(t);
        TrackSnapshot &track = out.tracks[t];
        track.reserved[0] = 0;
        track.reserved[1] = 0;
        track.reserved[2] = 0;
        track.stepCount = sequencer.getParameterStepCount(id);
        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
            track.values[step] = sequencer.getRawStepValue(id, step);
    }
}

void applyPattern(const PatternSnapshot &in, Sequencer &sequencer) noexcept
{
    for (uint8_t t = 0; t < PARAM_ID_COUNT; ++t)
    {
        const ParamId id = static_cast<ParamId>(t);
        const TrackSnapshot &track = in.tracks[t];
        const uint8_t count = (track.stepCount >= 1 && track.stepCount <= SequencerConstants::MAX_STEPS_COUNT)
                                  ? track.stepCount : SequencerConstants::DEFAULT_STEPS_COUNT;
        // Grow to MAX first so the writes below land at their true indices —
        // track writes wrap modulo the active length, which would corrupt the
        // head of a shorter track with tail values. Growing fills the new
        // range with the default, but every step is overwritten right after.
        sequencer.setParameterStepCount(id, SequencerConstants::MAX_STEPS_COUNT);
        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
            sequencer.setRawStepValue(id, step, track.values[step]);
        sequencer.setParameterStepCount(id, count);
    }
}

} // namespace persistence
