#pragma once

#include "../ui/UIState.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../voice/VoiceManager.h"
#include "../voice/VoiceSystem.h"
#include <atomic>
#include <memory>
#include <algorithm>
#include "../sensors/SensorConstants.h"

// Existing UI/sensor APIs refer to these objects by name. Keep their types and
// program-long lifetimes; their mutable control state belongs to Core 0.
extern UIState uiState;
extern Sequencer seq1, seq2, seq3, seq4;
extern std::unique_ptr<VoiceManager> voiceManager;
extern VoiceSystem voiceSystem;
extern uint8_t currentScale;
extern bool isClockRunning;

// Core 0 publishes the fixed voice collection after all control setup completes.
// Recovery mode leaves this false, keeping Core 1 out of hardware startup.
extern std::atomic<bool> voicesReady;

namespace AppState
{
// Non-owning, immutable routing table in musician-facing voice order (1-4).
extern Sequencer *const sequencers[VoiceSystem::MAX_VOICES];
struct PerformanceInput
{
    int distanceAboveMinimumMm = 0;

    void observeDistance(int rawDistanceMm) noexcept
    {
        constexpr int minimum = SensorConstants::DistanceSensor::MIN_DISTANCE_HEIGHT_MM;
        constexpr int maximum = SensorConstants::DistanceSensor::MAX_DISTANCE_HEIGHT_MM;
        distanceAboveMinimumMm = (rawDistanceMm >= minimum && rawDistanceMm <= maximum)
                                    ? rawDistanceMm - minimum : 0;
    }

    float recordingValue() const noexcept
    {
        constexpr int maximum = SensorConstants::DistanceSensor::MAX_DISTANCE_HEIGHT_MM;
        float normalized = 0.0f;
        constexpr int minimum = SensorConstants::DistanceSensor::MIN_DISTANCE_HEIGHT_MM;
        if (maximum > minimum)
            normalized = static_cast<float>(distanceAboveMinimumMm) / static_cast<float>(maximum-minimum);
        return std::max(0.0f, std::min(normalized, 1.0f));
    }
};
extern PerformanceInput performanceInput;
}
