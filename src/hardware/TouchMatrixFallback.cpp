#include "TouchMatrixFallback.h"
#include "../matrix/Matrix.h"
#include "../ui/UIEventHandler.h"
#include <Arduino.h>

// Default button mapping - maps fallback buttons to touch matrix positions
const TouchMatrixFallback::ButtonMapping TouchMatrixFallback::defaultMapping[4] = {
    {0, 0, "Step 1"},      // Fallback button 0 -> Matrix button 0
    {1, 1, "Step 2"},      // Fallback button 1 -> Matrix button 1  
    {2, 2, "Step 3"},      // Fallback button 2 -> Matrix button 2
    {3, 3, "Step 4"}       // Fallback button 3 -> Matrix button 3
};

// Initialize the touch matrix fallback system
bool TouchMatrixFallback::initialize(std::function<void(const MatrixButtonEvent&)> callback) {
    if (isInitialized) {
        return true;  // Already initialized
    }
    
    eventCallback = callback;
    
    // Reset all button states
    resetButtonStates();
    
    lastUpdateTime = millis();
    isInitialized = true;
    
    return true;
}

// Configure fallback button settings
void TouchMatrixFallback::configure(const Config& newConfig) {
    bool wasActive = isActive;
    
    // Deactivate if currently active to reconfigure pins
    if (wasActive) {
        deactivate();
    }
    
    config = newConfig;
    
    // Reactivate if it was previously active
    if (wasActive) {
        activate();
    }
}

// Activate the fallback system
bool TouchMatrixFallback::activate() {
    if (!isInitialized) {
        return false;
    }
    
    if (isActive) {
        return true;  // Already active
    }
    
    // Initialize GPIO pins
    initializePins();
    
    // Reset button states
    resetButtonStates();
    
    isActive = true;
    lastUpdateTime = millis();
    
    return true;
}

// Deactivate the fallback system
bool TouchMatrixFallback::deactivate() {
    if (!isActive) {
        return true;  // Already inactive
    }
    
    // Reset button states
    resetButtonStates();
    
    isActive = false;
    
    return true;
}

// Update button states and generate events
void TouchMatrixFallback::update() {
    if (!isActive || !isInitialized) {
        return;
    }
    
    uint32_t currentTime = millis();
    
    // Update each button state
    for (uint8_t i = 0; i < 4; i++) {
        if (updateButtonState(i)) {
            // Button state changed, generate matrix event
            generateMatrixEvent(i, buttonStates[i].isPressed);
        }
    }
    
    lastUpdateTime = currentTime;
}

// Process button events and route to UI system
void TouchMatrixFallback::processEvents(UIState& uiState,
                                       Sequencer* const* sequencers,
                                       size_t sequencerCount,
                                       MidiNoteManager& midiNoteManager) {
    // This method is called after update() has generated events
    // The actual event processing is handled by the callback function
    // which routes events to the UI system
    
    // Additional processing could be added here if needed
    // For example, special handling for button combinations
}

// Get current state of a specific button
const TouchMatrixFallback::ButtonState& TouchMatrixFallback::getButtonState(uint8_t buttonIndex) const {
    static const ButtonState invalidState{};
    
    if (!isValidButtonIndex(buttonIndex)) {
        return invalidState;
    }
    
    return buttonStates[buttonIndex];
}

// Check if any button is currently pressed
bool TouchMatrixFallback::isAnyButtonPressed() const {
    if (!isActive) {
        return false;
    }
    
    for (uint8_t i = 0; i < 4; i++) {
        if (buttonStates[i].isPressed) {
            return true;
        }
    }
    
    return false;
}

// Get number of buttons currently pressed
uint8_t TouchMatrixFallback::getPressedButtonCount() const {
    if (!isActive) {
        return 0;
    }
    
    uint8_t count = 0;
    for (uint8_t i = 0; i < 4; i++) {
        if (buttonStates[i].isPressed) {
            count++;
        }
    }
    
    return count;
}

// Get list of currently pressed button indices
uint8_t TouchMatrixFallback::getPressedButtons(uint8_t* pressedButtons, uint8_t maxButtons) const {
    if (!isActive || !pressedButtons || maxButtons == 0) {
        return 0;
    }
    
    uint8_t count = 0;
    for (uint8_t i = 0; i < 4 && count < maxButtons; i++) {
        if (buttonStates[i].isPressed) {
            pressedButtons[count] = i;
            count++;
        }
    }
    
    return count;
}

// Map fallback button index to touch matrix button index
uint8_t TouchMatrixFallback::mapToMatrixButton(uint8_t fallbackButton) const {
    if (!isValidButtonIndex(fallbackButton)) {
        return 0;  // Default to first button
    }
    
    return defaultMapping[fallbackButton].matrixButton;
}

// Get description for a fallback button
const char* TouchMatrixFallback::getButtonDescription(uint8_t fallbackButton) const {
    if (!isValidButtonIndex(fallbackButton)) {
        return "Invalid";
    }
    
    return defaultMapping[fallbackButton].description;
}

// Test all button connections
bool TouchMatrixFallback::testButtons() {
    if (!isActive) {
        return false;
    }
    
    // Test each button by reading its current state
    for (uint8_t i = 0; i < 4; i++) {
        // Try to read the button - if pin is not configured correctly,
        // this might return unexpected values
        bool reading = readButtonRaw(i);
        
        // For now, just verify we can read the pin
        // A more comprehensive test would require user interaction
        (void)reading;  // Suppress unused variable warning
    }
    
    return true;  // Basic test passed
}

// Get diagnostic information as string
size_t TouchMatrixFallback::getDiagnostics(char* buffer, size_t bufferSize) const {
    if (!buffer || bufferSize == 0) {
        return 0;
    }
    
    size_t written = 0;
    
    // Write status information
    written += snprintf(buffer + written, bufferSize - written,
                       "TouchMatrixFallback: %s\n", isActive ? "Active" : "Inactive");
    
    if (written >= bufferSize) return written;
    
    // Write button states
    for (uint8_t i = 0; i < 4; i++) {
        written += snprintf(buffer + written, bufferSize - written,
                           "Button %d (Pin %d): %s\n",
                           i, config.buttonPins[i],
                           buttonStates[i].isPressed ? "Pressed" : "Released");
        
        if (written >= bufferSize) break;
    }
    
    return written;
}

// Initialize GPIO pins for all buttons
void TouchMatrixFallback::initializePins() {
    for (uint8_t i = 0; i < 4; i++) {
        if (config.useInternalPullups) {
            pinMode(config.buttonPins[i], INPUT_PULLUP);
        } else {
            pinMode(config.buttonPins[i], INPUT);
        }
    }
}

// Read raw button state from GPIO
bool TouchMatrixFallback::readButtonRaw(uint8_t buttonIndex) const {
    if (!isValidButtonIndex(buttonIndex)) {
        return false;
    }
    
    bool reading = digitalRead(config.buttonPins[buttonIndex]) == HIGH;
    
    // Apply logic inversion if configured
    if (config.invertLogic) {
        reading = !reading;
    }
    
    return reading;
}

// Update state for a single button with debouncing
bool TouchMatrixFallback::updateButtonState(uint8_t buttonIndex) {
    if (!isValidButtonIndex(buttonIndex)) {
        return false;
    }
    
    ButtonState& state = buttonStates[buttonIndex];
    bool currentReading = readButtonRaw(buttonIndex);
    uint32_t currentTime = millis();
    
    // Reset state changed flag
    state.stateChanged = false;
    
    // Check if reading has changed
    if (currentReading != state.lastReading) {
        state.lastChangeTime = currentTime;
        state.lastReading = currentReading;
    }
    
    // Check if enough time has passed for debouncing
    if ((currentTime - state.lastChangeTime) >= config.debounceTimeMs) {
        // Check if button state should change
        if (currentReading != state.isPressed) {
            state.isPressed = currentReading;
            state.stateChanged = true;
            
            // Record press time for timing analysis
            if (state.isPressed) {
                state.pressTime = currentTime;
            }
            
            return true;  // State changed
        }
    }
    
    return false;  // No state change
}

// Generate matrix button event for button state change
void TouchMatrixFallback::generateMatrixEvent(uint8_t buttonIndex, bool isPressed) {
    if (!isValidButtonIndex(buttonIndex) || !eventCallback) {
        return;
    }
    
    // Create matrix button event
    MatrixButtonEvent event;
    event.buttonIndex = mapToMatrixButton(buttonIndex);
    event.type = isPressed ? MATRIX_BUTTON_PRESSED : MATRIX_BUTTON_RELEASED;
    
    // Call the event callback to route the event
    eventCallback(event);
}

// Validate button index is within range
bool TouchMatrixFallback::isValidButtonIndex(uint8_t buttonIndex) const {
    return buttonIndex < 4;
}

// Reset all button states to default
void TouchMatrixFallback::resetButtonStates() {
    for (uint8_t i = 0; i < 4; i++) {
        buttonStates[i] = ButtonState{};
    }
}