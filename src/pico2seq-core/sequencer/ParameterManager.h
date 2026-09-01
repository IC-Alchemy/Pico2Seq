#ifndef PARAMETER_MANAGER_H
#define PARAMETER_MANAGER_H

#include "SequencerDefs.h" // For ParamId, ParameterTrack, CORE_PARAMETERS, EncoderParameterMode
#include <cstddef>         // For size_t

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
    void randomizeParameters();

    // Encoder Parameter Bounds Management functions moved to src/sensors/EncoderManager.h/.cpp

private:
    ParameterTrack<SequencerConstants::MAX_STEPS_COUNT> _tracks[static_cast<size_t>(ParamId::Count)];
};

#endif // PARAMETER_MANAGER_H