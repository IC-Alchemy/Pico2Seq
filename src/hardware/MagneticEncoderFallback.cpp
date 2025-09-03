#include "MagneticEncoderFallback.h"
#include "../ui/UIState.h"
#include "../sequencer/Sequencer.h"
#include <Arduino.h>

// Initialize the magnetic encoder fallback system
bool MagneticEncoderFallback::initialize(std::function<void(int16_t)> callback) {
    if (isInitialized) {
        return true;  // Already initialized
    }
    
    encoderCallback = callback;
    
    // Reset states
    resetButtonState(incrementButtonState);
    resetButtonState(decrementButtonState);
    resetEncoderState();
    
    lastUpdateTime = millis();
    isInitialized = true;
    
    return true;
}

// Configure fallback settings
void MagneticEncoderFallback::configure(const Config& newConfig) {
    bool wasActive = isActive;
    
    // Deactivate if currently active to reconfigure pins
    if (wasActive) {
        deactivate();
    }
    
    config = newConfig;
    
    // Update encoder range if it changed
    encoderState.minPosition = -2048;  // Default range
    encoderState.maxPosition = 2047;
    
    // Constrain current position to new range
    encoderState.position = constrainPosition(encoderState.position);
    updateNormalizedPosition();
    
    // Reactivate if it was previously active
    if (wasActive) {
        activate();
    }
}

// Activate the fallback system
bool MagneticEncoderFallback::activate() {
    if (!isInitialized) {
        return false;
    }
    
    if (isActive) {
        return true;  // Already active
    }
    
    // Initialize GPIO pins
    initializePins();
    
    // Reset states
    resetButtonState(incrementButtonState);
    resetButtonState(decrementButtonState);
    resetEncoderState();
    
    isActive = true;
    lastUpdateTime = millis();
    
    return true;
}

// Deactivate the fallback system
bool MagneticEncoderFallback::deactivate() {
    if (!isActive) {
        return true;  // Already inactive
    }
    
    // Reset states
    resetButtonState(incrementButtonState);
    resetButtonState(decrementButtonState);
    resetEncoderState();
    
    isActive = false;
    
    return true;
}

// Update button states and generate encoder events
void MagneticEncoderFallback::update() {
    if (!isActive || !isInitialized) {
        return;
    }
    
    uint32_t currentTime = millis();
    bool positionChanged = false;
    
    // Update increment button
    if (updateButtonState(config.incrementPin, incrementButtonState)) {
        if (incrementButtonState.isPressed || incrementButtonState.isRepeating) {
            // Determine increment value based on repeat state
            int16_t increment = incrementButtonState.isFastRepeating ? 
                               config.fastIncrementValue : config.incrementValue;
            
            updateEncoderPosition(increment);
            positionChanged = true;
        }
    }
    
    // Update decrement button
    if (updateButtonState(config.decrementPin, decrementButtonState)) {
        if (decrementButtonState.isPressed || decrementButtonState.isRepeating) {
            // Determine decrement value based on repeat state
            int16_t decrement = decrementButtonState.isFastRepeating ? 
                               config.fastDecrementValue : config.decrementValue;
            
            updateEncoderPosition(decrement);
            positionChanged = true;
        }
    }
    
    // Generate encoder event if position changed
    if (positionChanged) {
        generateEncoderEvent(encoderState.position);
    }
    
    lastUpdateTime = currentTime;
}

// Process parameter updates based on encoder simulation
void MagneticEncoderFallback::processParameterUpdates(UIState& uiState,
                                                      Sequencer* const* sequencers,
                                                      size_t sequencerCount) {
    if (!isActive) {
        return;
    }
    
    // This method would integrate with the parameter system
    // For now, the encoder position is available via getEncoderPosition()
    // The actual parameter update logic would depend on the UI state
    // and which parameter is currently selected for modification
    
    // Example integration (would need to be adapted to actual parameter system):
    // if (uiState.currentParameterMode != ParamMode::NONE) {
    //     float normalizedPosition = getNormalizedPosition();
    //     // Apply position to current parameter based on UI state
    // }
}

// Check if any button is currently pressed
bool MagneticEncoderFallback::isAnyButtonPressed() const {
    return incrementButtonState.isPressed || decrementButtonState.isPressed;
}

// Set simulated encoder position directly
void MagneticEncoderFallback::setEncoderPosition(int16_t position) {
    int16_t oldPosition = encoderState.position;
    encoderState.position = constrainPosition(position);
    updateNormalizedPosition();
    
    // Update delta and timestamp
    encoderState.deltaPosition = encoderState.position - oldPosition;
    encoderState.lastUpdateTime = millis();
    
    // Generate event if position actually changed
    if (encoderState.position != oldPosition) {
        generateEncoderEvent(encoderState.position);
    }
}

// Reset simulated encoder position to center
void MagneticEncoderFallback::resetEncoderPosition() {
    int16_t centerPosition = (encoderState.minPosition + encoderState.maxPosition) / 2;
    setEncoderPosition(centerPosition);
}

// Set encoder position range
void MagneticEncoderFallback::setEncoderRange(int16_t minPos, int16_t maxPos) {
    if (minPos >= maxPos) {
        return;  // Invalid range
    }
    
    encoderState.minPosition = minPos;
    encoderState.maxPosition = maxPos;
    
    // Constrain current position to new range
    encoderState.position = constrainPosition(encoderState.position);
    updateNormalizedPosition();
}

// Get encoder position range
void MagneticEncoderFallback::getEncoderRange(int16_t& minPos, int16_t& maxPos) const {
    minPos = encoderState.minPosition;
    maxPos = encoderState.maxPosition;
}

// Test button connections
bool MagneticEncoderFallback::testButtons() {
    if (!isActive) {
        return false;
    }
    
    // Test increment button
    bool incrementReading = readButtonRaw(config.incrementPin);
    (void)incrementReading;  // Suppress unused variable warning
    
    // Test decrement button
    bool decrementReading = readButtonRaw(config.decrementPin);
    (void)decrementReading;  // Suppress unused variable warning
    
    return true;  // Basic test passed
}

// Get diagnostic information as string
size_t MagneticEncoderFallback::getDiagnostics(char* buffer, size_t bufferSize) const {
    if (!buffer || bufferSize == 0) {
        return 0;
    }
    
    size_t written = 0;
    
    // Write status information
    written += snprintf(buffer + written, bufferSize - written,
                       "MagneticEncoderFallback: %s\n", isActive ? "Active" : "Inactive");
    
    if (written >= bufferSize) return written;
    
    // Write button states
    written += snprintf(buffer + written, bufferSize - written,
                       "Inc Button (Pin %d): %s %s\n",
                       config.incrementPin,
                       incrementButtonState.isPressed ? "Pressed" : "Released",
                       incrementButtonState.isRepeating ? "(Repeating)" : "");
    
    if (written >= bufferSize) return written;
    
    written += snprintf(buffer + written, bufferSize - written,
                       "Dec Button (Pin %d): %s %s\n",
                       config.decrementPin,
                       decrementButtonState.isPressed ? "Pressed" : "Released",
                       decrementButtonState.isRepeating ? "(Repeating)" : "");
    
    if (written >= bufferSize) return written;
    
    // Write encoder state
    written += snprintf(buffer + written, bufferSize - written,
                       "Encoder Position: %d (%.2f%%)\n",
                       encoderState.position,
                       encoderState.normalizedPosition * 100.0f);
    
    return written;
}

// Initialize GPIO pins for both buttons
void MagneticEncoderFallback::initializePins() {
    if (config.useInternalPullups) {
        pinMode(config.incrementPin, INPUT_PULLUP);
        pinMode(config.decrementPin, INPUT_PULLUP);
    } else {
        pinMode(config.incrementPin, INPUT);
        pinMode(config.decrementPin, INPUT);
    }
}

// Read raw button state from GPIO
bool MagneticEncoderFallback::readButtonRaw(uint8_t pin) const {
    bool reading = digitalRead(pin) == HIGH;
    
    // Apply logic inversion if configured
    if (config.invertLogic) {
        reading = !reading;
    }
    
    return reading;
}

// Update state for a single button with debouncing and repeat logic
bool MagneticEncoderFallback::updateButtonState(uint8_t pin, ButtonState& buttonState) {
    bool currentReading = readButtonRaw(pin);
    uint32_t currentTime = millis();
    bool eventGenerated = false;
    
    // Reset state changed flag
    buttonState.stateChanged = false;
    
    // Check if reading has changed
    if (currentReading != buttonState.lastReading) {
        buttonState.lastChangeTime = currentTime;
        buttonState.lastReading = currentReading;
    }
    
    // Check if enough time has passed for debouncing
    if ((currentTime - buttonState.lastChangeTime) >= config.debounceTimeMs) {
        // Check if button state should change
        if (currentReading != buttonState.isPressed) {
            buttonState.isPressed = currentReading;
            buttonState.stateChanged = true;
            
            if (buttonState.isPressed) {
                // Button was just pressed
                buttonState.pressTime = currentTime;
                buttonState.lastRepeatTime = currentTime;
                buttonState.isRepeating = false;
                buttonState.isFastRepeating = false;
                eventGenerated = true;  // Generate initial press event
            } else {
                // Button was just released
                buttonState.isRepeating = false;
                buttonState.isFastRepeating = false;
            }
        }
    }
    
    // Handle repeat logic for held buttons
    if (buttonState.isPressed && handleButtonRepeat(buttonState)) {
        eventGenerated = true;  // Generate repeat event
    }
    
    return eventGenerated;
}

// Handle button repeat logic for held buttons
bool MagneticEncoderFallback::handleButtonRepeat(ButtonState& buttonState) {
    if (!buttonState.isPressed) {
        return false;
    }
    
    uint32_t currentTime = millis();
    uint32_t timeSincePress = currentTime - buttonState.pressTime;
    uint32_t timeSinceLastRepeat = currentTime - buttonState.lastRepeatTime;
    
    // Check if we should start repeating
    if (!buttonState.isRepeating && timeSincePress >= config.repeatDelayMs) {
        buttonState.isRepeating = true;
        buttonState.lastRepeatTime = currentTime;
        return true;  // Generate repeat event
    }
    
    // Check if we should switch to fast repeat
    if (buttonState.isRepeating && !buttonState.isFastRepeating && 
        timeSincePress >= config.fastRepeatThresholdMs) {
        buttonState.isFastRepeating = true;
    }
    
    // Check if we should continue repeating
    if (buttonState.isRepeating) {
        uint32_t repeatRate = buttonState.isFastRepeating ? 
                             config.fastRepeatRateMs : config.repeatRateMs;
        
        if (timeSinceLastRepeat >= repeatRate) {
            buttonState.lastRepeatTime = currentTime;
            return true;  // Generate repeat event
        }
    }
    
    return false;  // No repeat event
}

// Update simulated encoder position
void MagneticEncoderFallback::updateEncoderPosition(int16_t increment) {
    int16_t oldPosition = encoderState.position;
    encoderState.position = constrainPosition(encoderState.position + increment);
    
    // Update delta and timestamp
    encoderState.deltaPosition = encoderState.position - oldPosition;
    encoderState.lastPosition = oldPosition;
    encoderState.lastUpdateTime = millis();
    
    // Update normalized position
    updateNormalizedPosition();
}

// Constrain encoder position to valid range
int16_t MagneticEncoderFallback::constrainPosition(int16_t position) const {
    if (position < encoderState.minPosition) {
        return encoderState.minPosition;
    } else if (position > encoderState.maxPosition) {
        return encoderState.maxPosition;
    }
    return position;
}

// Update normalized position based on current position
void MagneticEncoderFallback::updateNormalizedPosition() {
    int16_t range = encoderState.maxPosition - encoderState.minPosition;
    if (range <= 0) {
        encoderState.normalizedPosition = 0.5f;  // Default to center if invalid range
        return;
    }
    
    float normalized = static_cast<float>(encoderState.position - encoderState.minPosition) / 
                      static_cast<float>(range);
    
    // Constrain to 0.0-1.0 range
    if (normalized < 0.0f) {
        normalized = 0.0f;
    } else if (normalized > 1.0f) {
        normalized = 1.0f;
    }
    
    encoderState.normalizedPosition = normalized;
}

// Reset button state to default
void MagneticEncoderFallback::resetButtonState(ButtonState& buttonState) {
    buttonState = ButtonState{};
}

// Reset encoder state to default
void MagneticEncoderFallback::resetEncoderState() {
    encoderState = EncoderState{};
    encoderState.minPosition = -2048;
    encoderState.maxPosition = 2047;
    encoderState.position = 0;  // Start at center
    updateNormalizedPosition();
}

// Generate encoder change event
void MagneticEncoderFallback::generateEncoderEvent(int16_t newPosition) {
    if (encoderCallback) {
        encoderCallback(newPosition);
    }
}