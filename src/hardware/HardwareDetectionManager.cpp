#include "HardwareDetectionManager.h"
#include "../ui/UIState.h"
#include <Arduino.h>
#include <algorithm>

// Initialization and detection methods

bool HardwareDetectionManager::initializeSystem() {
    // Initialize hardware status structure
    currentHardwareStatus = HardwareStatus{};
    currentHardwareStatus.lastUpdateTime = millis();
    
    // Reset timing variables
    lastHealthCheckTime = 0;
    nextRecoveryAttemptTime = 0;
    modulesAwaitingRecovery.clear();
    
    systemInitialized = true;
    
    Serial.println("Hardware Detection Manager initialized");
    return true;
}

void HardwareDetectionManager::performStartupDetection() {
    if (!systemInitialized) {
        Serial.println("ERROR: System not initialized before detection");
        return;
    }
    
    Serial.println("Starting hardware detection sequence...");
    
    // Detect each registered module
    for (auto& module : modules) {
        if (!module) continue;
        
        ModuleType moduleType = module->getModuleType();
        const char* moduleName = module->getModuleName();
        
        Serial.print("Detecting ");
        Serial.print(moduleName);
        Serial.print("... ");
        
        // Attempt detection
        bool detected = module->detect();
        logDetectionResult(moduleType, detected);
        
        if (detected) {
            // Update registry with successful detection
            registry.setModulePresent(moduleType, true);
            registry.setStatusMessage(moduleType, "Detected");
            
            // Attempt initialization
            Serial.print("Initializing ");
            Serial.print(moduleName);
            Serial.print("... ");
            
            bool initialized = module->initialize();
            if (initialized) {
                registry.setModuleInitialized(moduleType, true);
                registry.setStatusMessage(moduleType, "Ready");
                registry.updateHealthCheck(moduleType, millis());
                Serial.println("OK");
            } else {
                registry.setStatusMessage(moduleType, "Init Failed");
                Serial.println("FAILED");
                
                // Try fallback if initialization failed
                if (module->hasFallback()) {
                    activateModuleFallback(moduleType);
                }
            }
        } else {
            // Module not detected - activate fallback if available
            registry.setModulePresent(moduleType, false);
            registry.setStatusMessage(moduleType, "Not Found");
            
            if (module->hasFallback()) {
                activateModuleFallback(moduleType);
            }
        }
    }
    
    // Update hardware status from registry
    updateHardwareStatusFromRegistry();
    
    Serial.println("Hardware detection complete");
    
    // Print summary
    auto availableModules = getAvailableModules();
    auto failedModules = getFailedModules();
    
    Serial.print("Available modules: ");
    Serial.println(availableModules.size());
    Serial.print("Failed/Fallback modules: ");
    Serial.println(failedModules.size());
}

void HardwareDetectionManager::registerModule(std::unique_ptr<IHardwareModule> module) {
    if (!module) {
        Serial.println("ERROR: Attempted to register null module");
        return;
    }
    
    ModuleType moduleType = module->getModuleType();
    const char* moduleName = module->getModuleName();
    
    // Check for duplicate registration
    for (const auto& existingModule : modules) {
        if (existingModule && existingModule->getModuleType() == moduleType) {
            Serial.print("WARNING: Module ");
            Serial.print(moduleName);
            Serial.println(" already registered");
            return;
        }
    }
    
    Serial.print("Registered module: ");
    Serial.println(moduleName);
    
    modules.push_back(std::move(module));
}

// Runtime monitoring methods

void HardwareDetectionManager::performHealthChecks() {
    uint32_t currentTime = millis();
    
    // Check if it's time for health checks
    if (currentTime - lastHealthCheckTime < HEALTH_CHECK_INTERVAL_MS) {
        return;
    }
    
    lastHealthCheckTime = currentTime;
    
    Serial.println("Performing health checks...");
    
    for (auto& module : modules) {
        if (!module) continue;
        
        ModuleType moduleType = module->getModuleType();
        
        // Only check modules that are supposed to be present and initialized
        if (!registry.isModulePresent(moduleType) || !registry.isModuleHealthy(moduleType)) {
            continue;
        }
        
        // Perform health check
        bool healthy = module->healthCheck();
        
        if (healthy) {
            registry.updateHealthCheck(moduleType, currentTime);
            registry.resetFailureCount(moduleType);
        } else {
            Serial.print("Health check failed for ");
            Serial.println(module->getModuleName());
            handleModuleFailure(moduleType);
        }
    }
    
    // Update hardware status after health checks
    updateHardwareStatusFromRegistry();
}

void HardwareDetectionManager::handleModuleFailure(ModuleType moduleType) {
    IHardwareModule* module = findModule(moduleType);
    if (!module) return;
    
    // Increment failure count
    registry.incrementFailureCount(moduleType);
    uint8_t failureCount = registry.getFailureCount(moduleType);
    
    Serial.print("Module failure #");
    Serial.print(failureCount);
    Serial.print(" for ");
    Serial.println(module->getModuleName());
    
    if (failureCount < MAX_RETRY_ATTEMPTS) {
        // Schedule recovery attempt with exponential backoff
        uint32_t delay = calculateBackoffDelay(failureCount);
        scheduleRecoveryAttempt(moduleType, delay);
        
        registry.setStatusMessage(moduleType, "Retrying...");
        
        Serial.print("Scheduling recovery in ");
        Serial.print(delay);
        Serial.println("ms");
    } else {
        // Max retries exceeded - activate fallback
        registry.setStatusMessage(moduleType, "Failed - Using Fallback");
        
        if (module->hasFallback()) {
            activateModuleFallback(moduleType);
        } else {
            registry.setStatusMessage(moduleType, "Failed - No Fallback");
        }
    }
}

bool HardwareDetectionManager::attemptModuleRecovery(ModuleType moduleType) {
    IHardwareModule* module = findModule(moduleType);
    if (!module) return false;
    
    Serial.print("Attempting recovery for ");
    Serial.println(module->getModuleName());
    
    // Try to reset and re-initialize the module
    module->reset();
    delay(100);  // Give module time to reset
    
    bool detected = module->detect();
    if (detected) {
        bool initialized = module->initialize();
        if (initialized) {
            // Recovery successful
            registry.resetFailureCount(moduleType);
            registry.setModuleInitialized(moduleType, true);
            registry.setStatusMessage(moduleType, "Recovered");
            registry.updateHealthCheck(moduleType, millis());
            
            // Deactivate fallback if it was active
            if (registry.isFallbackActive(moduleType)) {
                module->deactivateFallback();
                registry.setFallbackActive(moduleType, false);
            }
            
            Serial.println("Recovery successful");
            return true;
        }
    }
    
    Serial.println("Recovery failed");
    return false;
}

void HardwareDetectionManager::processScheduledRecovery() {
    uint32_t currentTime = millis();
    
    if (currentTime < nextRecoveryAttemptTime || modulesAwaitingRecovery.empty()) {
        return;
    }
    
    // Process one module per call to avoid blocking
    ModuleType moduleType = modulesAwaitingRecovery.front();
    modulesAwaitingRecovery.erase(modulesAwaitingRecovery.begin());
    
    bool recovered = attemptModuleRecovery(moduleType);
    if (!recovered) {
        // If recovery failed, handle as another failure
        handleModuleFailure(moduleType);
    }
    
    // Update next recovery time if more modules are waiting
    if (!modulesAwaitingRecovery.empty()) {
        nextRecoveryAttemptTime = currentTime + 1000;  // 1 second between recovery attempts
    }
}

// Status query methods

bool HardwareDetectionManager::isModuleAvailable(ModuleType moduleType) const {
    return registry.isModuleHealthy(moduleType);
}

std::vector<ModuleType> HardwareDetectionManager::getAvailableModules() const {
    return registry.getAvailableModules();
}

std::vector<ModuleType> HardwareDetectionManager::getFailedModules() const {
    return registry.getFailedModules();
}

// UI integration methods

void HardwareDetectionManager::updateUIStatus(UIState& uiState) {
    // Note: UIState doesn't currently have hardware status fields
    // This method is prepared for future UIState extension
    updateHardwareStatusFromRegistry();
}

void HardwareDetectionManager::generateStatusReport(String& report) {
    report = "=== Hardware Status Report ===\n";
    
    for (const auto& module : modules) {
        if (!module) continue;
        
        ModuleType moduleType = module->getModuleType();
        ModuleStatus status = registry.getModuleStatus(moduleType);
        
        report += module->getModuleName();
        report += ": ";
        
        if (status.isPresent) {
            if (status.isInitialized && status.failureCount == 0) {
                report += "OK";
            } else if (status.isFallbackActive) {
                report += "FALLBACK";
            } else {
                report += "FAILED (";
                report += String(status.failureCount);
                report += " failures)";
            }
        } else {
            report += "NOT FOUND";
            if (status.isFallbackActive) {
                report += " (FALLBACK ACTIVE)";
            }
        }
        
        report += " - ";
        report += status.statusMessage;
        report += "\n";
    }
    
    report += "\nSummary:\n";
    report += "Available: ";
    report += String(registry.getHealthyModuleCount());
    report += "\nFallback: ";
    report += String(registry.getFallbackModules().size());
    report += "\nFailed: ";
    report += String(getFailedModules().size());
}

// Private helper methods

IHardwareModule* HardwareDetectionManager::findModule(ModuleType moduleType) {
    auto it = std::find_if(modules.begin(), modules.end(),
                          [moduleType](const std::unique_ptr<IHardwareModule>& module) {
                              return module && module->getModuleType() == moduleType;
                          });
    
    return (it != modules.end()) ? it->get() : nullptr;
}

void HardwareDetectionManager::updateHardwareStatusFromRegistry() {
    currentHardwareStatus.touchMatrixAvailable = registry.isModuleHealthy(ModuleType::TOUCH_MATRIX);
    currentHardwareStatus.distanceSensorAvailable = registry.isModuleHealthy(ModuleType::DISTANCE_SENSOR);
    currentHardwareStatus.magneticEncoderAvailable = registry.isModuleHealthy(ModuleType::MAGNETIC_ENCODER);
    currentHardwareStatus.oledDisplayAvailable = registry.isModuleHealthy(ModuleType::OLED_DISPLAY);
    
    currentHardwareStatus.touchMatrixFallbackActive = registry.isFallbackActive(ModuleType::TOUCH_MATRIX);
    currentHardwareStatus.distanceSensorFallbackActive = registry.isFallbackActive(ModuleType::DISTANCE_SENSOR);
    currentHardwareStatus.magneticEncoderFallbackActive = registry.isFallbackActive(ModuleType::MAGNETIC_ENCODER);
    currentHardwareStatus.oledDisplayFallbackActive = registry.isFallbackActive(ModuleType::OLED_DISPLAY);
    
    currentHardwareStatus.lastUpdateTime = millis();
    
    // Set status message based on overall system state
    if (registry.hasAnyFallbacks()) {
        currentHardwareStatus.statusMessage = "Some modules using fallback";
    } else if (registry.getHealthyModuleCount() == modules.size()) {
        currentHardwareStatus.statusMessage = "All modules operational";
    } else {
        currentHardwareStatus.statusMessage = "Some modules unavailable";
    }
}

uint32_t HardwareDetectionManager::calculateBackoffDelay(uint8_t attemptCount) const {
    // Exponential backoff: 1s, 2s, 4s, 8s, etc.
    uint32_t delay = RECOVERY_BASE_DELAY_MS;
    for (uint8_t i = 0; i < attemptCount && i < 8; ++i) {  // Cap at 8 to prevent overflow
        delay *= 2;
    }
    return delay;
}

void HardwareDetectionManager::scheduleRecoveryAttempt(ModuleType moduleType, uint32_t delayMs) {
    // Add to recovery queue if not already present
    auto it = std::find(modulesAwaitingRecovery.begin(), modulesAwaitingRecovery.end(), moduleType);
    if (it == modulesAwaitingRecovery.end()) {
        modulesAwaitingRecovery.push_back(moduleType);
    }
    
    // Set next recovery time if this is the first scheduled recovery
    if (nextRecoveryAttemptTime == 0) {
        nextRecoveryAttemptTime = millis() + delayMs;
    }
}

void HardwareDetectionManager::logDetectionResult(ModuleType moduleType, bool success) {
    if (success) {
        Serial.println("OK");
    } else {
        Serial.println("NOT FOUND");
    }
}

bool HardwareDetectionManager::activateModuleFallback(ModuleType moduleType) {
    IHardwareModule* module = findModule(moduleType);
    if (!module || !module->hasFallback()) {
        return false;
    }
    
    bool activated = module->activateFallback();
    if (activated) {
        registry.setFallbackActive(moduleType, true);
        
        Serial.print("Activated fallback for ");
        Serial.println(module->getModuleName());
        
        // Update status message
        String message = "Fallback Active";
        registry.setStatusMessage(moduleType, message.c_str());
        
        return true;
    }
    
    return false;
}