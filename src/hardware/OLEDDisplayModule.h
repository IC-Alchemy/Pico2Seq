#pragma once

#include "IHardwareModule.h"
#include <Adafruit_SH110X.h>
#include <Wire.h>

class OLEDDisplayModule : public IHardwareModule {
private:
    Adafruit_SH1106G display;
    bool connected;
    uint32_t lastHealthCheck;
    uint8_t failureCount;
    static constexpr uint8_t SH110X_I2C_ADDRESS = 0x3C;
    static constexpr uint8_t MAX_FAILURE_COUNT = 3;
    static constexpr uint32_t HEALTH_CHECK_INTERVAL_MS = 30000;
    static constexpr uint8_t DISPLAY_WIDTH = 128;
    static constexpr uint8_t DISPLAY_HEIGHT = 64;

public:
    OLEDDisplayModule();
    virtual ~OLEDDisplayModule() = default;

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
    Adafruit_SH1106G& getDisplay() { return display; }
    void clearDisplay();
    void updateDisplay();
    void setTextSize(uint8_t size);
    void setTextColor(uint16_t color);
    void setCursor(int16_t x, int16_t y);
    void print(const char* text);
    void println(const char* text);
};