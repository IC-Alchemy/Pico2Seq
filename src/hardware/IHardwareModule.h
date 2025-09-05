#pragma once

#include "HardwareModuleDefs.h"
#include <cstdint>

/**
 * Abstract Hardware Module Interface
 * 
 * Standardized interface for all detectable hardware modules in the system.
 * Each hardware component (MPR121, VL53L1X, AS5600, etc.) implements this
 * interface to provide consistent detection, initialization, and status methods.
 * 
 */
class IHardwareModule {
public:
    virtual ~IHardwareModule() = default;
    
    // Core detection and initialization methods
    
    /**
     * Detect if the hardware module is present and responsive
     * @return true if module is detected and communicating properly
     */
    virtual bool detect() = 0;
    
    /**
     * Initialize the hardware module after successful detection
     * @return true if initialization completed successfully
     */
    virtual bool initialize() = 0;
    
    /**
     * Check if the module is currently connected and responsive
     * @return true if module is connected and operational
     */
    virtual bool isConnected() const = 0;
    
    // Module identification methods
    
    /**
     * Get human-readable module name for debugging and UI display
     * @return pointer to null-terminated string with module name
     */
    virtual const char* getModuleName() const = 0;
    
    /**
     * Get unique module identifier for registry lookups
     * @return module type enum value
     */
    virtual ModuleType getModuleType() const = 0;
    
    /**
     * Get module capability flags indicating what features are supported
     * @return bitmask of ModuleCapabilities flags
     */
    virtual uint32_t getCapabilities() const = 0;
    
    // Runtime status and health monitoring
    
    /**
     * Perform health check to verify module is still operational
     * Called periodically during runtime to detect failures
     * @return true if module passes health check
     */
    virtual bool healthCheck() = 0;
    
    /**
     * Reset the module to recover from communication failures
     * Attempts to restore module to operational state
     */
    virtual void reset() = 0;
    
    // Fallback support interface
    
    /**
     * Check if this module has fallback control methods available
     * @return true if fallback controls are implemented
     */
    virtual bool hasFallback() const = 0;
    
    /**
     * Activate fallback control method when hardware is unavailable
     * @return true if fallback was successfully activated
     */
    virtual bool activateFallback() = 0;
    
    /**
     * Deactivate fallback and return to hardware control
     * @return true if fallback was successfully deactivated
     */
    virtual bool deactivateFallback() = 0;
    
    /**
     * Check if fallback mode is currently active
     * @return true if using fallback controls instead of hardware
     */
    virtual bool isFallbackActive() const = 0;
};