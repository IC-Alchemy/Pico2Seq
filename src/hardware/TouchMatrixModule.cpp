#include "TouchMatrixModule.h"
#include "HardwareModuleDefs.h"

TouchMatrixModule::TouchMatrixModule() 
    : connected(false), lastHealthCheck(0), failureCount(0) {
}

bool TouchMatrixModule::detect() {
    // Reset failure count for fresh detection attempt
    failureCount = 0;
    
    // Test I2C communication with MPR121
    Wire.beginTransmission(MPR121_I2C_ADDRESS);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
        // Device responded, verify it's actually an MPR121
        if (mpr121.begin(MPR121_I2C_ADDRESS)) {
            connected = true;
            return true;
        }
    }
    
    connected = false;
    return false;
}

bool TouchMatrixModule::initialize() {
    if (!connected) {
        return false;
    }
    
    // MPR121 initialization is handled in detect() via begin()
    // Additional configuration can be added here if needed
    
    // Set touch and release thresholds
    for (uint8_t i = 0; i < 12; i++) {
        mpr121.setThresholds(i, 12, 6);  // Touch threshold: 12, Release threshold: 6
    }
    
    lastHealthCheck = millis();
    return true;
}

bool TouchMatrixModule::isConnected() const {
    return connected;
}

const char* TouchMatrixModule::getModuleName() const {
    return "TouchMatrix";
}

uint8_t TouchMatrixModule::getModuleId() const {
    return static_cast<uint8_t>(ModuleType::TOUCH_MATRIX);
}

uint32_t TouchMatrixModule::getCapabilities() const {
    return ModuleCapabilities::REAL_TIME_INPUT | 
           ModuleCapabilities::STEP_SEQUENCING |
           ModuleCapabilities::FALLBACK_AVAILABLE;
}

bool TouchMatrixModule::healthCheck() {
    uint32_t currentTime = millis();
    
    // Only perform health check if enough time has passed
    if (currentTime - lastHealthCheck < HEALTH_CHECK_INTERVAL_MS) {
        return connected;
    }
    
    lastHealthCheck = currentTime;
    
    if (!connected) {
        return false;
    }
    
    // Test I2C communication
    Wire.beginTransmission(MPR121_I2C_ADDRESS);
    uint8_t error = Wire.endTransmission();
    
    if (error != 0) {
        failureCount++;
        if (failureCount >= MAX_FAILURE_COUNT) {
            connected = false;
        }
        return false;
    }
    
    // Reset failure count on successful communication
    failureCount = 0;
    return true;
}

void TouchMatrixModule::reset() {
    connected = false;
    failureCount = 0;
    lastHealthCheck = 0;
    
    // Attempt to reinitialize
    if (detect()) {
        initialize();
    }
}

bool TouchMatrixModule::hasFallback() const {
    return true;  // 4-button digital input fallback available
}

bool TouchMatrixModule::activateFallback() {
    // Fallback activation is handled by FallbackControlManager
    // This method just confirms fallback is available
    return hasFallback();
}

bool TouchMatrixModule::deactivateFallback() {
    // Fallback deactivation is handled by FallbackControlManager
    // This method just confirms we can return to hardware control
    return connected;
}

uint16_t TouchMatrixModule::getTouchStatus() {
    if (!connected) {
        return 0;
    }
    
    return mpr121.touched();
}

bool TouchMatrixModule::isTouchDetected(uint8_t pin) {
    if (!connected || pin >= 12) {
        return false;
    }
    
    uint16_t touchStatus = getTouchStatus();
    return (touchStatus & (1 << pin)) != 0;
}