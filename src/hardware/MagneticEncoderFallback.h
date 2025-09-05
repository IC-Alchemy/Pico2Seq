#pragma once

#include <Arduino.h>
#include <cstdint>
#include <functional>

// Forward declarations
class UIState;
class Sequencer;

/**
 * Magnetic Encoder Fallback Implementation
 * 
 * Provides increment/decrement button fallback for AS5600 magnetic encoder.
 * Implements button-based parameter adjustment logic and creates encoder
 * value simulation from button presses with repeat functionality.
 * 
 */
class MagneticEncoderFallback {
public:
    /**
     * Fallback Configuration Structure
     * Defines GPIO pins and behavior parameters for encoder fallback
     */
    struct Config {
        uint8_t incrementPin = 6;              // GPIO pin for increment button
        uint8_t decrementPin = 7;              // GPIO pin for decrement button
        bool useInternalPullups = true;        // Enable internal pull-up resistors
        uint32_t debounceTimeMs = 50;          // Button debounce time
        bool invertLogic = true;               // true = active low, false = active high
        uint32_t repeatDelayMs = 500;          // Initial repeat delay
        uint32_t repeatRateMs = 100;           // Repeat rate for held buttons
        uint32_t fastRepeatThresholdMs = 2000; // Time to switch to fast repeat
        uint32_t fastRepeatRateMs = 50;        // Fast repeat rate
        int16_t incrementValue = 1;            // Value to add per increment
        int16_t decrementValue = -1;           // Value to add per decrement
        int16_t fastIncrementValue = 5;        // Value for fast increment
        int16_t fastDecrementValue = -5;       // Value for fast decrement
    };
    
    /**
     * Button State Structure
     * Tracks current state and timing for each button
     */
    struct ButtonState {
        bool isPressed = false;        // Current pressed state
        bool lastReading = false;      // Last raw reading
        uint32_t lastChangeTime = 0;   // Time of last state change
        uint32_t pressTime = 0;        // Time when button was pressed
        uint32_t lastRepeatTime = 0;   // Time of last repeat event
        bool isRepeating = false;      // true if button is in repeat mode
        bool isFastRepeating = false;  // true if button is in fast repeat mode
        bool stateChanged = false;     // Flag indicating state changed this update
    };
    
    /**
     * Encoder Simulation State
     * Tracks simulated encoder position and movement
     */
    struct EncoderState {
        int16_t position = 0;          // Current simulated encoder position
        int16_t lastPosition = 0;      // Previous encoder position
        int16_t deltaPosition = 0;     // Change in position since last update
        float normalizedPosition = 0.5f; // Position as normalized value (0.0-1.0)
        int16_t minPosition = -2048;   // Minimum encoder position
        int16_t maxPosition = 2047;    // Maximum encoder position
        uint32_t lastUpdateTime = 0;   // Time of last position update
    };
    
private:
    Config config;
    ButtonState incrementButtonState;
    ButtonState decrementButtonState;
    EncoderState encoderState;
    bool isActive = false;
    bool isInitialized = false;
    uint32_t lastUpdateTime = 0;
    
    // Event callback for encoder value changes
    std::function<void(int16_t)> encoderCallback;
    
public:
    MagneticEncoderFallback() = default;
    ~MagneticEncoderFallback() = default;
    
    // Disable copy/move to prevent accidental duplication
    MagneticEncoderFallback(const MagneticEncoderFallback&) = delete;
    MagneticEncoderFallback& operator=(const MagneticEncoderFallback&) = delete;
    MagneticEncoderFallback(MagneticEncoderFallback&&) = delete;
    MagneticEncoderFallback& operator=(MagneticEncoderFallback&&) = delete;
    
    // Initialization and configuration
    
    /**
     * Initialize the magnetic encoder fallback system
     * @param callback Function to call when encoder values change
     * @return true if initialization successful
     */
    bool initialize(std::function<void(int16_t)> callback);
    
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
     * Initializes GPIO pins and starts monitoring buttons
     * @return true if activation successful
     */
    bool activate();
    
    /**
     * Deactivate the fallback system
     * Stops monitoring buttons and resets state
     * @return true if deactivation successful
     */
    bool deactivate();
    
    /**
     * Check if fallback system is currently active
     * @return true if active and monitoring buttons
     */
    bool isActivated() const { return isActive; }
    
    // Runtime operation
    
    /**
     * Update button states and generate encoder events
     * Call this from the main loop to process button changes
     */
    void update();
    
    /**
     * Process parameter updates based on encoder simulation
     * @param uiState UI state for parameter updates
     * @param sequencers Array of sequencer pointers
     * @param sequencerCount Number of sequencers
     */
    void processParameterUpdates(UIState& uiState,
                                Sequencer* const* sequencers,
                                size_t sequencerCount);
    
    // Status and diagnostics
    
    /**
     * Get current increment button state
     * @return const reference to increment button state
     */
    const ButtonState& getIncrementButtonState() const { return incrementButtonState; }
    
    /**
     * Get current decrement button state
     * @return const reference to decrement button state
     */
    const ButtonState& getDecrementButtonState() const { return decrementButtonState; }
    
    /**
     * Get current encoder simulation state
     * @return const reference to encoder state
     */
    const EncoderState& getEncoderState() const { return encoderState; }
    
    /**
     * Check if increment button is currently pressed
     * @return true if increment button is pressed
     */
    bool isIncrementPressed() const { return incrementButtonState.isPressed; }
    
    /**
     * Check if decrement button is currently pressed
     * @return true if decrement button is pressed
     */
    bool isDecrementPressed() const { return decrementButtonState.isPressed; }
    
    /**
     * Check if any button is currently pressed
     * @return true if at least one button is pressed
     */
    bool isAnyButtonPressed() const;
    
    /**
     * Get current simulated encoder position
     * @return encoder position value
     */
    int16_t getEncoderPosition() const { return encoderState.position; }
    
    /**
     * Get encoder position as normalized value (0.0 to 1.0)
     * @return normalized encoder position
     */
    float getNormalizedPosition() const { return encoderState.normalizedPosition; }
    
    // Encoder simulation control
    
    /**
     * Set simulated encoder position directly
     * @param position New encoder position
     */
    void setEncoderPosition(int16_t position);
    
    /**
     * Reset simulated encoder position to center
     */
    void resetEncoderPosition();
    
    /**
     * Set encoder position range
     * @param minPos Minimum position value
     * @param maxPos Maximum position value
     */
    void setEncoderRange(int16_t minPos, int16_t maxPos);
    
    /**
     * Get encoder position range
     * @param minPos Output parameter for minimum position
     * @param maxPos Output parameter for maximum position
     */
    void getEncoderRange(int16_t& minPos, int16_t& maxPos) const;
    
    // Testing and diagnostics
    
    /**
     * Test button connections
     * @return true if both buttons respond correctly
     */
    bool testButtons();
    
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
     * Initialize GPIO pins for both buttons
     */
    void initializePins();
    
    /**
     * Read raw button state from GPIO
     * @param pin GPIO pin to read
     * @return true if button is pressed (after logic inversion)
     */
    bool readButtonRaw(uint8_t pin) const;
    
    /**
     * Update state for a single button with debouncing and repeat logic
     * @param pin GPIO pin to read
     * @param buttonState Button state structure to update
     * @return true if button state changed or repeat event occurred
     */
    bool updateButtonState(uint8_t pin, ButtonState& buttonState);
    
    /**
     * Handle button repeat logic for held buttons
     * @param buttonState Button state to check for repeat
     * @return true if repeat event should be generated
     */
    bool handleButtonRepeat(ButtonState& buttonState);
    
    /**
     * Update simulated encoder position
     * @param increment Value to add to current position
     */
    void updateEncoderPosition(int16_t increment);
    
    /**
     * Constrain encoder position to valid range
     * @param position Position value to constrain
     * @return constrained position value
     */
    int16_t constrainPosition(int16_t position) const;
    
    /**
     * Update normalized position based on current position
     */
    void updateNormalizedPosition();
    
    /**
     * Reset button state to default
     * @param buttonState Button state to reset
     */
    void resetButtonState(ButtonState& buttonState);
    
    /**
     * Reset encoder state to default
     */
    void resetEncoderState();
    
    /**
     * Generate encoder change event
     * @param newPosition New encoder position
     */
    void generateEncoderEvent(int16_t newPosition);
};