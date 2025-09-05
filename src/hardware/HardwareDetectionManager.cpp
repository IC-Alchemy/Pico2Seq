#include "HardwareDetectionManager.h"
#include <Arduino.h>
#include <Wire.h>

// Known I2C addresses for devices used by Pico2Seq
static constexpr uint8_t ADDR_OLED = 0x3C;
static constexpr uint8_t ADDR_AS5600 = 0x36;
static constexpr uint8_t ADDR_VL53L1X = 0x29;

bool HardwareDetectionManager::initializeSystem() {
    // Prepare a fresh registry
    registry.reset();

    // Initialize I2C on the correct pins for this board (SDA=GP12, SCL=GP13)
    // and set a reasonable fast clock for OLED/sensors
   // Wire.setSDA(12);
   // Wire.setSCL(13);
    Wire.begin();
    Wire.setClock(400000); // 400kHz fast mode

    systemInitialized = true;
    Serial.println("Hardware Detection (simplified) initialized (I2C ready)");
    return true;
}

static bool probeI2CAddress(uint8_t address) {
    Wire.beginTransmission(address);
    uint8_t err = Wire.endTransmission();
    return (err == 0);
}

void HardwareDetectionManager::performStartupDetection() {
    if (!systemInitialized) {
        Serial.println("ERROR: HardwareDetectionManager not initialized");
        return;
    }

    Serial.println("Running simplified startup detection...");

    // Probe OLED
    bool oledPresent = probeI2CAddress(ADDR_OLED);
    registry.setModulePresent(ModuleType::OLED_DISPLAY, oledPresent);
    if (oledPresent) {
        registry.setModuleInitialized(ModuleType::OLED_DISPLAY, true);
        registry.setStatusMessage(ModuleType::OLED_DISPLAY, "Detected");
        registry.updateHealthCheck(ModuleType::OLED_DISPLAY, millis());
        Serial.println("OLED: Detected");
    } else {
        registry.setStatusMessage(ModuleType::OLED_DISPLAY, "Not Found");
        Serial.println("OLED: Not Found");
    }

    // Probe touch matrix (MPR121 at 0x5A)
    bool touchPresent = probeI2CAddress(0x5A);
    registry.setModulePresent(ModuleType::TOUCH_MATRIX, touchPresent);
    if (touchPresent) {
        registry.setModuleInitialized(ModuleType::TOUCH_MATRIX, true);
        registry.setStatusMessage(ModuleType::TOUCH_MATRIX, "Detected");
        registry.updateHealthCheck(ModuleType::TOUCH_MATRIX, millis());
        Serial.println("Touch Matrix (MPR121): Detected");
    } else {
        registry.setStatusMessage(ModuleType::TOUCH_MATRIX, "Not Found");
        Serial.println("Touch Matrix (MPR121): Not Found");
    }

    // Probe magnetic encoder (AS5600)
    bool encPresent = probeI2CAddress(ADDR_AS5600);
    registry.setModulePresent(ModuleType::MAGNETIC_ENCODER, encPresent);
    if (encPresent) {
        registry.setModuleInitialized(ModuleType::MAGNETIC_ENCODER, true);
        registry.setStatusMessage(ModuleType::MAGNETIC_ENCODER, "Detected");
        registry.updateHealthCheck(ModuleType::MAGNETIC_ENCODER, millis());
        Serial.println("Magnetic Encoder: Detected");
    } else {
        registry.setStatusMessage(ModuleType::MAGNETIC_ENCODER, "Not Found");
        Serial.println("Magnetic Encoder: Not Found");
    }

    // Probe distance sensor (VL53L1X)
    bool distPresent = probeI2CAddress(ADDR_VL53L1X);
    registry.setModulePresent(ModuleType::DISTANCE_SENSOR, distPresent);
    if (distPresent) {
        registry.setModuleInitialized(ModuleType::DISTANCE_SENSOR, true);
        registry.setStatusMessage(ModuleType::DISTANCE_SENSOR, "Detected");
        registry.updateHealthCheck(ModuleType::DISTANCE_SENSOR, millis());
        Serial.println("Distance Sensor: Detected");
    } else {
        registry.setStatusMessage(ModuleType::DISTANCE_SENSOR, "Not Found");
        Serial.println("Distance Sensor: Not Found");
    }

    // Update internal hardware status snapshot
    updateHardwareStatusFromRegistry();

    Serial.println("Simplified hardware detection complete");
}

void HardwareDetectionManager::generateStatusReport(String& report) {
    report = "=== Hardware Status Report ===\n";

    // OLED
    {
        ModuleStatus s = registry.getModuleStatus(ModuleType::OLED_DISPLAY);
        report += "OLED: ";
        report += s.isPresent ? (s.isInitialized ? "OK" : "Present") : "NOT FOUND";
        report += " - ";
        report += s.statusMessage;
        report += "\n";
    }

    // Magnetic Encoder
    {
        ModuleStatus s = registry.getModuleStatus(ModuleType::MAGNETIC_ENCODER);
        report += "MagneticEncoder: ";
        report += s.isPresent ? (s.isInitialized ? "OK" : "Present") : "NOT FOUND";
        report += " - ";
        report += s.statusMessage;
        report += "\n";
    }

    // Distance Sensor
    {
        ModuleStatus s = registry.getModuleStatus(ModuleType::DISTANCE_SENSOR);
        report += "DistanceSensor: ";
        report += s.isPresent ? (s.isInitialized ? "OK" : "Present") : "NOT FOUND";
        report += " - ";
        report += s.statusMessage;
        report += "\n";
    }

    report += "\nSummary:\n";
    report += "Available: ";
    report += String(registry.getHealthyModuleCount());
    report += "\nFailed: ";
    report += String(getFailedModules().size());
}

void HardwareDetectionManager::updateHardwareStatusFromRegistry() {
    // Intentionally minimal: mirror registry state into a simple snapshot if needed
    (void)registry; // keep compiler happy if snapshot not used elsewhere
}