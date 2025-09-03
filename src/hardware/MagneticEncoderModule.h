#pragma once

#include "IHardwareModule.h"
#include <Wire.h>

class MagneticEncoderModule : public IHardwareModule {
private:
    bool connected;
    uint32_t lastHealthCheck;
    uint8_t failureCount;
    uint16_t lastPosition;
    static constexpr uint8_t AS5600_I2C_ADDRESS = 0x36;
    static constexpr uint8_t MAX_FAILURE_COUNT = 3;
    static constexpr uint32_t HEALTH_CHECK_INTERVAL_MS = 30000;
    static constexpr uint8_t AS5600_STATUS_REG = 0x0B;
    static constexpr uint8_t AS5600_ANGLE_HIGH_REG = 0x0E;
    static constexpr uint8_t AS5600_ANGLE_LOW_REG = 0x0F;

    // Helper methods for I2C communication
    uint8_t readRegister8(uint8_t reg);
    uint16_t readRegister16(uint8_t reg);
    bool writeRegister8(uint8_t reg, uint8_t value);

public:
    MagneticEncoderModule();
    virtual ~MagneticEncoderModule() = default;

    // Core detection and initialization
    bool detect() override;
    bool initialize() override;
    bool isConnected() const override;

    // Module identification
    const char* getModuleName() const override;
    uint8_t getModuleId() const override;
    uint32_t getCapabilities() const override;

    // Runtime status
    bool healthCheck() override;
    void reset() override;

    // Fallback support
    bool hasFallback() const override;
    bool activateFallback() override;
    bool deactivateFallback() override;

    // Module-specific methods
    uint16_t getRawAngle();
    float getAngleDegrees();
    int16_t getAngleDelta();  // Returns change since last read
    bool isMagnetDetected();
    uint8_t getMagnetStatus();
};