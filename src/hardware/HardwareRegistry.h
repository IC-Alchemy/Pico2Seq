#pragma once

#include "IHardwareModule.h"
#include <array>
#include <vector>
#include <mutex>
#include <cstdint>

/**
 * Module Status Structure
 * Contains all status information for a single hardware module
 */
struct ModuleStatus {
    bool isPresent = false;           // Module was detected during startup
    bool isInitialized = false;      // Module initialization completed successfully
    bool isFallbackActive = false;   // Currently using fallback controls
    uint32_t lastHealthCheck = 0;    // Timestamp of last successful health check
    uint8_t failureCount = 0;        // Number of consecutive failures
    const char* statusMessage = "";  // Human-readable status description
};

/**
 * Hardware Registry Class
 * 
 * Thread-safe storage for hardware module status and capabilities.
 * Provides fast lookup methods for runtime hardware queries and supports
 * atomic updates for safe access from both Core 0 and Core 1.
 * 
 * Requirements: 1.2, 1.3, 4.2
 */
class HardwareRegistry {
private:
    static constexpr size_t MAX_MODULES = static_cast<size_t>(ModuleType::COUNT);
    
    // Thread-safe storage for module status
    std::array<ModuleStatus, MAX_MODULES> moduleStatuses;
    mutable std::mutex registryMutex;  // Protects cross-core access
    
public:
    HardwareRegistry() = default;
    ~HardwareRegistry() = default;
    
    // Disable copy/move to prevent accidental duplication
    HardwareRegistry(const HardwareRegistry&) = delete;
    HardwareRegistry& operator=(const HardwareRegistry&) = delete;
    HardwareRegistry(HardwareRegistry&&) = delete;
    HardwareRegistry& operator=(HardwareRegistry&&) = delete;
    
    // Status query methods (thread-safe, const)
    
    /**
     * Check if a module was detected and is present
     * @param moduleType Module to check
     * @return true if module is present
     */
    bool isModulePresent(ModuleType moduleType) const;
    
    /**
     * Check if a module is initialized and healthy
     * @param moduleType Module to check
     * @return true if module is operational
     */
    bool isModuleHealthy(ModuleType moduleType) const;
    
    /**
     * Check if a module is currently using fallback controls
     * @param moduleType Module to check
     * @return true if fallback is active
     */
    bool isFallbackActive(ModuleType moduleType) const;
    
    /**
     * Get the current status message for a module
     * @param moduleType Module to check
     * @return pointer to status message string
     */
    const char* getStatusMessage(ModuleType moduleType) const;
    
    /**
     * Get the failure count for a module
     * @param moduleType Module to check
     * @return number of consecutive failures
     */
    uint8_t getFailureCount(ModuleType moduleType) const;
    
    /**
     * Get timestamp of last successful health check
     * @param moduleType Module to check
     * @return timestamp in milliseconds
     */
    uint32_t getLastHealthCheck(ModuleType moduleType) const;
    
    /**
     * Get complete status structure for a module (thread-safe copy)
     * @param moduleType Module to check
     * @return copy of module status
     */
    ModuleStatus getModuleStatus(ModuleType moduleType) const;
    
    // Status update methods (thread-safe)
    
    /**
     * Update complete module status atomically
     * @param moduleType Module to update
     * @param status New status information
     */
    void updateModuleStatus(ModuleType moduleType, const ModuleStatus& status);
    
    /**
     * Set module presence status
     * @param moduleType Module to update
     * @param isPresent Whether module is detected
     */
    void setModulePresent(ModuleType moduleType, bool isPresent);
    
    /**
     * Set module initialization status
     * @param moduleType Module to update
     * @param isInitialized Whether module is initialized
     */
    void setModuleInitialized(ModuleType moduleType, bool isInitialized);
    
    /**
     * Set fallback activation status
     * @param moduleType Module to update
     * @param isFallbackActive Whether fallback is active
     */
    void setFallbackActive(ModuleType moduleType, bool isFallbackActive);
    
    /**
     * Update health check timestamp
     * @param moduleType Module to update
     * @param timestamp Current time in milliseconds
     */
    void updateHealthCheck(ModuleType moduleType, uint32_t timestamp);
    
    /**
     * Increment failure count for a module
     * @param moduleType Module to update
     */
    void incrementFailureCount(ModuleType moduleType);
    
    /**
     * Reset failure count to zero
     * @param moduleType Module to update
     */
    void resetFailureCount(ModuleType moduleType);
    
    /**
     * Set status message for a module
     * @param moduleType Module to update
     * @param message Status message (must be persistent string)
     */
    void setStatusMessage(ModuleType moduleType, const char* message);
    
    // Bulk query methods
    
    /**
     * Get list of all modules that are present and healthy
     * @return vector of module types that are operational
     */
    std::vector<ModuleType> getAvailableModules() const;
    
    /**
     * Get list of all modules that have failed or are using fallback
     * @return vector of module types with issues
     */
    std::vector<ModuleType> getFailedModules() const;
    
    /**
     * Get list of all modules currently using fallback controls
     * @return vector of module types in fallback mode
     */
    std::vector<ModuleType> getFallbackModules() const;
    
    /**
     * Check if any modules are currently in fallback mode
     * @return true if at least one module is using fallback
     */
    bool hasAnyFallbacks() const;
    
    /**
     * Get count of modules that are present and operational
     * @return number of healthy modules
     */
    size_t getHealthyModuleCount() const;
    
private:
    /**
     * Validate module type is within valid range
     * @param moduleType Module type to validate
     * @return true if valid
     */
    bool isValidModuleType(ModuleType moduleType) const;
};