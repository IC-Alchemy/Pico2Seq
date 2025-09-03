# Design Document

## Overview

The adaptive hardware detection system provides robust hardware module discovery, fallback control mechanisms, and dynamic UI feedback for the Pico Mudras Sequencer. The design follows a plugin-like architecture where each hardware module implements a standardized detection interface, enabling easy extension for future modules while maintaining backward compatibility.

The system operates on Core 1 (UI/sensor processing) and integrates seamlessly with the existing VoiceSystem, UIState, and OLED display architecture. It provides graceful degradation when hardware modules are missing and clear visual feedback about the current hardware configuration.

## Architecture

### Core Components

#### 1. Hardware Detection Manager (`HardwareDetectionManager`)
- **Purpose**: Centralized coordinator for all hardware module detection and fallback management
- **Location**: `src/hardware/HardwareDetectionManager.h/.cpp`
- **Responsibilities**:
  - Orchestrate startup detection sequence
  - Maintain hardware registry with module status
  - Coordinate fallback activation/deactivation
  - Provide unified interface for hardware status queries
  - Handle runtime hardware failure detection

#### 2. Hardware Module Interface (`IHardwareModule`)
- **Purpose**: Standardized interface for all detectable hardware modules
- **Location**: `src/hardware/IHardwareModule.h`
- **Responsibilities**:
  - Define common detection, initialization, and status methods
  - Provide module identification and capability information
  - Enable polymorphic handling of different module types

#### 3. Hardware Registry (`HardwareRegistry`)
- **Purpose**: Thread-safe storage for hardware module status and capabilities
- **Location**: `src/hardware/HardwareRegistry.h/.cpp`
- **Responsibilities**:
  - Store module availability status
  - Track active fallback mechanisms
  - Provide fast lookup for runtime hardware queries
  - Support atomic updates for thread safety

#### 4. Fallback Control System (`FallbackControlManager`)
- **Purpose**: Manage alternative control methods when hardware is missing
- **Location**: `src/hardware/FallbackControlManager.h/.cpp`
- **Responsibilities**:
  - Activate appropriate fallback controls based on missing hardware
  - Route input events through fallback mechanisms
  - Maintain logical interface consistency
  - Support hot-swapping between hardware and fallback modes

### Module-Specific Components

#### 1. Touch Matrix Module (`TouchMatrixModule`)
- **Hardware**: MPR121 touch sensor (I2C address 0x5A)
- **Fallback**: 4 digital buttons with internal pull-ups
- **Detection Method**: I2C communication test with device ID verification
- **Fallback Pins**: GPIO 2, 3, 4, 5 (configurable)

#### 2. Distance Sensor Module (`DistanceSensorModule`)
- **Hardware**: VL53L1X LIDAR sensor (I2C address 0x29)
- **Fallback**: Step button + AS5600 rotary encoder combination
- **Detection Method**: I2C communication test with sensor ID verification
- **Fallback Logic**: Hold step button + rotate encoder to modify step parameters

#### 3. Magnetic Encoder Module (`MagneticEncoderModule`)
- **Hardware**: AS5600 magnetic encoder (I2C address 0x36)
- **Fallback**: Digital buttons for parameter increment/decrement
- **Detection Method**: I2C communication test with register read verification
- **Fallback Pins**: GPIO 6, 7 for increment/decrement (configurable)

#### 4. OLED Display Module (`OLEDDisplayModule`)
- **Hardware**: SH110X OLED display (I2C address 0x3C)
- **Fallback**: Serial console output for status information
- **Detection Method**: I2C communication test with display initialization
- **Fallback Logic**: Mirror display content to Serial output

## Components and Interfaces

### Hardware Module Interface

```cpp
class IHardwareModule {
public:
    virtual ~IHardwareModule() = default;
    
    // Core detection and initialization
    virtual bool detect() = 0;
    virtual bool initialize() = 0;
    virtual bool isConnected() const = 0;
    
    // Module identification
    virtual const char* getModuleName() const = 0;
    virtual uint8_t getModuleId() const = 0;
    virtual uint32_t getCapabilities() const = 0;
    
    // Runtime status
    virtual bool healthCheck() = 0;
    virtual void reset() = 0;
    
    // Fallback support
    virtual bool hasFallback() const = 0;
    virtual bool activateFallback() = 0;
    virtual bool deactivateFallback() = 0;
};
```

### Hardware Registry Structure

```cpp
struct ModuleStatus {
    bool isPresent;
    bool isInitialized;
    bool isFallbackActive;
    uint32_t lastHealthCheck;
    uint8_t failureCount;
    const char* statusMessage;
};

class HardwareRegistry {
private:
    std::array<ModuleStatus, MAX_MODULES> moduleStatuses;
    mutable std::mutex registryMutex;  // Thread safety for Core 0/1 access
    
public:
    bool isModulePresent(uint8_t moduleId) const;
    bool isModuleHealthy(uint8_t moduleId) const;
    bool isFallbackActive(uint8_t moduleId) const;
    void updateModuleStatus(uint8_t moduleId, const ModuleStatus& status);
    std::vector<uint8_t> getFailedModules() const;
};
```

### Detection Manager Interface

```cpp
class HardwareDetectionManager {
private:
    std::vector<std::unique_ptr<IHardwareModule>> modules;
    HardwareRegistry registry;
    FallbackControlManager fallbackManager;
    
public:
    // Initialization and detection
    bool initializeSystem();
    void performStartupDetection();
    void registerModule(std::unique_ptr<IHardwareModule> module);
    
    // Runtime monitoring
    void performHealthChecks();
    void handleModuleFailure(uint8_t moduleId);
    void attemptModuleRecovery(uint8_t moduleId);
    
    // Status queries
    bool isModuleAvailable(uint8_t moduleId) const;
    std::vector<uint8_t> getAvailableModules() const;
    std::vector<uint8_t> getFailedModules() const;
    
    // UI integration
    void updateUIStatus(UIState& uiState);
    void generateStatusReport(String& report);
};
```

## Data Models

### Module Identification

```cpp
enum class ModuleType : uint8_t {
    TOUCH_MATRIX = 0,
    DISTANCE_SENSOR = 1,
    MAGNETIC_ENCODER = 2,
    OLED_DISPLAY = 3,
    LED_MATRIX = 4,
    // Future modules can be added here
    COUNT
};

struct ModuleCapabilities {
    static constexpr uint32_t REAL_TIME_INPUT = 0x01;
    static constexpr uint32_t PARAMETER_CONTROL = 0x02;
    static constexpr uint32_t VISUAL_FEEDBACK = 0x04;
    static constexpr uint32_t STEP_SEQUENCING = 0x08;
    static constexpr uint32_t FALLBACK_AVAILABLE = 0x10;
};
```

### Fallback Configuration

```cpp
struct FallbackConfig {
    ModuleType primaryModule;
    ModuleType fallbackModule;
    std::vector<uint8_t> fallbackPins;
    std::function<void()> activationCallback;
    std::function<void()> deactivationCallback;
};

struct TouchMatrixFallbackConfig {
    uint8_t buttonPins[4] = {2, 3, 4, 5};  // GPIO pins for 4 buttons
    bool useInternalPullups = true;
    uint32_t debounceTimeMs = 50;
};

struct DistanceSensorFallbackConfig {
    uint8_t stepButtonPin = 8;  // Pin for step selection
    bool requiresAS5600 = true;  // Requires magnetic encoder for value adjustment
    float sensitivity = 1.0f;    // Adjustment sensitivity multiplier
};
```

### UI Status Integration

```cpp
struct HardwareStatus {
    bool touchMatrixAvailable;
    bool distanceSensorAvailable;
    bool magneticEncoderAvailable;
    bool oledDisplayAvailable;
    
    bool touchMatrixFallbackActive;
    bool distanceSensorFallbackActive;
    bool magneticEncoderFallbackActive;
    bool oledDisplayFallbackActive;
    
    String statusMessage;
    uint32_t lastUpdateTime;
};

// Extension to UIState
struct UIState {
    // ... existing fields ...
    HardwareStatus hardwareStatus;
    bool showHardwareStatus = false;  // Toggle for status display
};
```

## Error Handling

### Detection Failure Handling

1. **I2C Communication Errors**
   - Retry detection with exponential backoff (100ms, 200ms, 400ms)
   - Log specific I2C error codes for debugging
   - Mark module as unavailable after 3 failed attempts
   - Activate fallback mechanism automatically

2. **Initialization Failures**
   - Attempt module reset before retry
   - Check for hardware conflicts (address collisions)
   - Validate power supply stability
   - Provide detailed error reporting via OLED/Serial

3. **Runtime Failures**
   - Detect communication timeouts during operation
   - Implement graceful degradation without system restart
   - Attempt automatic recovery every 30 seconds
   - Notify user of hardware status changes

### Fallback Activation Logic

```cpp
void HardwareDetectionManager::handleModuleFailure(uint8_t moduleId) {
    ModuleStatus status = registry.getModuleStatus(moduleId);
    
    if (status.failureCount < MAX_RETRY_ATTEMPTS) {
        // Attempt recovery
        scheduleRecoveryAttempt(moduleId, calculateBackoffDelay(status.failureCount));
    } else {
        // Activate fallback
        if (modules[moduleId]->hasFallback()) {
            fallbackManager.activateFallback(moduleId);
            registry.setFallbackActive(moduleId, true);
            updateUIStatus();
        }
    }
}
```

## Testing Strategy

### Unit Testing

1. **Module Detection Tests**
   - Mock I2C communication for each module type
   - Test detection success and failure scenarios
   - Verify proper error handling and retry logic
   - Test module identification and capability reporting

2. **Fallback Mechanism Tests**
   - Test automatic fallback activation on module failure
   - Verify fallback control routing and event handling
   - Test hot-swapping between hardware and fallback modes
   - Validate logical interface consistency

3. **Registry Tests**
   - Test thread-safe access from multiple cores
   - Verify atomic status updates
   - Test status query performance and accuracy
   - Validate memory usage and cleanup

### Integration Testing

1. **Hardware Configuration Tests**
   - Test all possible hardware combinations (present/absent)
   - Verify system stability with partial hardware configurations
   - Test startup behavior with different hardware states
   - Validate UI feedback accuracy for each configuration

2. **Runtime Behavior Tests**
   - Test hardware failure detection during operation
   - Verify graceful degradation and recovery
   - Test user interaction with fallback controls
   - Validate MIDI and audio output consistency

3. **Performance Tests**
   - Measure detection time impact on startup
   - Test health check overhead during operation
   - Verify real-time performance with fallback controls
   - Validate memory usage with all modules active

### Hardware-in-the-Loop Testing

1. **Physical Hardware Tests**
   - Test with actual hardware modules connected/disconnected
   - Verify I2C bus stability with multiple devices
   - Test power-on sequence with different hardware states
   - Validate electromagnetic interference resilience

2. **User Experience Tests**
   - Test fallback control usability and responsiveness
   - Verify UI feedback clarity and timing
   - Test system behavior during live performance scenarios
   - Validate error recovery user experience

## Implementation Notes

### Core 0/Core 1 Considerations

- **Core 0 (Audio)**: Only queries hardware status, never performs detection
- **Core 1 (UI/Sensors)**: Handles all detection, initialization, and fallback management
- **Thread Safety**: Hardware registry uses mutex for safe cross-core access
- **Performance**: Detection occurs only at startup and during scheduled health checks

### Memory Management

- **Static Allocation**: All module instances use static allocation for real-time safety
- **Registry Size**: Fixed-size arrays for predictable memory usage
- **String Handling**: Use Arduino String class sparingly, prefer const char* for status messages

### Integration Points

- **UIState Integration**: Hardware status becomes part of central UI state
- **OLED Display**: Hardware status display mode accessible via button combination
- **LED Matrix**: Visual indicators for hardware status (optional)
- **Serial Output**: Detailed hardware status available via Serial console

This design provides a robust, extensible foundation for adaptive hardware detection while maintaining the real-time performance requirements of the audio system and the modular architecture of the existing codebase.