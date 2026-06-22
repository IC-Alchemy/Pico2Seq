# ButtonHandlers Module Documentation

## Overview

The `ButtonHandlers` module provides specialized button event handling for the Pico2Seq UI system. This module was extracted from the main `UIEventHandler` to improve code organization and maintainability by encapsulating button-specific logic into focused, reusable functions.

## Key Components

### ButtonHandlers.h - Interface Definition

```cpp
#ifndef BUTTON_HANDLERS_H
#define BUTTON_HANDLERS_H

#include <Arduino.h>

// Forward declarations to avoid circular dependencies
class UIState;
class Sequencer;
class MidiNoteManager;

// Core button handling functions
void handleRandomizeButton(int voiceIndex, UIState& state);
void handleVoiceParameterButton(int voiceIndex, int paramIndex, UIState& state);
void handleControlButton(int buttonId, UIState& state);

// Button state management helpers
void beginRandomizePress(int voiceIndex, UIState& state);
void endRandomizePress(int voiceIndex, UIState& state);

#endif // BUTTON_HANDLERS_H
```

## Function Reference

### handleRandomizeButton(int voiceIndex, UIState& state)

Handles the randomize button for a specific voice (0-3). Implements dual behavior based on press duration:

- **Short Press**: Randomizes all parameters for the specified voice's sequencer
- **Long Press**: Triggers parameter reset behavior with UI feedback

**Parameters:**
- `voiceIndex`: Voice number (0-3) corresponding to sequencer selection
- `state`: Reference to UIState structure for state management

**Integration:** Routes to appropriate sequencer (`seq1`, `seq2`, `seq3`, or `seq4`) and triggers LED feedback.

### handleVoiceParameterButton(int voiceIndex, int paramIndex, UIState& state)

Handles voice parameter button presses for real-time voice configuration. Implements per-voice parameter editing for effects and envelope settings.

**Parameters:**
- `voiceIndex`: Voice number (0-3) for parameter targeting
- `paramIndex`: Matrix button index (8-15) mapped to specific parameters
- `state`: Reference to UIState for feedback timing and state tracking

**Button Mappings:**

| Button Index | Parameter Function |
|-------------|-------------------|
| 8 | Toggle envelope enable/disable |
| 9 | Toggle overdrive effect |
| 10 | Toggle wavefolder effect |
| 11 | Cycle filter mode (LP12, LP24, BP12, BP24) |
| 12 | Adjust filter resonance (0.0-1.0 range) |
| 13-15 | Reserved for future voice parameters |

**Integration:** Directly modifies VoiceConfig through VoiceManager API and applies changes immediately.

### handleControlButton(int buttonId, UIState& state)

Handles global control buttons that affect system-wide functionality. Implements centralized control logic for UI modes and system settings.

**Parameters:**
- `buttonId`: Control button identifier defined in UIConstants.h
- `state`: Reference to UIState for state updates

**Control Button Functions:**

| Button ID | Function | Description |
|-----------|-----------|-----------|
| `BUTTON_SLIDE_MODE` | Toggle slide/portamento mode | Enables/disables note transition smoothing |
| `BUTTON_AS5600_CONTROL` | Cycle AS5600 parameter | Rotates through available parameter controls |
| `BUTTON_PLAY_STOP` | Play/stop toggle | Controls sequencer playback state |
| `BUTTON_CHANGE_SCALE` | Scale selection | Cycles through available musical scales |
| `BUTTON_CHANGE_THEME` | LED theme cycling | Changes visual feedback color schemes |
| `BUTTON_CHANGE_SWING_PATTERN` | Shuffle pattern | Modifies rhythmic timing patterns |
| `BUTTON_TOGGLE_DELAY` | Delay effect toggle | Enables/disables global delay processing |

### beginRandomizePress(int voiceIndex, UIState& state) / endRandomizePress(int voiceIndex, UIState& state)

Button state tracking utilities for randomize button press/release events. Manages timing for short vs. long press detection.

**Parameters:**
- `voiceIndex`: Voice number (0-3) for tracking
- `state`: Reference to UIState containing timestamp arrays

**Usage:** Called automatically by matrix scanning system to maintain press state for duration-based actions.

## Integration Points

### UIEventHandler Integration

ButtonHandlers functions are called from `UIEventHandler.cpp` when matrix button events are detected:

```cpp
// In UIEventHandler.cpp
if (matrixButtonEvent.type == MATRIX_BUTTON_PRESSED) {
    int buttonId = calculateButtonId(matrixButtonEvent);
    if (isRandomizeButton(buttonId)) {
        handleRandomizeButton(voiceIndex, uiState);
    } else if (isVoiceParameter(buttonId)) {
        handleVoiceParameterButton(voiceIndex, buttonId, uiState);
    } else if (isControlButton(buttonId)) {
        handleControlButton(buttonId, uiState);
    }
}
```

### VoiceSystem Integration

Voice parameter buttons interact directly with the VoiceSystem through VoiceManager:

```cpp
// Voice parameter changes route through VoiceSystem
uint8_t currentVoiceId = voiceSystem.getVoiceId(voiceIndex);
VoiceConfig* config = voiceManager->getVoiceConfig(currentVoiceId);
if (config) {
    // Modify config based on paramIndex
    voiceManager->setVoiceConfig(currentVoiceId, *config);
}
```

### Sequencer Integration

Randomize button affects individual sequencers based on selected voice:

```cpp
Sequencer* activeSequencer = nullptr;
switch(voiceIndex) {
    case 0: activeSequencer = &seq1; break;
    case 1: activeSequencer = &seq2; break;
    case 2: activeSequencer = &seq3; break;
    case 3: activeSequencer = &seq4; break;
}

if (activeSequencer) {
    activeSequencer->randomizeParameters();
}
```

## Usage Examples

### Basic Button Event Handling

```cpp
#include "ButtonHandlers.h"
#include "UIState.h"

// In matrix scanning callback
void handleButtonPress(int buttonId, int voiceIndex, UIState& uiState) {
    if (isRandomizeButton(buttonId)) {
        handleRandomizeButton(voiceIndex, uiState);
    } else if (isVoiceParameter(buttonId)) {
        handleVoiceParameterButton(voiceIndex, buttonId, uiState);
    } else if (isControlButton(buttonId)) {
        handleControlButton(buttonId, uiState);
    }
}
```

### Voice Parameter Editing Workflow

```cpp
// Typical voice parameter editing sequence
void onVoiceParameterPress(int voiceIndex, int paramIndex, UIState& uiState) {
    // 1. Update UI state for feedback
    uiState.inVoiceParameterMode = true;
    uiState.lastVoiceParameterButton = paramIndex;
    uiState.voiceParameterChangeTime = millis();

    // 2. Handle the parameter change
    handleVoiceParameterButton(voiceIndex, paramIndex, uiState);

    // 3. System will provide visual feedback through LED/OLED
}
```

### Control Button Integration

```cpp
// Control button integration with system state
void onControlButtonPress(int buttonId, UIState& uiState) {
    handleControlButton(buttonId, uiState);

    // Additional integration as needed
    switch(buttonId) {
        case BUTTON_SLIDE_MODE:
            updateLEDMatrixForSlideMode(uiState.slideMode);
            break;
        case BUTTON_PLAY_STOP:
            if (uiState.playStopWasPressed) {
                isClockRunning ? stopSequencer() : startSequencer();
            }
            break;
    }
}
```

## Design Principles

### Separation of Concerns
ButtonHandlers focuses exclusively on button press logic and state management, delegating:
- Matrix scanning to Matrix module
- State persistence to UIState
- Audio processing to VoiceSystem/VoiceManager
- Visual feedback to LEDMatrix/OLED modules

### State Management
All button-related state is managed through the centralized UIState structure, ensuring:
- Consistent state access across modules
- Proper thread safety for Core 1 operations
- Easy debugging and state inspection

### Extensibility
The modular design allows easy addition of new button types:
1. Add button ID constants to UIConstants.h
2. Implement handler in ButtonHandlers.cpp
3. Route new button type in UIEventHandler.cpp

## File Structure

```
src/ui/
├── ButtonHandlers.cpp      # Implementation of button handling logic
├── ButtonHandlers.h        # Interface definitions and function declarations
├── UIEventHandler.cpp      # Matrix event detection and ButtonHandlers integration
├── UIEventHandler.h        # UI event processing interface
├── UIConstants.h           # Button ID definitions and mappings
└── UIState.h              # State management structure
```

## Performance Considerations

- **Minimal Overhead**: Button handling executes only on button press events
- **Non-blocking**: All functions return quickly without delays or polling
- **Memory Efficient**: No dynamic memory allocation; static state management
- **Thread Safe**: Operates on Core 1 with appropriate volatile state handling

## Testing and Debugging

The ButtonHandlers module can be tested independently by:
1. Simulating button press events with UIState manipulation
2. Verifying state changes without UI dependencies
3. Mocking VoiceSystem integration for isolated testing

Debug output is available through Serial logging in each button handler function for troubleshooting button-related issues.