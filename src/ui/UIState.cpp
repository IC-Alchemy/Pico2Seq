#include "UIState.h"
#include "../hardware/HardwareRegistry.h"

String HardwareStatus::generateStatusSummary() const {
    String summary = "HW: ";
    uint8_t availableCount = getAvailableModuleCount();
    uint8_t totalModules = 4; // Touch, Distance, Encoder, OLED
    
    summary += String(availableCount) + "/" + String(totalModules) + " OK";
    
    if (hasAnyFallbacks()) {
        summary += " (FB)";
    }
    
    return summary;
}

bool HardwareStatus::hasAnyFallbacks() const {
    return touchMatrixFallbackActive || 
           distanceSensorFallbackActive || 
           magneticEncoderFallbackActive || 
           oledDisplayFallbackActive;
}

uint8_t HardwareStatus::getAvailableModuleCount() const {
    uint8_t count = 0;
    
    if (touchMatrixAvailable && !touchMatrixFallbackActive) count++;
    if (distanceSensorAvailable && !distanceSensorFallbackActive) count++;
    if (magneticEncoderAvailable && !magneticEncoderFallbackActive) count++;
    if (oledDisplayAvailable && !oledDisplayFallbackActive) count++;
    
    return count;
}
void
 HardwareStatus::updateFromRegistry(const HardwareRegistry& registry) {
    // Update module availability status
    touchMatrixAvailable = registry.isModulePresent(ModuleType::TOUCH_MATRIX);
    distanceSensorAvailable = registry.isModulePresent(ModuleType::DISTANCE_SENSOR);
    magneticEncoderAvailable = registry.isModulePresent(ModuleType::MAGNETIC_ENCODER);
    oledDisplayAvailable = registry.isModulePresent(ModuleType::OLED_DISPLAY);
    
    // Update fallback activation status
    touchMatrixFallbackActive = registry.isFallbackActive(ModuleType::TOUCH_MATRIX);
    distanceSensorFallbackActive = registry.isFallbackActive(ModuleType::DISTANCE_SENSOR);
    magneticEncoderFallbackActive = registry.isFallbackActive(ModuleType::MAGNETIC_ENCODER);
    oledDisplayFallbackActive = registry.isFallbackActive(ModuleType::OLED_DISPLAY);
    
    // Update status message and timestamp
    statusMessage = generateStatusSummary();
    lastUpdateTime = millis();
}
void
 UIState::updateHardwareStatus(const HardwareRegistry& registry) {
    hardwareStatus.updateFromRegistry(registry);
}