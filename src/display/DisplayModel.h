#pragma once

#include "LaneDisplay.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../voice/MusicalValues.h"
#include "../pico2seq-core/tuning/TuningState.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace DisplayModel {
inline uint8_t graphValue(ParamId id, float played) noexcept {
    const auto *definition = parameterDefinition(id);
    if (!definition) return 0;
    // Bit-pattern check survives the firmware's -ffast-math. A corrupt float
    // must not reach float-to-integer conversion or array geometry.
    uint32_t bits;
    std::memcpy(&bits, &played, sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u) return 0;
    const float low = parameterValueAsFloat(definition->minValue);
    const float high = parameterValueAsFloat(definition->maxValue);
    const float normalized = std::clamp((played - low) / (high - low), 0.0f, 1.0f);
    return static_cast<uint8_t>(normalized * 255.0f + 0.5f);
}

// Core-0 read-only capture, one lane per slice. Patch-follow sentinels resolve
// through the SAME transform as playback; no trigger, transport, or DSP reads.
inline void captureLane(LaneDisplay::Lane &lane, const Sequencer &sequencer,
                        ParamId id, const VoiceConfig *config, float bpm) noexcept {
    lane.count = sequencer.getParameterStepCount(id);
    lane.start = sequencer.getParameterLoopStart(id);
    lane.cursor = sequencer.getCurrentStepForParameter(id);
    for (uint8_t step = 0; step < lane.count; ++step)
        lane.values[step] = graphValue(id, sequencer.getPlaybackValue(id, step));
    std::fill(lane.values + lane.count, lane.values + SequencerConstants::MAX_STEPS_COUNT, 0);
    const auto *definition = parameterDefinition(id);
    std::snprintf(lane.label, sizeof(lane.label), "%s",
                  config ? VoiceEdit::laneName(id, *config) : definition->name);
    if (!config) {
        std::snprintf(lane.valueText, sizeof(lane.valueText), "--");
        return;
    }
    const size_t scaleIndex = std::min<size_t>(currentScale, SCALES_COUNT - 1);
    const TuningView tuned = currentTuningView(scaleIndex);
    MusicalValues::format(id, sequencer.getPlaybackStep(), *config, scale[scaleIndex],
                         bpm, lane.valueText, sizeof(lane.valueText), false,
                         &tuned.world, tuned.nativeScale);
}
}
