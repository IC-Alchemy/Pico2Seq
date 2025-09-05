#pragma once

#include <Arduino.h>

/**
 * Hardware Status Structure
 * Contains hardware module availability and fallback status
 */
struct HardwareStatus {
    // Module availability status
    bool touchMatrixAvailable = false;
    bool distanceSensorAvailable = false;
    bool magneticEncoderAvailable = false;
    bool oledDisplayAvailable = false;

    // Fallback activation status
    bool touchMatrixFallbackActive = false;
    bool distanceSensorFallbackActive = false;
    bool magneticEncoderFallbackActive = false;
    bool oledDisplayFallbackActive = false;

    // Status display information
    String statusMessage = "";
    uint32_t lastUpdateTime = 0;

    /**
     * Generate a summary status message for display
     * @return formatted status string
     */
    String generateStatusSummary() const;

    /**
     * Check if any modules are using fallback controls
     * @return true if any fallback is active
     */
    bool hasAnyFallbacks() const;

    /**
     * Get count of available (non-fallback) modules
     * @return number of modules working normally
     */
    uint8_t getAvailableModuleCount() const;

    /**
     * Update hardware status from HardwareRegistry
     * @param registry Reference to hardware registry
     */
    void updateFromRegistry(const class HardwareRegistry& registry);
};