#include "MagneticEncoderModule.h"
#include "HardwareModuleDefs.h"

MagneticEncoderModule::MagneticEncoderModule() 
    : connected(false), lastHealthCheck(0), failureCount(0), lastPosition(0) {
}

bool MagneticEncoderModule::detect() {
    // Reset failure count for fresh detection attempt
    failureCount = 0;
    
    // Test I2C communication with AS5600
    Wire.beginTransmission(AS5600_I2C_ADDRESS);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
        // Device responded, verify it's actually an AS5600 by reading status register
        uint8_t status = readRegister8(AS5600_STATUS_REG);
        
        // Check if we got a valid response (status register should have valid bits)
        if (status != 0xFF && status != 0x00) {
            connected = true;
            return true;
        }
    }
    
    connected = false;
    return false;
}

bool MagneticEncoderModule::initialize() {
    if (!connected) {
        return false;
    }
    
    // Read initial position
    lastPosition = getRawAngle();
    
    lastHealthCheck = millis();
    return true;
}

bool MagneticEncoderModule::isConnected() const {
    return connected;
}

const char* MagneticEncoderModule::getModuleName() const {
    return "MagneticEncoder";
}

uint8_t MagneticEncoderModule::getModuleId() const {
    return static_cast<uint8_t>(ModuleType::MAGNETIC_ENCODER);
}

uint32_t MagneticEncoderModule::getCapabilities() const {
    return ModuleCapabilities::REAL_TIME_INPUT | 
           ModuleCapabilities::PARAMETER_CONTROL |
           ModuleCapabilities::FALLBACK_AVAILABLE;
}

bool MagneticEncoderModule::healthCheck() {
    uint32_t currentTime = millis();
    
    // Only perform health check if enough time has passed
    if (currentTime - lastHealthCheck < HEALTH_CHECK_INTERVAL_MS) {
        return connected;
    }
    
    lastHealthCheck = currentTime;
    
    if (!connected) {
        return false;
    }
    
    // Test communication by reading status register
    uint8_t status = readRegister8(AS5600_STATUS_REG);
    
    if (status == 0xFF || status == 0x00) {
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

void MagneticEncoderModule::reset() {
    connected = false;
    failureCount = 0;
    lastHealthCheck = 0;
    lastPosition = 0;
    
    // Attempt to reinitialize
    if (detect()) {
        initialize();
    }
}

bool MagneticEncoderModule::hasFallback() const {
    return true;  // Increment/decrement button fallback available
}

bool MagneticEncoderModule::activateFallback() {
    // Fallback activation is handled by FallbackControlManager
    // This method just confirms fallback is available
    return hasFallback();
}

bool MagneticEncoderModule::deactivateFallback() {
    // Fallback deactivation is handled by FallbackControlManager
    // This method just confirms we can return to hardware control
    return connected;
}

uint16_t MagneticEncoderModule::getRawAngle() {
    if (!connected) {
        return 0;
    }
    
    return readRegister16(AS5600_ANGLE_HIGH_REG);
}

float MagneticEncoderModule::getAngleDegrees() {
    uint16_t rawAngle = getRawAngle();
    return (rawAngle * 360.0f) / 4096.0f;  // AS5600 has 12-bit resolution
}

int16_t MagneticEncoderModule::getAngleDelta() {
    if (!connected) {
        return 0;
    }
    
    uint16_t currentPosition = getRawAngle();
    int16_t delta = currentPosition - lastPosition;
    
    // Handle wraparound (12-bit encoder: 0-4095)
    if (delta > 2048) {
        delta -= 4096;
    } else if (delta < -2048) {
        delta += 4096;
    }
    
    lastPosition = currentPosition;
    return delta;
}

bool MagneticEncoderModule::isMagnetDetected() {
    if (!connected) {
        return false;
    }
    
    uint8_t status = getMagnetStatus();
    return (status & 0x20) != 0;  // MD bit (bit 5) indicates magnet detected
}

uint8_t MagneticEncoderModule::getMagnetStatus() {
    if (!connected) {
        return 0;
    }
    
    return readRegister8(AS5600_STATUS_REG);
}

uint8_t MagneticEncoderModule::readRegister8(uint8_t reg) {
    Wire.beginTransmission(AS5600_I2C_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) {
        return 0xFF;  // Communication error
    }
    
    Wire.requestFrom(AS5600_I2C_ADDRESS, (uint8_t)1);
    if (Wire.available()) {
        return Wire.read();
    }
    
    return 0xFF;  // No data available
}

uint16_t MagneticEncoderModule::readRegister16(uint8_t reg) {
    Wire.beginTransmission(AS5600_I2C_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) {
        return 0xFFFF;  // Communication error
    }
    
    Wire.requestFrom(AS5600_I2C_ADDRESS, (uint8_t)2);
    if (Wire.available() >= 2) {
        uint8_t high = Wire.read();
        uint8_t low = Wire.read();
        return (high << 8) | low;
    }
    
    return 0xFFFF;  // No data available
}

bool MagneticEncoderModule::writeRegister8(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(AS5600_I2C_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}