#pragma once

#include "HardwareModuleDefs.h"
#include "HardwareRegistry.h"
#include <Arduino.h>
#include <Wire.h>
#include <vector>

/**
 * Simplified HardwareDetectionManager
 *
 * Purpose:
 * - During setup1() this manager will perform a minimal I2C presence check
 *   for the known devices and update the HardwareRegistry flags. No complex
 *   health checks, retries, fallbacks, or background recovery logic are
 *   performed. This keeps startup concise and predictable on Arduino.
 *
 * Behavior:
 * - initializeSystem() prepares the registry.
 * - performStartupDetection() probes a small list of well-known I2C addresses
 *   (OLED, AS5600 encoder, VL53L1X distance) and marks modules present/ready.
 * - All other methods remain but are lightweight no-ops so existing callers
 *   (Pico2Seq.ino) don't need to change.
 */
class HardwareDetectionManager {
public:
    HardwareDetectionManager() = default;
    ~HardwareDetectionManager() = default;

    // Non-copyable
    HardwareDetectionManager(const HardwareDetectionManager&) = delete;
    HardwareDetectionManager& operator=(const HardwareDetectionManager&) = delete;

    // Initialize the (simple) detection system
    bool initializeSystem();

    // Run once at startup to detect which modules are present
    void performStartupDetection();

    // Lightweight no-op implementations kept for compatibility
    void performHealthChecks() {}
    void handleModuleFailure(ModuleType /*moduleType*/) {}
    bool attemptModuleRecovery(ModuleType /*moduleType*/) { return false; }
    void processScheduledRecovery() {}

    // Status queries (delegated to registry)
    bool isModuleAvailable(ModuleType moduleType) const { return registry.isModuleHealthy(moduleType); }
    std::vector<ModuleType> getAvailableModules() const { return registry.getAvailableModules(); }
    std::vector<ModuleType> getFailedModules() const { return registry.getFailedModules(); }
    const HardwareRegistry& getRegistry() const { return registry; }

    // UI integration
    void updateUIStatus(class UIState& /*uiState*/) { updateHardwareStatusFromRegistry(); }
    void generateStatusReport(String& report);

    bool isSystemInitialized() const { return systemInitialized; }

private:
    HardwareRegistry registry;
    bool systemInitialized = false;

    // Minimal helper used during startup detection
    void updateHardwareStatusFromRegistry();
};