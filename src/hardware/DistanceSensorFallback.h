#pragma once

#include <Arduino.h>
#include <cstdint>
#include <functional>

// Forward declarations
class AS5600Sensor;
class UIState;
class Sequencer;

/**
 * Distance Sensor Fallback Implementation
 * 
 * Provides step button + AS5600 encoder fallback for VL53L1X distance sensor.
 * Implements hold-and-rotate logic for parameter modification and creates
 * distance value simulation from encoder input when step button is held.
 * 
 */
class DistanceSensorFallback {
public:
    /**
     * Fallback Configuration Structure
     * Defines GPIO pins and behavior parameters for distance sensor fallback
     */
    struct Config {
        uint8_t stepButtonPin = 8;             // GPIO pin for step selection button
        bool useInternalPullup = true;         // Enable internal pull-up resistor
        uint32_t debounceTimeMs = 50;          // Button debounce time
        bool invertLogic = true;               // true = active low, false = active high
        float sensitivity = 1.0f;              // Adjustment sensitivity multiplier
        float minDistance = 50.0f;             // Minimum simulated distance (mm)
        float maxDistance = 400.0f;            // Maximum simulated distance (mm)
        uint16_t encoderDeadzone = 5;          // Encoder deadzone to prevent jitter
        uint32_t holdTimeMs = 200;             // Time to hold button before encoder becomes active
    };
    
    /**
     * Button State Structure
     * Tracks current state and timing for the step button
     */
    struct ButtonState {
        bool isPressed = false;        // Current pressed state
        bool lastReading = false;      // Last raw reading
        uint32_t lastChangeTime = 0;   // Time of last state change
        uint32_t pressTime = 0;        // Time when button was pressed
        uint32_t holdStartTime = 0;    // Time when hold period started
        bool isHolding = false;        // true if button has been held long enough
        bool stateChanged = false;     // Flag indicating state changed this update
    };
    
    /**
     * Encoder State Structure
     * Tracks encoder position and movement for distance simulation
     */
    struct EncoderState {
        int16_t lastPosition = 0;      // Last encoder position
        int16_t currentPosition = 0;   // Current encoder position
        int16_t deltaPosition = 0;     // Change in position since last update
        float simulatedDistance = 200.0f; // Current simulated distance value
        bool hasValidReading = false;  // true if encoder is providing valid data
        uint32_t lastUpdateTime = 0;   // Time of last encoder update
    };
    
private:
    Config config;
    ButtonState buttonState;
    EncoderState encoderState;
    bool isActive = false;
    bool isInitialized = false;
    uint32_t lastUpdateTime = 0;
    
    // Reference to AS5600 encoder for fallback operation
    AS5600Sensor* encoder = nullptr;
    
    // Event callback for distance value changes
    std::function<void(float)> distanceCallback;
    
public:
    DistanceSensorFallback() = default;
    ~DistanceSensorFallback() = default;
    
    // Disable copy/move to prevent accidental duplication
    DistanceSensorFallback(const DistanceSensorFallback&) = delete;
    DistanceSensorFallback& operator=(const DistanceSensorFallback&) = delete;
    DistanceSensorFallback(DistanceSensorFallback&&) = delete;
    DistanceSensorFallback& operator=(DistanceSensorFallback&&) = delete;
    
    // Initialization and configuration
    
    /**
     * Initialize the distance sensor fallback system
     * @param encoderRef Reference to AS5600 encoder sensor
     * @param callback Function to call when distance values change
     * @return true if initialization successful
     */
    bool initialize(AS5600Sensor& encoderRef, std::function<void(float)> callback);
    
    /**
     * Configure fallback settings
     * @param newConfig Fallback configuration parameters
     */
    void configure(const Config& newConfig);
    
    /**
     * Get current configuration
     * @return const reference to current config
     */
    const Config& getConfig() const { return config; }
    
    // Activation and deactivation
    
    /**
     * Activate the fallback system
     * Initializes GPIO pins and starts monitoring button/encoder
     * @return true if activation successful
     */
    bool activate();
    
    /**
     * Deactivate the fallback system
     * Stops monitoring and resets state
     * @return true if deactivation successful
     */
    bool deactivate();
    
    /**
     * Check if fallback system is currently active
     * @return true if active and monitoring
     */
    bool isActivated() const { return isActive; }
    
    // Runtime operation
    
    /**
     * Update button and encoder states
     * Call this from the main loop to process changes
     */
    void update();
    
    /**
     * Process parameter updates when button is held and encoder moves
     * @param uiState UI state for parameter updates
     * @param sequencers Array of sequencer pointers
     * @param sequencerCount Number of sequencers
     */
    void processParameterUpdates(UIState& uiState,
                                Sequencer* const* sequencers,
                                size_t sequencerCount);
    
    // Status and diagnostics
    
    /**
     * Get current button state
     * @return const reference to button state
     */
    const ButtonState& getButtonState() const { return buttonState; }
    
    /**
     * Get current encoder state
     * @return const reference to encoder state
     */
    const EncoderState& getEncoderState() const { return encoderState; }
    
    /**
     * Check if step button is currently pressed
     * @return true if button is pressed
     */
    bool isStepButtonPressed() const { return buttonState.isPressed; }
    
    /**
     * Check if step button is being held (past hold threshold)
     * @return true if button is being held
     */
    bool isStepButtonHeld() const { return buttonState.isHolding; }
    
    /**
     * Get current simulated distance value
     * @return distance in millimeters
     */
    float getSimulatedDistance() const { return encoderState.simulatedDistance; }
    
    /**
     * Check if encoder is providing valid readings
     * @return true if encoder is connected and working
     */
    bool isEncoderValid() const { return encoderState.hasValidReading; }
    
    // Distance simulation
    
    /**
     * Set simulated distance value directly
     * @param distance Distance value in millimeters
     */
    void setSimulatedDistance(float distance);
    
    /**
     * Reset simulated distance to center of range
     */
    void resetSimulatedDistance();
    
    /**
     * Get distance as normalized value (0.0 to 1.0)
     * @return normalized distance value
     */
    float getNormalizedDistance() const;
    
    // Testing and diagnostics
    
    /**
     * Test button and encoder connections
     * @return true if both components respond correctly
     */
    bool testConnections();
    
    /**
     * Get diagnostic information as string
     * @param buffer Buffer to write diagnostic info
     * @param bufferSize Size of buffer
     * @return number of characters written
     */
    size_t getDiagnostics(char* buffer, size_t bufferSize) const;
    
private:
    // Internal helper methods
    
    /**
     * Initialize GPIO pin for step button
     */
    void initializePin();
    
    /**
     * Read raw button state from GPIO
     * @return true if button is pressed (after logic inversion)
     */
    bool readButtonRaw() const;
    
    /**
     * Update button state with debouncing and hold detection
     * @return true if button state changed
     */
    bool updateButtonState();
    
    /**
     * Update encoder state and calculate position changes
     * @return true if encoder position changed significantly
     */
    bool updateEncoderState();
    
    /**
     * Update simulated distance based on encoder movement
     * @param deltaPosition Change in encoder position
     */
    void updateSimulatedDistance(int16_t deltaPosition);
    
    /**
     * Constrain distance value to configured range
     * @param distance Distance value to constrain
     * @return constrained distance value
     */
    float constrainDistance(float distance) const;
    
    /**
     * Convert encoder delta to distance delta
     * @param encoderDelta Change in encoder position
     * @return change in distance (mm)
     */
    float encoderDeltaToDistanceDelta(int16_t encoderDelta) const;
    
    /**
     * Reset button state to default
     */
    void resetButtonState();
    
    /**
     * Reset encoder state to default
     */
    void resetEncoderState();
    
    /**
     * Check if encoder delta is significant enough to process
     * @param delta Encoder position change
     * @return true if delta exceeds deadzone
     */
    bool isSignificantEncoderDelta(int16_t delta) const;
};