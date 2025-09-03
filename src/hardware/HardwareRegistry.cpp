#include "HardwareRegistry.h"
#include <algorithm>

// Status query methods (thread-safe, const)

bool HardwareRegistry::isModulePresent(ModuleType moduleType) const {
    if (!isValidModuleType(moduleType)) return false;
    
    std::lock_guard<std::mutex> lock(registryMutex);
    return moduleStatuses[static_cast<size_t>(moduleType)].isPresent;
}

bool HardwareRegistry::isModuleHealthy(ModuleType moduleType) const {
    if (!isValidModuleType(moduleType)) return false;
    
    std::lock_guard<std::mutex> lock(registryMutex);
    const auto& status = moduleStatuses[static_cast<size_t>(moduleType)];
    return status.isPresent && status.isInitialized && status.failureCount == 0;
}

bool HardwareRegistry::isFallbackActive(ModuleType moduleType) const {
    if (!isValidModuleType(moduleType)) return false;
    
    std::lock_guard<std::mutex> lock(registryMutex);
    return moduleStatuses[static_cast<size_t>(moduleType)].isFallbackActive;
}

const char* HardwareRegistry::getStatusMessage(ModuleType moduleType) const {
    if (!isValidModuleType(moduleType)) return "Invalid module";
    
    std::lock_guard<std::mutex> lock(registryMutex);
    return moduleStatuses[static_cast<size_t>(moduleType)].statusMessage;
}

uint8_t HardwareRegistry::getFailureCount(ModuleType moduleType) const {
    if (!isValidModuleType(moduleType)) return 255;
    
    std::lock_guard<std::mutex> lock(registryMutex);
    return moduleStatuses[static_cast<size_t>(moduleType)].failureCount;
}

uint32_t HardwareRegistry::getLastHealthCheck(ModuleType moduleType) const {
    if (!isValidModuleType(moduleType)) return 0;
    
    std::lock_guard<std::mutex> lock(registryMutex);
    return moduleStatuses[static_cast<size_t>(moduleType)].lastHealthCheck;
}

ModuleStatus HardwareRegistry::getModuleStatus(ModuleType moduleType) const {
    if (!isValidModuleType(moduleType)) {
        return ModuleStatus{}; // Return default-initialized status
    }
    
    std::lock_guard<std::mutex> lock(registryMutex);
    return moduleStatuses[static_cast<size_t>(moduleType)];
}

// Status update methods (thread-safe)

void HardwareRegistry::updateModuleStatus(ModuleType moduleType, const ModuleStatus& status) {
    if (!isValidModuleType(moduleType)) return;
    
    std::lock_guard<std::mutex> lock(registryMutex);
    moduleStatuses[static_cast<size_t>(moduleType)] = status;
}

void HardwareRegistry::setModulePresent(ModuleType moduleType, bool isPresent) {
    if (!isValidModuleType(moduleType)) return;
    
    std::lock_guard<std::mutex> lock(registryMutex);
    moduleStatuses[static_cast<size_t>(moduleType)].isPresent = isPresent;
}

void HardwareRegistry::setModuleInitialized(ModuleType moduleType, bool isInitialized) {
    if (!isValidModuleType(moduleType)) return;
    
    std::lock_guard<std::mutex> lock(registryMutex);
    moduleStatuses[static_cast<size_t>(moduleType)].isInitialized = isInitialized;
}

void HardwareRegistry::setFallbackActive(ModuleType moduleType, bool isFallbackActive) {
    if (!isValidModuleType(moduleType)) return;
    
    std::lock_guard<std::mutex> lock(registryMutex);
    moduleStatuses[static_cast<size_t>(moduleType)].isFallbackActive = isFallbackActive;
}

void HardwareRegistry::updateHealthCheck(ModuleType moduleType, uint32_t timestamp) {
    if (!isValidModuleType(moduleType)) return;
    
    std::lock_guard<std::mutex> lock(registryMutex);
    moduleStatuses[static_cast<size_t>(moduleType)].lastHealthCheck = timestamp;
}

void HardwareRegistry::incrementFailureCount(ModuleType moduleType) {
    if (!isValidModuleType(moduleType)) return;
    
    std::lock_guard<std::mutex> lock(registryMutex);
    auto& status = moduleStatuses[static_cast<size_t>(moduleType)];
    if (status.failureCount < 255) {  // Prevent overflow
        status.failureCount++;
    }
}

void HardwareRegistry::resetFailureCount(ModuleType moduleType) {
    if (!isValidModuleType(moduleType)) return;
    
    std::lock_guard<std::mutex> lock(registryMutex);
    moduleStatuses[static_cast<size_t>(moduleType)].failureCount = 0;
}

void HardwareRegistry::setStatusMessage(ModuleType moduleType, const char* message) {
    if (!isValidModuleType(moduleType)) return;
    
    std::lock_guard<std::mutex> lock(registryMutex);
    moduleStatuses[static_cast<size_t>(moduleType)].statusMessage = message;
}

// Bulk query methods

std::vector<ModuleType> HardwareRegistry::getAvailableModules() const {
    std::lock_guard<std::mutex> lock(registryMutex);
    std::vector<ModuleType> available;
    
    for (size_t i = 0; i < MAX_MODULES; ++i) {
        const auto& status = moduleStatuses[i];
        if (status.isPresent && status.isInitialized && status.failureCount == 0) {
            available.push_back(static_cast<ModuleType>(i));
        }
    }
    
    return available;
}

std::vector<ModuleType> HardwareRegistry::getFailedModules() const {
    std::lock_guard<std::mutex> lock(registryMutex);
    std::vector<ModuleType> failed;
    
    for (size_t i = 0; i < MAX_MODULES; ++i) {
        const auto& status = moduleStatuses[i];
        if (status.isPresent && (status.failureCount > 0 || status.isFallbackActive)) {
            failed.push_back(static_cast<ModuleType>(i));
        }
    }
    
    return failed;
}

std::vector<ModuleType> HardwareRegistry::getFallbackModules() const {
    std::lock_guard<std::mutex> lock(registryMutex);
    std::vector<ModuleType> fallback;
    
    for (size_t i = 0; i < MAX_MODULES; ++i) {
        if (moduleStatuses[i].isFallbackActive) {
            fallback.push_back(static_cast<ModuleType>(i));
        }
    }
    
    return fallback;
}

bool HardwareRegistry::hasAnyFallbacks() const {
    std::lock_guard<std::mutex> lock(registryMutex);
    
    return std::any_of(moduleStatuses.begin(), moduleStatuses.end(),
                      [](const ModuleStatus& status) {
                          return status.isFallbackActive;
                      });
}

size_t HardwareRegistry::getHealthyModuleCount() const {
    std::lock_guard<std::mutex> lock(registryMutex);
    
    return std::count_if(moduleStatuses.begin(), moduleStatuses.end(),
                        [](const ModuleStatus& status) {
                            return status.isPresent && status.isInitialized && status.failureCount == 0;
                        });
}

// Private helper methods

bool HardwareRegistry::isValidModuleType(ModuleType moduleType) const {
    return static_cast<size_t>(moduleType) < MAX_MODULES;
}