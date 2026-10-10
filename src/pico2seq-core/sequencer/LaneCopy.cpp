// LaneCopy: whole-lane moves between sequencers. See LaneCopy.h.
// Portable C++ — runs on Core 0 with the rest of the sequencer; no heap.
#include "LaneCopy.h"
#include "Sequencer.h"

#include <cmath>

namespace lanecopy
{

float convertValue(ParamId from, ParamId to, float stored) noexcept
{
    const ParameterDefinition *source = parameterDefinition(from);
    const ParameterDefinition *target = parameterDefinition(to);
    if (!source || !target || from == to)
        return stored;

    // "No value of its own" has no number to carry across.
    if (source->patchDefault && followsPatch(stored))
        return target->patchDefault ? SequencerConstants::LANE_FOLLOWS_PATCH
                                    : parameterValueAsFloat(target->defaultValue);

    const float low = parameterValueAsFloat(source->minValue);
    const float high = parameterValueAsFloat(source->maxValue);
    float unit = high > low ? (stored - low) / (high - low) : 0.0f;
    unit = unit < 0.0f ? 0.0f : (unit > 1.0f ? 1.0f : unit);

    // mapNormalizedValueToParamRange() would read an Octave value as a hand
    // distance (its zones are not evenly spaced); here it is a plain 0..1 level.
    if (to == ParamId::Octave)
        return std::floor(unit * 4.0f + 0.5f) * 0.25f;
    return mapNormalizedValueToParamRange(to, unit);
}

void capture(const Sequencer &src, ParamId lane, LaneSnapshot &out) noexcept
{
    out = LaneSnapshot{};
    if (lane >= ParamId::Count)
        return;
    out.lane = lane;
    out.length = src.getParameterStepCount(lane);
    out.loopStart = src.getParameterLoopStart(lane);
    for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
        out.values[step] = src.getRawStepValue(lane, step);
}

bool paste(const LaneSnapshot &in, Sequencer &dst, ParamId lane) noexcept
{
    if (!in.valid() || lane >= ParamId::Count)
        return false;

    // Grow to the full 64 first: lane writes wrap at the active length, so
    // writing a long source's tail into a short lane would land on its head.
    // The grow fill is overwritten right below, then the length settles.
    dst.setParameterStepCount(lane, SequencerConstants::MAX_STEPS_COUNT);
    const bool sameLane = in.lane == lane;
    for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
    {
        if (sameLane)
            dst.setRawStepValue(lane, step, in.values[step]);
        else // clamps, rounds Note, snaps toggles: same rules as any other edit
            dst.setStepParameterValue(lane, step, convertValue(in.lane, lane, in.values[step]));
    }
    dst.setParameterStepCount(lane, in.length);
    dst.setParameterLoopStart(lane, in.loopStart);
    return true;
}

} // namespace lanecopy
