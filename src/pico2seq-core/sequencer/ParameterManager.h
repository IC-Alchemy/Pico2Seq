#ifndef PARAMETER_MANAGER_H
#define PARAMETER_MANAGER_H

#include "SequencerDefs.h" // For ParamId, ParameterTrack, CORE_PARAMETERS, EncoderParameterMode
#include <array>
#include <cstddef>         // For size_t
#include <cstdint>

/**
 * @brief Manages all parameter tracks for a sequencer.
 */
class ParameterManager
{
public:
    /**
     * @brief Initializes all parameter tracks with their default values.
     */
    void init();

    void setStepCount(ParamId id, uint8_t steps);
    uint8_t getStepCount(ParamId id) const;
    float getValue(ParamId id, uint8_t stepIdx) const;
    void setValue(ParamId id, uint8_t stepIdx, float value);
    void copyStep(uint8_t srcStep, uint8_t dstStep);

    // Direct (non-wrapping) access for persistence. getRawValue reads storage
    // beyond the active length; setRawValue writes without the UI clamp/round.
    // Writes at/beyond the active length wrap like setValue — the persistence
    // codec grows the track to MAX first, so restore never wraps.
    float getRawValue(ParamId id, uint8_t stepIdx) const;
    void setRawValue(ParamId id, uint8_t stepIdx, float value);

    static constexpr uint8_t kDefaultRandomizeDepth = 35;

    // Velocity/Filter/Attack/Decay steps get triangular offsets around the
    // neutral modifier 0.5, spanning 0.5 +/- depth/2 scaled by each lane's
    // amount. Played through patch bases, depth is how far (in percent) a
    // step may stray from the base toward either end of its lane, most draws
    // staying close. Note draws scale steps 0-12, Octave and GateLength return
    // to neutral, and Gate/Slide are never touched. A lane amount of 0 leaves
    // that lane as it is. seed 0 seeds from the clock; any other seed repeats.
    void randomizeParameters(uint8_t depthPercent = kDefaultRandomizeDepth, uint64_t seed = 0);
    void setLaneAmount(ParamId id, uint8_t percent); // clamps to 0-100
    uint8_t getLaneAmount(ParamId id) const;

    // Encoder Parameter Bounds Management functions moved to src/sensors/EncoderManager.h/.cpp

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