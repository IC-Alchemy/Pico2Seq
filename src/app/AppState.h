#pragma once

// AppState: the live musical state shared across Core 0 modules.
// Musical role: everything the performance is right now — UI focus, four patterns,
// voices, and hand height. Technical role: single home for program-long objects so
// control, playback, and session code share without extra globals.

#include "../ui/UIState.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../voice/VoiceManager.h"
#include "../voice/VoiceSystem.h"
#include "SequencerView.h"
#include <atomic>
#include <memory>
#include <algorithm>
#include "../sensors/SensorConstants.h"

// Legacy externs kept by name for existing UI/sensor call sites. Mutable control
// state belongs to Core 0; Core 1 only reads the published voice collection.
extern UIState uiState;
extern std::unique_ptr<VoiceManager> voiceManager;
extern VoiceSystem voiceSystem;
extern uint8_t currentScale;
extern bool isClockRunning;

// Set once by Core 0 after control setup; Core 1 spins on it at boot, then
// reads voices lock-free. Release/acquire pairing — never clear it afterwards.
// Recovery boot leaves it false so Core 1 never restarts broken audio alone.
extern std::atomic<bool> voicesReady;

namespace AppState
{
// Fixed voice order as the performer sees it (1-4). Non-owning, never null;
// the table and sequencers must outlive every user.
extern Sequencer *const sequencers[VoiceSystem::MAX_VOICES];
extern const SequencerView sequencerView;
struct PerformanceInput
{
    int distanceAboveMinimumMm = 0;
    // False for invalid readings and for anything beyond the window's edge
    // tolerance. Recording must skip absent hands: the value below would
    // otherwise write the window minimum (a -50% modifier) into steps.
    bool handPresent = false;

    void observeDistance(int rawDistanceMm) noexcept
    {
        constexpr int minimum = SensorConstants::DistanceSensor::MIN_DISTANCE_HEIGHT_MM;
        constexpr int maximum = SensorConstants::DistanceSensor::MAX_DISTANCE_HEIGHT_MM;
        constexpr int tolerance = SensorConstants::DistanceSensor::EDGE_TOLERANCE_MM;
        handPresent = rawDistanceMm != SensorConstants::DistanceSensor::INVALID_DISTANCE_MM &&
                      rawDistanceMm >= minimum - tolerance && rawDistanceMm <= maximum + tolerance;
        distanceAboveMinimumMm = handPresent ? std::clamp(rawDistanceMm, minimum, maximum) - minimum : 0;
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
