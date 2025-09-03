#pragma once

#include "IHardwareModule.h"
#include "HardwareRegistry.h"
#include <vector>
#include <memory>
#include <cstdint>

// Forward declarations
struct UIState;
class String;

/**
 * Hardware Status Structure for UI Integration
 * Contains hardware status information that can be displayed in the UI
 */
struct HardwareStatus {
    bool touchMatrixAvailable = false;
    bool distanceSensorAvailable = false;
    bool magneticEncoderAvailable = false;
    bool oledDisplayAvailable = false;
    
    bool touchMatrixFallbackActive = false;
    bool distanceSensorFallbackActive = false;
    bool magneticEncoderFallbackActive = false;
    bool oledDisplayFallbackActive = false;
    
    const char* statusMessage = "";
    uint32_t lastUpdateTime = 0;
};

/**
 * Hardware Detection Manager Class
 * 
 * Centralized coordinator for all hardware module detection and fallback management.
 * Orchestrates startup detection sequence, maintains hardware registry, coordinates
 * fallback activation/deactivation, and provides unified interface for hardware status queries.
 * 
 * Requirements: 1.1, 1.2, 1.5, 4.3
 */
class HardwareDetectionManager {
private:
    static constexpr uint8_t MAX_RETRY_ATTEMPTS = 3;
    static constexpr uint32_t HEALTH_CHECK_INTERVAL_MS = 30000;  // 30 seconds
    static constexpr uint32_t RECOVERY_BASE_DELAY_MS = 1000;     // Base delay for exponential backoff
    
    // Core components
    std::vector<std::unique_ptr<IHardwareModule>> modules;
    HardwareRegistry registry;
    
    // Health check scheduling
    uint32_t lastHealthCheckTime = 0;
    uint32_t nextRecoveryAttemptTime = 0;
    std::vector<ModuleType> modulesAwaitingRecovery;
    
    // Status tracking
    HardwareStatus currentHardwareStatus;
    bool systemInitialized = false;
    
public:
    HardwareDetectionManager() = default;
    ~HardwareDetectionManager() = default;
    
    // Disable copy/move to prevent accidental duplication
    HardwareDetectionManager(const HardwareDetectionManager&) = delete;
    HardwareDetectionManager& operator=(const HardwareDetectionManager&) = delete;
    HardwareDetectionManager(HardwareDetectionManager&&) = delete;
    HardwareDetectionManager& operator=(HardwareDetectionManager&&) = delete;
    
    // Initialization and detection methods
    
    /**
     * Initialize the hardware detection system
     * Must be called before any other methods
     * @return true if system initialized successfully
     */
    bool initializeSystem();
    
    /**
     * Perform startup detection sequence for all registered modules
     * Detects all hardware modules and activates fallbacks as needed
     */
    void performStartupDetection();
    
    /**
     * Register a hardware module for detection and management
     * @param module Unique pointer to module implementation
     */
    void registerModule(std::unique_ptr<IHardwareModule> module);
    
    // Runtime monitoring methods
    
    /**
     * Perform periodic health checks on all modules
     * Should be called regularly from main loop (every 30 seconds)
     */
    void performHealthChecks();
    
    /**
     * Handle failure of a specific module
     * Implements retry logic and fallback activation
     * @param moduleType Module that has failed
     */
    void handleModuleFailure(ModuleType moduleType);
    
    /**
     * Attempt recovery of a failed module
     * @param moduleType Module to attempt recovery on
     * @return true if recovery was successful
     */
    bool attemptModuleRecovery(ModuleType moduleType);
    
    /**
     * Process scheduled recovery attempts
     * Should be called from main loop to handle timed recovery
     */
    void processScheduledRecovery();
    
    // Status query methods
    
    /**
     * Check if a specific module is available and operational
     * @param moduleType Module to check
     * @return true if module is available for use
     */
    bool isModuleAvailable(ModuleType moduleType) const;
    
    /**
     * Get list of all available and operational modules
     * @return vector of module types that are working
     */
    std::vector<ModuleType> getAvailableModules() const;
    
    /**
     * Get list of all failed or problematic modules
     * @return vector of module types with issues
     */
    std::vector<ModuleType> getFailedModules() const;
    
    /**
     * Get the hardware registry for direct access
     * @return reference to the hardware registry
     */
    const HardwareRegistry& getRegistry() const { return registry; }
    
    /**
     * Get current hardware status for UI display
     * @return reference to current hardware status
     */
    const HardwareStatus& getHardwareStatus() const { return currentHardwareStatus; }
    
    // UI integration methods
    
    /**
     * Update UI state with current hardware status
     * Extends UIState with hardware information
     * @param uiState UI state structure to update
     */
    void updateUIStatus(UIState& uiState);
    
    /**
     * Generate comprehensive status report for debugging
     * @param report String to populate with status information
     */
    void generateStatusReport(String& report);
    
    /**
     * Check if system has completed initialization
     * @return true if startup detection has completed
     */
    bool isSystemInitialized() const { return systemInitialized; }
    
private:
    // Internal helper methods
    
    /**
     * Find module instance by type
     * @param moduleType Module type to find
     * @return pointer to module or nullptr if not found
     */
    IHardwareModule* findModule(ModuleType moduleType);
    
    /**
     * Update hardware status structure from registry
     * Synchronizes HardwareStatus with current registry state
     */
    void updateHardwareStatusFromRegistry();
    
    /**
     * Calculate exponential backoff delay for recovery attempts
     * @param attemptCount Number of previous attempts
     * @return delay in milliseconds
     */
    uint32_t calculateBackoffDelay(uint8_t attemptCount) const;
    
    /**
     * Schedule a module for recovery attempt
     * @param moduleType Module to schedule for recovery
     * @param delayMs Delay before attempting recovery
     */
    void scheduleRecoveryAttempt(ModuleType moduleType, uint32_t delayMs);
    
    /**
     * Log detection results to Serial output
     * @param moduleType Module that was detected/failed
     * @param success Whether detection was successful
     */
    void logDetectionResult(ModuleType moduleType, bool success);
    
    /**
     * Activate fallback for a module if available
     * @param moduleType Module to activate fallback for
     * @return true if fallback was activated
     */
    bool activateModuleFallback(ModuleType moduleType);
};