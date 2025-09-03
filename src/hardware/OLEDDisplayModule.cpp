#include "OLEDDisplayModule.h"
#include "HardwareModuleDefs.h"

OLEDDisplayModule::OLEDDisplayModule() 
    : display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, -1), 
      connected(false), lastHealthCheck(0), failureCount(0) {
}

bool OLEDDisplayModule::detect() {
    // Reset failure count for fresh detection attempt
    failureCount = 0;
    
    // Test I2C communication with SH110X
    Wire.beginTransmission(SH110X_I2C_ADDRESS);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
        // Device responded, attempt to initialize display
        if (display.begin(SH110X_I2C_ADDRESS, true)) {  // true = reset display
            connected = true;
            return true;
        }
    }
    
    connected = false;
    return false;
}

bool OLEDDisplayModule::initialize() {
    if (!connected) {
        return false;
    }
    
    // Clear display and set default settings
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    
    // Display initialization message
    display.println("OLED Display");
    display.println("Initialized");
    display.display();
    
    lastHealthCheck = millis();
    return true;
}

bool OLEDDisplayModule::isConnected() const {
    return connected;
}

const char* OLEDDisplayModule::getModuleName() const {
    return "OLEDDisplay";
}

uint8_t OLEDDisplayModule::getModuleId() const {
    return static_cast<uint8_t>(ModuleType::OLED_DISPLAY);
}

uint32_t OLEDDisplayModule::getCapabilities() const {
    return ModuleCapabilities::VISUAL_FEEDBACK |
           ModuleCapabilities::FALLBACK_AVAILABLE;
}

bool OLEDDisplayModule::healthCheck() {
    uint32_t currentTime = millis();
    
    // Only perform health check if enough time has passed
    if (currentTime - lastHealthCheck < HEALTH_CHECK_INTERVAL_MS) {
        return connected;
    }
    
    lastHealthCheck = currentTime;
    
    if (!connected) {
        return false;
    }
    
    // Test communication by attempting to write to display
    Wire.beginTransmission(SH110X_I2C_ADDRESS);
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

void OLEDDisplayModule::reset() {
    connected = false;
    failureCount = 0;
    lastHealthCheck = 0;
    
    // Attempt to reinitialize
    if (detect()) {
        initialize();
    }
}

bool OLEDDisplayModule::hasFallback() const {
    return true;  // Serial console output fallback available
}

bool OLEDDisplayModule::activateFallback() {
    // Fallback activation is handled by FallbackControlManager
    // This method just confirms fallback is available
    return hasFallback();
}

bool OLEDDisplayModule::deactivateFallback() {
    // Fallback deactivation is handled by FallbackControlManager
    // This method just confirms we can return to hardware control
    return connected;
}

void OLEDDisplayModule::clearDisplay() {
    if (!connected) {
        return;
    }
    
    display.clearDisplay();
}

void OLEDDisplayModule::updateDisplay() {
    if (!connected) {
        return;
    }
    
    display.display();
}

void OLEDDisplayModule::setTextSize(uint8_t size) {
    if (!connected) {
        return;
    }
    
    display.setTextSize(size);
}

void OLEDDisplayModule::setTextColor(uint16_t color) {
    if (!connected) {
        return;
    }
    
    display.setTextColor(color);
}

void OLEDDisplayModule::setCursor(int16_t x, int16_t y) {
    if (!connected) {
        return;
    }
    
    display.setCursor(x, y);
}

void OLEDDisplayModule::print(const char* text) {
    if (!connected) {
        return;
    }
    
    display.print(text);
}

void OLEDDisplayModule::println(const char* text) {
    if (!connected) {
        return;
    }
    
    display.println(text);
}