#include "DistanceSensorFallback.h"
#include "../sensors/as5600.h"
#include "../ui/UIState.h"
#include "../sequencer/Sequencer.h"
#include <Arduino.h>

// Initialize the distance sensor fallback system
bool DistanceSensorFallback::initialize(AS5600Sensor& encoderRef, std::function<void(float)> callback) {
    if (isInitialized) {
        return true;  // Already initialized
    }
    
    encoder = &encoderRef;
    distanceCallback = callback;
    
    // Reset states
    resetButtonState();
    resetEncoderState();
    
    // Set initial simulated distance to center of range
    resetSimulatedDistance();
    
    lastUpdateTime = millis();
    isInitialized = true;
    
    return true;
}

// Configure fallback settings
void DistanceSensorFallback::configure(const Config& newConfig) {
    bool wasActive = isActive;
    
    // Deactivate if currently active to reconfigure
    if (wasActive) {
        deactivate();
    }
    
    config = newConfig;
    
    // Constrain simulated distance to new range
    encoderState.simulatedDistance = constrainDistance(encoderState.simulatedDistance);
    
    // Reactivate if it was previously active
    if (wasActive) {
        activate();
    }
}

// Activate the fallback system
bool DistanceSensorFallback::activate() {
    if (!isInitialized || !encoder) {
        return false;
    }
    
    if (isActive) {
        return true;  // Already active
    }
    
    // Check if encoder is available
    if (!encoder->isConnected()) {
        return false;  // Cannot activate without encoder
    }
    
    // Initialize GPIO pin
    initializePin();
    
    // Reset states
    resetButtonState();
    resetEncoderState();
    
    // Initialize encoder state with current position
    encoderState.currentPosition = encoder->getRawAngle();
    encoderState.lastPosition = encoderState.currentPosition;
    encoderState.hasValidReading = true;
    
    isActive = true;
    lastUpdateTime = millis();
    
    return true;
}

// Deactivate the fallback system
bool DistanceSensorFallback::deactivate() {
    if (!isActive) {
        return true;  // Already inactive
    }
    
    // Reset states
    resetButtonState();
    resetEncoderState();
    
    isActive = false;
    
    return true;
}

// Update button and encoder states
void DistanceSensorFallback::update() {
    if (!isActive || !isInitialized || !encoder) {
        return;
    }
    
    uint32_t currentTime = millis();
    
    // Update button state
    bool buttonChanged = updateButtonState();
    
    // Update encoder state
    bool encoderChanged = updateEncoderState();
    
    // If button is being held and encoder moved, update simulated distance
    if (buttonState.isHolding && encoderChanged && isSignificantEncoderDelta(encoderState.deltaPosition)) {
        updateSimulatedDistance(encoderState.deltaPosition);
        
        // Call distance callback if provided
        if (distanceCallback) {
            distanceCallback(encoderState.simulatedDistance);
        }
    }
    
    lastUpdateTime = currentTime;
}

// Process parameter updates when button is held and encoder moves
void DistanceSensorFallback::processParameterUpdates(UIState& uiState,
                                                     Sequencer* const* sequencers,
                                                     size_t sequencerCount) {
    if (!isActive || !buttonState.isHolding) {
        return;
    }
    
    // This method would integrate with the parameter system
    // For now, the distance value is available via getSimulatedDistance()
    // The actual parameter update logic would depend on the UI state
    // and which parameter is currently selected for modification
    
    // Example integration (would need to be adapted to actual parameter system):
    // if (uiState.currentParameterMode != ParamMode::NONE) {
    //     float normalizedDistance = getNormalizedDistance();
    //     // Apply distance to current parameter based on UI state
    // }
}

// Set simulated distance value directly
void DistanceSensorFallback::setSimulatedDistance(float distance) {
    encoderState.simulatedDistance = constrainDistance(distance);
}

// Reset simulated distance to center of range
void DistanceSensorFallback::resetSimulatedDistance() {
    encoderState.simulatedDistance = (config.minDistance + config.maxDistance) / 2.0f;
}

// Get distance as normalized value (0.0 to 1.0)
float DistanceSensorFallback::getNormalizedDistance() const {
    float range = config.maxDistance - config.minDistance;
    if (range <= 0.0f) {
        return 0.5f;  // Default to center if invalid range
    }
    
    return (encoderState.simulatedDistance - config.minDistance) / range;
}

// Test button and encoder connections
bool DistanceSensorFallback::testConnections() {
    if (!isActive) {
        return false;
    }
    
    // Test button by reading its current state
    bool buttonReading = readButtonRaw();
    (void)buttonReading;  // Suppress unused variable warning
    
    // Test encoder connection
    if (!encoder || !encoder->isConnected()) {
        return false;
    }
    
    // Try to read encoder position
    uint16_t encoderReading = encoder->getRawAngle();
    (void)encoderReading;  // Suppress unused variable warning
    
    return true;  // Basic tests passed
}

// Get diagnostic information as string
size_t DistanceSensorFallback::getDiagnostics(char* buffer, size_t bufferSize) const {
    if (!buffer || bufferSize == 0) {
        return 0;
    }
    
    size_t written = 0;
    
    // Write status information
    written += snprintf(buffer + written, bufferSize - written,
                       "DistanceSensorFallback: %s\n", isActive ? "Active" : "Inactive");
    
    if (written >= bufferSize) return written;
    
    // Write button state
    written += snprintf(buffer + written, bufferSize - written,
                       "Button (Pin %d): %s %s\n",
                       config.stepButtonPin,
                       buttonState.isPressed ? "Pressed" : "Released",
                       buttonState.isHolding ? "(Holding)" : "");
    
    if (written >= bufferSize) return written;
    
    // Write encoder state
    written += snprintf(buffer + written, bufferSize - written,
                       "Encoder: %s, Position: %d, Distance: %.1fmm\n",
                       encoderState.hasValidReading ? "Valid" : "Invalid",
                       encoderState.currentPosition,
                       encoderState.simulatedDistance);
    
    return written;
}

// Initialize GPIO pin for step button
void DistanceSensorFallback::initializePin() {
    if (config.useInternalPullup) {
        pinMode(config.stepButtonPin, INPUT_PULLUP);
    } else {
        pinMode(config.stepButtonPin, INPUT);
    }
}

// Read raw button state from GPIO
bool DistanceSensorFallback::readButtonRaw() const {
    bool reading = digitalRead(config.stepButtonPin) == HIGH;
    
    // Apply logic inversion if configured
    if (config.invertLogic) {
        reading = !reading;
    }
    
    return reading;
}

// Update button state with debouncing and hold detection
bool DistanceSensorFallback::updateButtonState() {
    bool currentReading = readButtonRaw();
    uint32_t currentTime = millis();
    
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
                buttonState.holdStartTime = currentTime;
                buttonState.isHolding = false;
            } else {
                // Button was just released
                buttonState.isHolding = false;
            }
            
            return true;  // State changed
        }
    }
    
    // Check for hold condition
    if (buttonState.isPressed && !buttonState.isHolding) {
        if ((currentTime - buttonState.holdStartTime) >= config.holdTimeMs) {
            buttonState.isHolding = true;
            // Reset encoder position when hold starts to prevent jumps
            if (encoder) {
                encoderState.lastPosition = encoder->getRawAngle();
                encoderState.currentPosition = encoderState.lastPosition;
                encoderState.deltaPosition = 0;
            }
        }
    }
    
    return false;  // No state change
}

// Update encoder state and calculate position changes
bool DistanceSensorFallback::updateEncoderState() {
    if (!encoder) {
        encoderState.hasValidReading = false;
        return false;
    }
    
    // Check encoder connection
    if (!encoder->isConnected()) {
        encoderState.hasValidReading = false;
        return false;
    }
    
    // Read current encoder position
    uint16_t newPosition = encoder->getRawAngle();
    encoderState.currentPosition = newPosition;
    
    // Calculate delta position (handle wraparound)
    int16_t delta = encoderState.currentPosition - encoderState.lastPosition;
    
    // Handle encoder wraparound (12-bit encoder: 0-4095)
    if (delta > 2048) {
        delta -= 4096;  // Wrapped backwards
    } else if (delta < -2048) {
        delta += 4096;  // Wrapped forwards
    }
    
    encoderState.deltaPosition = delta;
    encoderState.lastPosition = encoderState.currentPosition;
    encoderState.hasValidReading = true;
    encoderState.lastUpdateTime = millis();
    
    return (delta != 0);  // Return true if position changed
}

// Update simulated distance based on encoder movement
void DistanceSensorFallback::updateSimulatedDistance(int16_t deltaPosition) {
    float distanceDelta = encoderDeltaToDistanceDelta(deltaPosition);
    float newDistance = encoderState.simulatedDistance + distanceDelta;
    
    encoderState.simulatedDistance = constrainDistance(newDistance);
}

// Constrain distance value to configured range
float DistanceSensorFallback::constrainDistance(float distance) const {
    if (distance < config.minDistance) {
        return config.minDistance;
    } else if (distance > config.maxDistance) {
        return config.maxDistance;
    }
    return distance;
}

// Convert encoder delta to distance delta
float DistanceSensorFallback::encoderDeltaToDistanceDelta(int16_t encoderDelta) const {
    // Convert encoder steps to distance change
    // Full encoder range (4096 steps) maps to full distance range
    float encoderRange = 4096.0f;
    float distanceRange = config.maxDistance - config.minDistance;
    
    float distanceDelta = (static_cast<float>(encoderDelta) / encoderRange) * distanceRange;
    
    // Apply sensitivity multiplier
    distanceDelta *= config.sensitivity;
    
    return distanceDelta;
}

// Reset button state to default
void DistanceSensorFallback::resetButtonState() {
    buttonState = ButtonState{};
}

// Reset encoder state to default
void DistanceSensorFallback::resetEncoderState() {
    encoderState = EncoderState{};
    resetSimulatedDistance();
}

// Check if encoder delta is significant enough to process
bool DistanceSensorFallback::isSignificantEncoderDelta(int16_t delta) const {
    return abs(delta) >= config.encoderDeadzone;
}