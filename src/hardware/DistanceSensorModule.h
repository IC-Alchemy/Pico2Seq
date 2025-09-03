#pragma once

#include "IHardwareModule.h"
#include <Melopero_VL53L1X.h>
#include <Wire.h>

class DistanceSensorModule : public IHardwareModule {
private:
    Melopero_VL53L1X sensor;
    bool connected;
    uint32_t lastHealthCheck;
    uint8_t failureCount;
    static constexpr uint8_t VL53L1X_I2C_ADDRESS = 0x29;
    static constexpr uint8_t MAX_FAILURE_COUNT = 3;
    static constexpr uint32_t HEALTH_CHECK_INTERVAL_MS = 30000;
    static constexpr uint32_t COMMUNICATION_TIMEOUT_MS = 100;

public:
    DistanceSensorModule();
    virtual ~DistanceSensorModule() = default;

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
    Melopero_VL53L1X& getSensor() { return sensor; }
    uint16_t getDistance();
    bool startRanging();
    bool stopRanging();
    bool isDataReady();
};