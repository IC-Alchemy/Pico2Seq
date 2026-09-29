#ifndef PARAMETER_MANAGER_H
#define PARAMETER_MANAGER_H

#include "SequencerDefs.h" // For ParamId, ParameterTrack, CORE_PARAMETERS, EncoderParameterMode
#include <array>
#include <cstddef>         // For size_t
#include <cstdint>

// ParameterManager: owns one fixed-size ParameterTrack per ParamId.
// Each lane has its own step count, so e.g. Note can run 16 steps while
// Filter loops 8 — that independence is the polymetric sequencer.
class ParameterManager
{
public:
    // Reset every lane to its CORE_PARAMETERS default value and length.
    void init();

    void setStepCount(ParamId id, uint8_t steps);
    uint8_t getStepCount(ParamId id) const;
    float getValue(ParamId id, uint8_t stepIdx) const;
    void setValue(ParamId id, uint8_t stepIdx, float value);
    void copyStep(uint8_t srcStep, uint8_t dstStep);
    // Fill all 64 slots (present and future steps) without changing lane length.
    void fillTrack(ParamId id, float value);

    // Raw access for save/load: reads past the active length, writes without
    // UI clamp/round (values were normalized when recorded). Writes at/beyond
    // the active length wrap — the codec grows to MAX first so restore never wraps.
    float getRawValue(ParamId id, uint8_t stepIdx) const;
    void setRawValue(ParamId id, uint8_t stepIdx, float value);

    static constexpr uint8_t kDefaultRandomizeDepth = 35;

    // Humanize: rewrite each lane around its center without touching the rhythm.
    // Triangular spread (sum of two uniform draws) keeps most steps near the base
    // with a few reaching the depth edge. Note draws scale steps 0-12, Octave and
    // GateLength return to neutral, Gate/Slide are never touched (groove is sacred).
    // Lane amount 0 skips that lane; seed 0 seeds from the clock, else repeats.
    void randomizeParameters(uint8_t depthPercent = kDefaultRandomizeDepth, uint64_t seed = 0);
    void setLaneAmount(ParamId id, uint8_t percent); // Per-lane humanize depth, clamped 0-100
    uint8_t getLaneAmount(ParamId id) const;

    // Encoder bounds helpers live in src/sensors/EncoderManager, not here.

private:
    using LaneAmounts = std::array<uint8_t, static_cast<size_t>(ParamId::Count)>;
    static constexpr LaneAmounts fullLaneAmounts()
    {
        LaneAmounts amounts{};
        for (auto &amount : amounts)
            amount = 100;
        return amounts;
    }

    ParameterTrack<SequencerConstants::MAX_STEPS_COUNT> _tracks[static_cast<size_t>(ParamId::Count)];
    LaneAmounts _laneAmounts = fullLaneAmounts();
};

#endif // PARAMETER_MANAGER_H