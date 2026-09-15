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
    void copyStep(uint8_t srcStep, uint8_t dstStep);

    // Direct (non-wrapping) access for persistence. getRawValue reads storage
    // beyond the active length; setRawValue writes without the UI clamp/round.
    // Writes at/beyond the active length wrap like setValue — the persistence
    // codec grows the track to MAX first, so restore never wraps.
    float getRawValue(ParamId id, uint8_t stepIdx) const;
    void setRawValue(ParamId id, uint8_t stepIdx, float value);

    void randomizeParameters(bool patchModifiers = false);

    // Encoder Parameter Bounds Management functions moved to src/sensors/EncoderManager.h/.cpp

private:
    ParameterTrack<SequencerConstants::MAX_STEPS_COUNT> _tracks[static_cast<size_t>(ParamId::Count)];
};

#endif // PARAMETER_MANAGER_H