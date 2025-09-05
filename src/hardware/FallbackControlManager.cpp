#include "FallbackControlManager.h"
#include "../ui/UIEventHandler.h"
#include "../matrix/Matrix.h"
#include "../sequencer/Sequencer.h"
#include "../midi/MidiManager.h"
#include <Arduino.h>

// Initialize the fallback control manager
bool FallbackControlManager::initialize(HardwareRegistry& registry, 
                                       std::function<void(const FallbackEvent&)> callback) {
    if (isInitialized) {
        return true;  // Already initialized
    }
    
    hardwareRegistry = &registry;
    eventCallback = callback;
    
    // Initialize all button states
    for (auto& state : touchMatrixButtonStates) {
        state = ButtonState{};
    }
    stepButtonState = ButtonState{};
    incrementButtonState = ButtonState{};
    decrementButtonState = ButtonState{};
    
    lastUpdateTime = millis();
    isInitialized = true;
    
    return true;
}

// Configure touch matrix fallback settings
void FallbackControlManager::configureTouchMatrixFallback(const TouchMatrixFallbackConfig& config) {
    touchMatrixConfig = config;
    
    if (touchMatrixConfig.isActive) {
        initializeTouchMatrixPins();
    }
}

// Configure distance sensor fallback settings
void FallbackControlManager::configureDistanceSensorFallback(const DistanceSensorFallbackConfig& config) {
    distanceSensorConfig = config;
    
    if (distanceSensorConfig.isActive) {
        initializeDistanceSensorPins();
    }
}

// Configure magnetic encoder fallback settings
void FallbackControlManager::configureMagneticEncoderFallback(const MagneticEncoderFallbackConfig& config) {
    magneticEncoderConfig = config;
    
    if (magneticEncoderConfig.isActive) {
        initializeMagneticEncoderPins();
    }
}

// Activate fallback controls for a specific module
bool FallbackControlManager::activateFallback(ModuleType moduleType) {
    if (!isInitialized || !hardwareRegistry) {
        return false;
    }
    
    switch (moduleType) {
        case ModuleType::TOUCH_MATRIX:
            if (!touchMatrixConfig.isActive) {
                touchMatrixConfig.isActive = true;
                initializeTouchMatrixPins();
                hardwareRegistry->setFallbackActive(moduleType, true);
                return true;
            }
            break;
            
        case ModuleType::DISTANCE_SENSOR:
            if (!distanceSensorConfig.isActive) {
                // Check if AS5600 is available for distance fallback
                if (distanceSensorConfig.requiresAS5600 && 
                    !hardwareRegistry->isModuleHealthy(ModuleType::MAGNETIC_ENCODER)) {
                    return false;  // Cannot activate without encoder
                }
                distanceSensorConfig.isActive = true;
                initializeDistanceSensorPins();
                hardwareRegistry->setFallbackActive(moduleType, true);
                return true;
            }
            break;
            
        case ModuleType::MAGNETIC_ENCODER:
            if (!magneticEncoderConfig.isActive) {
                magneticEncoderConfig.isActive = true;
                initializeMagneticEncoderPins();
                hardwareRegistry->setFallbackActive(moduleType, true);
                return true;
            }
            break;
            
        default:
            return false;  // Unsupported module type
    }
    
    return false;  // Already active or failed
}

// Deactivate fallback controls for a specific module
bool FallbackControlManager::deactivateFallback(ModuleType moduleType) {
    if (!isInitialized || !hardwareRegistry) {
        return false;
    }
    
    switch (moduleType) {
        case ModuleType::TOUCH_MATRIX:
            if (touchMatrixConfig.isActive) {
                touchMatrixConfig.isActive = false;
                // Reset button states
                for (auto& state : touchMatrixButtonStates) {
                    state = ButtonState{};
                }
                hardwareRegistry->setFallbackActive(moduleType, false);
                return true;
            }
            break;
            
        case ModuleType::DISTANCE_SENSOR:
            if (distanceSensorConfig.isActive) {
                distanceSensorConfig.isActive = false;
                distanceSensorConfig.currentStepButton = 0;
                stepButtonState = ButtonState{};
                hardwareRegistry->setFallbackActive(moduleType, false);
                return true;
            }
            break;
            
        case ModuleType::MAGNETIC_ENCODER:
            if (magneticEncoderConfig.isActive) {
                magneticEncoderConfig.isActive = false;
                incrementButtonState = ButtonState{};
                decrementButtonState = ButtonState{};
                hardwareRegistry->setFallbackActive(moduleType, false);
                return true;
            }
            break;
            
        default:
            return false;  // Unsupported module type
    }
    
    return false;  // Already inactive or failed
}

// Check if fallback is active for a specific module
bool FallbackControlManager::isFallbackActive(ModuleType moduleType) const {
    switch (moduleType) {
        case ModuleType::TOUCH_MATRIX:
            return touchMatrixConfig.isActive;
        case ModuleType::DISTANCE_SENSOR:
            return distanceSensorConfig.isActive;
        case ModuleType::MAGNETIC_ENCODER:
            return magneticEncoderConfig.isActive;
        default:
            return false;
    }
}

// Activate all available fallback controls
void FallbackControlManager::activateAllFallbacks() {
    activateFallback(ModuleType::TOUCH_MATRIX);
    activateFallback(ModuleType::DISTANCE_SENSOR);
    activateFallback(ModuleType::MAGNETIC_ENCODER);
}

// Deactivate all fallback controls
void FallbackControlManager::deactivateAllFallbacks() {
    deactivateFallback(ModuleType::TOUCH_MATRIX);
    deactivateFallback(ModuleType::DISTANCE_SENSOR);
    deactivateFallback(ModuleType::MAGNETIC_ENCODER);
}

// Process hardware input events and route through fallback if needed
void FallbackControlManager::routeMatrixEvent(const MatrixButtonEvent& evt,
                                             UIState& uiState,
                                             Sequencer* const* sequencers,
                                             size_t sequencerCount,
                                             MidiNoteManager& midiNoteManager) {
    // If touch matrix hardware is available, use normal event handler
    if (hardwareRegistry && hardwareRegistry->isModuleHealthy(ModuleType::TOUCH_MATRIX)) {
        matrixEventHandler(evt, uiState, sequencers, sequencerCount, midiNoteManager);
    } else if (touchMatrixConfig.isActive) {
        // Route through fallback - events are generated by update() method
        // This method handles events that come from either hardware or fallback
        matrixEventHandler(evt, uiState, sequencers, sequencerCount, midiNoteManager);
    }
}

// Process distance sensor input and route through fallback if needed
void FallbackControlManager::routeDistanceInput(float distance,
                                               UIState& uiState,
                                               Sequencer* const* sequencers,
                                               size_t sequencerCount) {
    // If distance sensor hardware is available, use the value directly
    if (hardwareRegistry && hardwareRegistry->isModuleHealthy(ModuleType::DISTANCE_SENSOR)) {
        // Update UI state with distance value
        // This would normally be handled by the distance sensor module
        // For now, we'll store it in a way that can be used by parameter updates
        // The actual parameter update logic would be implemented in the sensor modules
    } else if (distanceSensorConfig.isActive && distanceSensorConfig.currentStepButton > 0) {
        // Use fallback distance value when step button is held
        // The distance value comes from encoder input processed elsewhere
        // This method receives the simulated distance value
    }
}

// Process encoder input and route through fallback if needed
void FallbackControlManager::routeEncoderInput(int16_t encoderValue,
                                              UIState& uiState,
                                              Sequencer* const* sequencers,
                                              size_t sequencerCount) {
    // If magnetic encoder hardware is available, use the value directly
    if (hardwareRegistry && hardwareRegistry->isModuleHealthy(ModuleType::MAGNETIC_ENCODER)) {
        // Process encoder value normally
        // This would be handled by the magnetic encoder module
    } else if (magneticEncoderConfig.isActive) {
        // Use fallback encoder simulation from increment/decrement buttons
        // The encoder events are generated by the update() method
    }
}

// Update fallback control states (call from main loop)
void FallbackControlManager::update() {
    if (!isInitialized) {
        return;
    }
    
    uint32_t currentTime = millis();
    
    // Update each active fallback type
    if (touchMatrixConfig.isActive) {
        updateTouchMatrixFallback();
    }
    
    if (distanceSensorConfig.isActive) {
        updateDistanceSensorFallback();
    }
    
    if (magneticEncoderConfig.isActive) {
        updateMagneticEncoderFallback();
    }
    
    lastUpdateTime = currentTime;
}

// Handle fallback event generated internally
void FallbackControlManager::handleFallbackEvent(const FallbackEvent& event) {
    if (eventCallback) {
        eventCallback(event);
    }
}

// Check if any fallback controls are currently active
bool FallbackControlManager::hasActiveFallbacks() const {
    return touchMatrixConfig.isActive || 
           distanceSensorConfig.isActive || 
           magneticEncoderConfig.isActive;
}

// Get list of modules currently using fallback controls
std::vector<ModuleType> FallbackControlManager::getActiveFallbackModules() const {
    std::vector<ModuleType> activeModules;
    
    if (touchMatrixConfig.isActive) {
        activeModules.push_back(ModuleType::TOUCH_MATRIX);
    }
    if (distanceSensorConfig.isActive) {
        activeModules.push_back(ModuleType::DISTANCE_SENSOR);
    }
    if (magneticEncoderConfig.isActive) {
        activeModules.push_back(ModuleType::MAGNETIC_ENCODER);
    }
    
    return activeModules;
}

// Initialize GPIO pins for touch matrix fallback
void FallbackControlManager::initializeTouchMatrixPins() {
    for (int i = 0; i < 4; i++) {
        pinMode(touchMatrixConfig.buttonPins[i], touchMatrixConfig.useInternalPullups ? INPUT_PULLUP : INPUT);
    }
}

// Initialize GPIO pins for distance sensor fallback
void FallbackControlManager::initializeDistanceSensorPins() {
    pinMode(distanceSensorConfig.stepButtonPin, INPUT_PULLUP);
}

// Initialize GPIO pins for magnetic encoder fallback
void FallbackControlManager::initializeMagneticEncoderPins() {
    pinMode(magneticEncoderConfig.incrementPin, 
            magneticEncoderConfig.useInternalPullups ? INPUT_PULLUP : INPUT);
    pinMode(magneticEncoderConfig.decrementPin, 
            magneticEncoderConfig.useInternalPullups ? INPUT_PULLUP : INPUT);
}

// Update touch matrix fallback button states
void FallbackControlManager::updateTouchMatrixFallback() {
    for (int i = 0; i < 4; i++) {
        if (updateButtonState(touchMatrixConfig.buttonPins[i], touchMatrixButtonStates[i])) {
            // Button state changed, generate matrix event
            FallbackEvent event;
            event.type = touchMatrixButtonStates[i].isPressed ? 
                        FallbackEventType::TOUCH_MATRIX_PRESS : 
                        FallbackEventType::TOUCH_MATRIX_RELEASE;
            event.sourceModule = ModuleType::TOUCH_MATRIX;
            event.buttonIndex = mapFallbackToTouchMatrix(i);
            event.value = 0.0f;
            event.timestamp = millis();
            
            generateFallbackEvent(event);
        }
    }
}

// Update distance sensor fallback button states
void FallbackControlManager::updateDistanceSensorFallback() {
    if (updateButtonState(distanceSensorConfig.stepButtonPin, stepButtonState)) {
        FallbackEvent event;
        event.type = stepButtonState.isPressed ? 
                    FallbackEventType::STEP_BUTTON_PRESS : 
                    FallbackEventType::STEP_BUTTON_RELEASE;
        event.sourceModule = ModuleType::DISTANCE_SENSOR;
        event.buttonIndex = 0;  // Single step button
        event.value = 0.0f;
        event.timestamp = millis();
        
        // Track which step button is currently held
        if (stepButtonState.isPressed) {
            distanceSensorConfig.currentStepButton = 1;
        } else {
            distanceSensorConfig.currentStepButton = 0;
        }
        
        generateFallbackEvent(event);
    }
}

// Update magnetic encoder fallback button states
void FallbackControlManager::updateMagneticEncoderFallback() {
    // Check increment button
    if (updateButtonState(magneticEncoderConfig.incrementPin, incrementButtonState)) {
        if (incrementButtonState.isPressed) {
            FallbackEvent event;
            event.type = FallbackEventType::ENCODER_INCREMENT;
            event.sourceModule = ModuleType::MAGNETIC_ENCODER;
            event.buttonIndex = 0;
            event.value = 1.0f;
            event.timestamp = millis();
            
            generateFallbackEvent(event);
        }
    }
    
    // Check decrement button
    if (updateButtonState(magneticEncoderConfig.decrementPin, decrementButtonState)) {
        if (decrementButtonState.isPressed) {
            FallbackEvent event;
            event.type = FallbackEventType::ENCODER_DECREMENT;
            event.sourceModule = ModuleType::MAGNETIC_ENCODER;
            event.buttonIndex = 0;
            event.value = -1.0f;
            event.timestamp = millis();
            
            generateFallbackEvent(event);
        }
    }
    
    // Handle button repeat for held buttons
    if (incrementButtonState.isPressed && handleButtonRepeat(incrementButtonState)) {
        FallbackEvent event;
        event.type = FallbackEventType::ENCODER_INCREMENT;
        event.sourceModule = ModuleType::MAGNETIC_ENCODER;
        event.buttonIndex = 0;
        event.value = 1.0f;
        event.timestamp = millis();
        
        generateFallbackEvent(event);
    }
    
    if (decrementButtonState.isPressed && handleButtonRepeat(decrementButtonState)) {
        FallbackEvent event;
        event.type = FallbackEventType::ENCODER_DECREMENT;
        event.sourceModule = ModuleType::MAGNETIC_ENCODER;
        event.buttonIndex = 0;
        event.value = -1.0f;
        event.timestamp = millis();
        
        generateFallbackEvent(event);
    }
}

// Generate fallback event and call callback
void FallbackControlManager::generateFallbackEvent(const FallbackEvent& event) {
    if (eventCallback) {
        eventCallback(event);
    }
}

// Map fallback button index to touch matrix button index
uint8_t FallbackControlManager::mapFallbackToTouchMatrix(uint8_t fallbackButton) const {
    // Simple 1:1 mapping for now - could be made configurable
    // Maps fallback buttons 0-3 to touch matrix buttons 0-3
    return fallbackButton < 4 ? fallbackButton : 0;
}

// Check if button state has changed and handle debouncing
bool FallbackControlManager::updateButtonState(uint8_t pin, ButtonState& buttonState) {
    bool currentReading = digitalRead(pin) == LOW;  // Assuming active-low with pullup
    uint32_t currentTime = millis();
    
    // Check if button state has changed
    if (currentReading != buttonState.isPressed) {
        // Check if enough time has passed for debouncing
        if (currentTime - buttonState.pressTime >= touchMatrixConfig.debounceTimeMs) {
            buttonState.isPressed = currentReading;
            buttonState.pressTime = currentTime;
            buttonState.lastRepeatTime = currentTime;
            buttonState.isRepeating = false;
            return true;  // State changed
        }
    }
    
    return false;  // No state change
}

// Handle button repeat logic for held buttons
bool FallbackControlManager::handleButtonRepeat(ButtonState& buttonState) {
    if (!buttonState.isPressed) {
        return false;
    }
    
    uint32_t currentTime = millis();
    uint32_t timeSincePress = currentTime - buttonState.pressTime;
    uint32_t timeSinceLastRepeat = currentTime - buttonState.lastRepeatTime;
    
    // Check if we should start repeating
    if (!buttonState.isRepeating && timeSincePress >= magneticEncoderConfig.repeatDelayMs) {
        buttonState.isRepeating = true;
        buttonState.lastRepeatTime = currentTime;
        return true;  // Generate repeat event
    }
    
    // Check if we should continue repeating
    if (buttonState.isRepeating && timeSinceLastRepeat >= magneticEncoderConfig.repeatRateMs) {
        buttonState.lastRepeatTime = currentTime;
        return true;  // Generate repeat event
    }
    
    return false;  // No repeat event
}