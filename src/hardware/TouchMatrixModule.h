#pragma once

#include "IHardwareModule.h"
#include <Adafruit_MPR121.h>
#include <Wire.h>

class TouchMatrixModule : public IHardwareModule {
private:
    Adafruit_MPR121 mpr121;
    bool connected;
    uint32_t lastHealthCheck;
    uint8_t failureCount;
    static constexpr uint8_t MPR121_I2C_ADDRESS = 0x5A;
    static constexpr uint8_t MAX_FAILURE_COUNT = 3;
    static constexpr uint32_t HEALTH_CHECK_INTERVAL_MS = 30000;

public:
    TouchMatrixModule();
    virtual ~TouchMatrixModule() = default;

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
    Adafruit_MPR121& getMPR121() { return mpr121; }
    uint16_t getTouchStatus();
    bool isTouchDetected(uint8_t pin);
};