#include "DistanceSensorModule.h"
#include "HardwareModuleDefs.h"

DistanceSensorModule::DistanceSensorModule() 
    : connected(false), lastHealthCheck(0), failureCount(0) {
}

bool DistanceSensorModule::detect() {
    // Reset failure count for fresh detection attempt
    failureCount = 0;
    
    // Test I2C communication with VL53L1X
    Wire.beginTransmission(VL53L1X_I2C_ADDRESS);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
        // Device responded, verify it's actually a VL53L1X by checking sensor ID
        sensor.initI2C();
        
        uint32_t startTime = millis();
        while (millis() - startTime < COMMUNICATION_TIMEOUT_MS) {
            if (sensor.getSensorId() == 0xEACC) {  // VL53L1X sensor ID
                connected = true;
                return true;
            }
            delay(10);
        }
    }
    
    connected = false;
    return false;
}

bool DistanceSensorModule::initialize() {
    if (!connected) {
        return false;
    }
    
    // Initialize the sensor with default configuration
    sensor.setDistanceMode(Melopero_VL53L1X::Short);
    sensor.setTimingBudgetInMs(20);  // 20ms timing budget for fast updates
    sensor.setInterMeasurementInMs(25);  // 25ms between measurements
    
    // Start continuous ranging
    if (!startRanging()) {
        connected = false;
        return false;
    }
    
    lastHealthCheck = millis();
    return true;
}

bool DistanceSensorModule::isConnected() const {
    return connected;
}

const char* DistanceSensorModule::getModuleName() const {
    return "DistanceSensor";
}

uint8_t DistanceSensorModule::getModuleId() const {
    return static_cast<uint8_t>(ModuleType::DISTANCE_SENSOR);
}

uint32_t DistanceSensorModule::getCapabilities() const {
    return ModuleCapabilities::REAL_TIME_INPUT | 
           ModuleCapabilities::PARAMETER_CONTROL |
           ModuleCapabilities::FALLBACK_AVAILABLE;
}

bool DistanceSensorModule::healthCheck() {
    uint32_t currentTime = millis();
    
    // Only perform health check if enough time has passed
    if (currentTime - lastHealthCheck < HEALTH_CHECK_INTERVAL_MS) {
        return connected;
    }
    
    lastHealthCheck = currentTime;
    
    if (!connected) {
        return false;
    }
    
    // Test communication by checking if data is ready
    uint32_t startTime = millis();
    bool communicationOk = false;
    
    while (millis() - startTime < COMMUNICATION_TIMEOUT_MS) {
        if (isDataReady()) {
            communicationOk = true;
            break;
        }
        delay(5);
    }
    
    if (!communicationOk) {
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

void DistanceSensorModule::reset() {
    connected = false;
    failureCount = 0;
    lastHealthCheck = 0;
    
    // Stop ranging before reset
    stopRanging();
    
    // Attempt to reinitialize
    if (detect()) {
        initialize();
    }
}

bool DistanceSensorModule::hasFallback() const {
    return true;  // Step button + AS5600 encoder fallback available
}

bool DistanceSensorModule::activateFallback() {
    // Fallback activation is handled by FallbackControlManager
    // This method just confirms fallback is available
    return hasFallback();
}

bool DistanceSensorModule::deactivateFallback() {
    // Fallback deactivation is handled by FallbackControlManager
    // This method just confirms we can return to hardware control
    return connected;
}

uint16_t DistanceSensorModule::getDistance() {
    if (!connected) {
        return 0;
    }
    
    if (isDataReady()) {
        return sensor.getDistance();
    }
    
    return 0;  // Return 0 if no data ready
}

bool DistanceSensorModule::startRanging() {
    if (!connected) {
        return false;
    }
    
    sensor.startRanging();
    return true;
}

bool DistanceSensorModule::stopRanging() {
    if (!connected) {
        return false;
    }
    
    sensor.stopRanging();
    return true;
}

bool DistanceSensorModule::isDataReady() {
    if (!connected) {
        return false;
    }
    
    return sensor.checkForDataReady();
}