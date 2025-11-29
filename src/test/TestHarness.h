// TestHarness.h - Simple test harness for voice selection and preset assignment
// Run this in Arduino IDE to simulate button events and verify behavior.
// Copy-paste output to validate expected results.

#include <Arduino.h>

// Mock UIState struct for testing
struct UIState {
    uint8_t selectedVoiceIndex = 0;
    bool voiceSelected = false;
    uint8_t voicePresetIndices[4] = {4, 2, 1, 6};
    // Add other fields as needed
};

// Mock VoiceSystem and VoiceManager for testing
struct VoiceSystem {
    static constexpr uint8_t MAX_VOICES = 4;
    uint8_t voiceIds[MAX_VOICES] = {1, 2, 3, 4}; // Mock valid voice IDs
    uint8_t getVoiceId(uint8_t index) { return (index < MAX_VOICES) ? voiceIds[index] : 0; }
};

struct VoiceManager {
    bool setVoiceConfig(uint8_t voiceId, void* config) { 
        Serial.print("VoiceManager::setVoiceConfig called with voiceId=");
        Serial.println(voiceId);
        return true;
    }
};

// Mock VoicePresets namespace
namespace VoicePresets {
    void applyPresetToVoice(uint8_t uiIndex, uint8_t presetIndex) {
        Serial.print("applyPresetToVoice called: uiIndex=");
        Serial.print(uiIndex);
        Serial.print(", presetIndex=");
        Serial.println(presetIndex);
        // Mock the call to VoiceSystem & VoiceManager
        VoiceSystem voiceSystem;
        VoiceManager voiceManager;
        uint8_t voiceId = voiceSystem.getVoiceId(uiIndex);
        if (voiceId != 0) {
            voiceManager.setVoiceConfig(voiceId, nullptr); // Mock config
        }
    }
}

// Mock event handling functions
void handleSettingsButtonPress(UIState& uiState, uint8_t buttonIndex) {
    if (buttonIndex < 4) {
        // Voice selection (Buttons 0-3)
        uiState.selectedVoiceIndex = buttonIndex;
        uiState.voiceSelected = true;
        Serial.print("Voice selected: ");
        Serial.println(buttonIndex);
    } else if (buttonIndex >= 8 && buttonIndex <= 14) {
        // Preset assignment (Buttons 8-14)
        uint8_t presetIndex = buttonIndex - 8;
        if (!uiState.voiceSelected) {
            Serial.println("No voice selected - preset assignment ignored");
        } else {
            uiState.voicePresetIndices[uiState.selectedVoiceIndex] = presetIndex;
            VoicePresets::applyPresetToVoice(uiState.selectedVoiceIndex, presetIndex);
        }
    }
}

// Test functions
void runTests() {
    UIState uiState;

    Serial.println("=== Test Harness for Voice Selection and Preset Assignment ===");

    // Test 1: Press voice selection buttons
    Serial.println("\n--- Test 1: Voice Selection ---");
    for (uint8_t btn = 0; btn < 4; ++btn) {
        handleSettingsButtonPress(uiState, btn);
        Serial.print("After button ");
        Serial.print(btn);
        Serial.print(": selectedVoiceIndex=");
        Serial.print(uiState.selectedVoiceIndex);
        Serial.print(", voiceSelected=");
        Serial.println(uiState.voiceSelected ? "true" : "false");
    }

    // Test 2: Try preset assignment without voice selection
    Serial.println("\n--- Test 2: Preset Assignment Without Voice Selection ---");
    uiState.voiceSelected = false; // Reset
    handleSettingsButtonPress(uiState, 8); // Preset A

    // Test 3: Preset assignment with voice selected
    Serial.println("\n--- Test 3: Preset Assignment With Voice Selected ---");
    uiState.voiceSelected = true;
    uiState.selectedVoiceIndex = 2; // Voice 2
    handleSettingsButtonPress(uiState, 8); // Preset A
    handleSettingsButtonPress(uiState, 9); // Preset B

    // Test 4: Preset assignment at button boundaries
    Serial.println("\n--- Test 4: Preset Assignment Boundaries ---");
    handleSettingsButtonPress(uiState, 14); // Preset G
    handleSettingsButtonPress(uiState, 7); // Invalid preset button
    handleSettingsButtonPress(uiState, 15); // Invalid preset button

    Serial.println("\n=== Test Harness Complete ===");
}

// Call runTests() in setup() or loop() in your Arduino sketch
#endif // TestHarness.h