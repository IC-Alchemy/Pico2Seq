#pragma once

#include <Arduino.h>
#include <cstdint>
#include <functional>

// Forward declarations
struct MatrixButtonEvent;
class UIState;
class Sequencer;
class MidiNoteManager;

/**
 * Touch Matrix Fallback Implementation
 * 
 * Provides 4-button digital input fallback for MPR121 touch matrix.
 * Implements button debouncing, event generation, and mapping from
 * button presses to touch matrix events for seamless operation.
 * 
 */
class TouchMatrixFallback {
public:
    /**
     * Button Configuration Structure
     * Defines GPIO pins and timing parameters for fallback buttons
     */
    struct Config {
        uint8_t buttonPins[4] = {2, 3, 4, 5};  // GPIO pins for 4 buttons
        bool useInternalPullups = true;         // Enable internal pull-up resistors
        uint32_t debounceTimeMs = 50;          // Button debounce time
        bool invertLogic = true;               // true = active low, false = active high
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
        bool stateChanged = false;     // Flag indicating state changed this update
    };
    
    /**
     * Touch Matrix Button Mapping
     * Maps fallback buttons to touch matrix button indices
     */
    struct ButtonMapping {
        uint8_t fallbackButton;    // Fallback button index (0-3)
        uint8_t matrixButton;      // Touch matrix button index
        const char* description;   // Human-readable description
    };
    
private:
    Config config;
    ButtonState buttonStates[4];
    bool isActive = false;
    bool isInitialized = false;
    uint32_t lastUpdateTime = 0;
    
    // Event callback for generated matrix events
    std::function<void(const MatrixButtonEvent&)> eventCallback;
    
    // Default button mapping (can be customized)
    static const ButtonMapping defaultMapping[4];
    
public:
    TouchMatrixFallback() = default;
    ~TouchMatrixFallback() = default;
    
    // Disable copy/move to prevent accidental duplication
    TouchMatrixFallback(const TouchMatrixFallback&) = delete;
    TouchMatrixFallback& operator=(const TouchMatrixFallback&) = delete;
    TouchMatrixFallback(TouchMatrixFallback&&) = delete;
    TouchMatrixFallback& operator=(TouchMatrixFallback&&) = delete;
    
    // Initialization and configuration
    
    /**
     * Initialize the touch matrix fallback system
     * @param eventCallback Function to call when matrix events are generated
     * @return true if initialization successful
     */
    bool initialize(std::function<void(const MatrixButtonEvent&)> eventCallback);
    
    /**
     * Configure fallback button settings
     * @param newConfig Button configuration parameters
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
     * Update button states and generate events
     * Call this from the main loop to process button changes
     */
    void update();
    
    /**
     * Process button events and route to UI system
     * @param uiState UI state for event processing
     * @param sequencers Array of sequencer pointers
     * @param sequencerCount Number of sequencers
     * @param midiNoteManager MIDI note manager for output
     */
    void processEvents(UIState& uiState,
                      Sequencer* const* sequencers,
                      size_t sequencerCount,
                      MidiNoteManager& midiNoteManager);
    
    // Status and diagnostics
    
    /**
     * Get current state of a specific button
     * @param buttonIndex Button index (0-3)
     * @return const reference to button state
     */
    const ButtonState& getButtonState(uint8_t buttonIndex) const;
    
    /**
     * Check if any button is currently pressed
     * @return true if at least one button is pressed
     */
    bool isAnyButtonPressed() const;
    
    /**
     * Get number of buttons currently pressed
     * @return count of pressed buttons
     */
    uint8_t getPressedButtonCount() const;
    
    /**
     * Get list of currently pressed button indices
     * @param pressedButtons Array to fill with pressed button indices
     * @param maxButtons Maximum number of buttons to return
     * @return number of pressed buttons found
     */
    uint8_t getPressedButtons(uint8_t* pressedButtons, uint8_t maxButtons) const;
    
    // Button mapping
    
    /**
     * Map fallback button index to touch matrix button index
     * @param fallbackButton Fallback button index (0-3)
     * @return Touch matrix button index
     */
    uint8_t mapToMatrixButton(uint8_t fallbackButton) const;
    
    /**
     * Get description for a fallback button
     * @param fallbackButton Fallback button index (0-3)
     * @return Human-readable description
     */
    const char* getButtonDescription(uint8_t fallbackButton) const;
    
    // Testing and diagnostics
    
    /**
     * Test all button connections
     * @return true if all buttons respond correctly
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
     * Initialize GPIO pins for all buttons
     */
    void initializePins();
    
    /**
     * Read raw button state from GPIO
     * @param buttonIndex Button index (0-3)
     * @return true if button is pressed (after logic inversion)
     */
    bool readButtonRaw(uint8_t buttonIndex) const;
    
    /**
     * Update state for a single button with debouncing
     * @param buttonIndex Button index (0-3)
     * @return true if button state changed
     */
    bool updateButtonState(uint8_t buttonIndex);
    
    /**
     * Generate matrix button event for button state change
     * @param buttonIndex Button index (0-3)
     * @param isPressed true for press, false for release
     */
    void generateMatrixEvent(uint8_t buttonIndex, bool isPressed);
    
    /**
     * Validate button index is within range
     * @param buttonIndex Button index to validate
     * @return true if valid (0-3)
     */
    bool isValidButtonIndex(uint8_t buttonIndex) const;
    
    /**
     * Reset all button states to default
     */
    void resetButtonStates();
};