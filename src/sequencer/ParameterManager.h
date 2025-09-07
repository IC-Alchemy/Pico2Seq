#ifndef PARAMETER_MANAGER_H
#define PARAMETER_MANAGER_H

#include "SequencerDefs.h" // For ParamId, ParameterTrack, CORE_PARAMETERS, AS5600ParameterMode
#include "pico/sync.h"     // For spin_lock_t

/**
 * @brief Manages all parameter tracks for a sequencer, providing thread-safe access.
 */
class ParameterManager
{
public:
    ParameterManager(); // Constructor to initialize the spin lock

    /**
     * @brief Initializes all parameter tracks with their default values.
     */
    void init();

    void setStepCount(ParamId id, uint8_t steps);
    uint8_t getStepCount(ParamId id) const;
    float getValue(ParamId id, uint8_t stepIdx) const;
    void setValue(ParamId id, uint8_t stepIdx, float value);
    void randomizeParameters();

    // AS5600 Parameter Bounds Management functions moved to src/sensors/AS5600Manager.h/.cpp

private:
    ParameterTrack<SequencerConstants::MAX_STEPS_COUNT> _tracks[static_cast<size_t>(ParamId::Count)];
    mutable spin_lock_t *_lock; // mutable because const methods (like getValue) need to acquire the lock
};

#endif // PARAMETER_MANAGER_H