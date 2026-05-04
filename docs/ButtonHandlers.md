# ButtonHandlers Module Documentation

Buttons are where the hand enters the firmware.

The `ButtonHandlers` module contains focused button event handlers for the Pico2Seq UI system. It was split out of `UIEventHandler` so matrix scanning, UI state, sequencer selection, and voice configuration do not all live in one place.

## Interface

`ButtonHandlers.h` exposes the button-specific functions and keeps dependencies forward-declared.

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

## Functions

### `handleRandomizeButton(int voiceIndex, UIState& state)`

Handles randomize behavior for one voice. `voiceIndex` is zero-based and must target voices `0..3`.

- Short press: randomizes all parameters for that voice's sequencer.
- Long press: triggers the reset path and UI feedback.
- Integration: selects `seq1`, `seq2`, `seq3`, or `seq4`, then updates LED feedback.

### `handleVoiceParameterButton(int voiceIndex, int paramIndex, UIState& state)`

Handles real-time voice configuration. It edits the selected voice through VoiceManager and VoiceSystem.

- `voiceIndex`: zero-based voice number, `0..3`.
- `paramIndex`: matrix button index, usually `8..15`.
- `state`: shared UIState used for timing and feedback.

Button mapping:

| Button Index | Parameter Function |
|-------------|-------------------|
| 8 | Toggle envelope enable/disable |
| 9 | Toggle overdrive effect |
| 10 | Toggle wavefolder effect |
| 11 | Cycle filter mode (LP12, LP24, BP12, BP24) |
| 12 | Adjust filter resonance (0.0-1.0 range) |
| 13-15 | Reserved for future voice parameters |

### `handleControlButton(int buttonId, UIState& state)`

Handles global controls. These affect the whole instrument, not a single voice.

| Button ID | Function | Description |
|-----------|-----------|-----------|
| `BUTTON_SLIDE_MODE` | Toggle slide/portamento mode | Enables/disables note transition smoothing |
| `BUTTON_AS5600_CONTROL` | Cycle AS5600 parameter | Rotates through available parameter controls |
| `BUTTON_PLAY_STOP` | Play/stop toggle | Controls sequencer playback state |
| `BUTTON_CHANGE_SCALE` | Scale selection | Cycles through available musical scales |
| `BUTTON_CHANGE_THEME` | LED theme cycling | Changes visual feedback color schemes |
| `BUTTON_CHANGE_SWING_PATTERN` | Shuffle pattern | Modifies rhythmic timing patterns |
| `BUTTON_TOGGLE_DELAY` | Delay effect toggle | Enables/disables global delay processing |

### `beginRandomizePress(...)` and `endRandomizePress(...)`

These helpers track press and release timing for randomize buttons.

- `voiceIndex`: zero-based voice number, `0..3`.
- `state`: UIState storage for timestamp arrays.
- Usage: called by matrix scanning so short and long presses can be resolved later.

## UIEventHandler Integration

`UIEventHandler.cpp` receives matrix events and routes them to these handlers.

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

## VoiceSystem Integration

Voice parameter buttons resolve from UI voice index to internal voice ID before touching configuration.

```cpp
// Voice parameter changes route through VoiceSystem
uint8_t currentVoiceId = voiceSystem.getVoiceId(voiceIndex);
VoiceConfig* config = voiceManager->getVoiceConfig(currentVoiceId);
if (config) {
    // Modify config based on paramIndex
    voiceManager->setVoiceConfig(currentVoiceId, *config);
}
```

Keep the distinction clear.

- UI voice index: `0..3`.
- Internal voice ID: returned by VoiceSystem.
- Hardware channel labels may still use their own numbering where needed.

## Sequencer Integration

Randomize uses the selected voice index to find the matching sequencer.

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

## Design Notes

- Matrix scanning belongs to the Matrix module.
- Button state belongs to UIState.
- Audio and voice configuration belong to VoiceSystem and VoiceManager.
- Visual feedback belongs to LEDMatrix and OLED code.
- ButtonHandlers should stay event-driven and non-blocking.
- No dynamic allocation is needed here.

## File Structure

```text
src/ui/
|-- ButtonHandlers.cpp      # Implementation of button handling logic
|-- ButtonHandlers.h        # Interface definitions and function declarations
|-- UIEventHandler.cpp      # Matrix event detection and ButtonHandlers integration
|-- UIEventHandler.h        # UI event processing interface
|-- UIConstants.h           # Button ID definitions and mappings
`-- UIState.h              # State management structure
```

## Testing Notes

This module can be tested by simulating matrix events and checking UIState changes. VoiceSystem calls can be mocked for isolated tests. Serial logging in each handler can help trace button-specific faults during hardware bring-up.
