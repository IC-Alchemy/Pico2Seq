# ButtonHandlers Module Documentation

## Overview

The `ButtonHandlers` module provides specialized button event handling for the Pico2Seq UI system. This module was extracted from the main `UIEventHandler` to improve code organization and maintainability by encapsulating button-specific logic into focused, reusable functions.

Since the **Alchemy tile migration** (see
`docs/superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md`),
the physical parameter/utility buttons no longer live on the MPR121 matrix —
all 32 matrix indices are dedicated step pads, and the buttons moved to two
Alchemy Modular UI tiles on Wire1:

- **SliderModule** (slot 0): 4 faders + 4 buttons — the buttons are direct
  **Voice 1–4 selects** in both modes.
- **ButtonModule8** (slot 1): 8 buttons — the 7 parameter buttons plus a
  **Shift** button (Param mode), or 6 utility buttons plus Shift (Utility mode).
- A **GP7 strap switch** selects the tile function set: LOW = Param mode,
  HIGH = Utility mode (software-debounced 20 ms; on a flip all holds/latches
  clear, a control LED flashes and the OLED shows a PARAM/UTIL banner).

The tile → firmware translation lives in `src/ui/AlchemyControlBridge.*`
(glue) and `src/ui/ControlSurfaceLogic.*` (pure, unit-tested: mode
stabilization, pad-bank resolution, Shift latching, fader mapping). The
matrix event path and the bridge both funnel into the *same* handler code
documented below.

### Alchemy tile semantics

**Param mode (GP7 low)** — ButtonModule8 bits 0–7:

| Bit | Function | Behavior |
|---|---|---|
| 0 | Note | hold-to-arm; step presses program notes |
| 1 | Velocity | hold-to-arm + encoder auto-select |
| 2 | Filter | hold-to-arm + encoder auto-select |
| 3 | Attack | hold-to-arm + encoder auto-select |
| 4 | Decay | hold-to-arm + encoder auto-select |
| 5 | Octave | hold-to-arm + encoder auto-select |
| 6 | Slide | toggles slide mode (clears conflicting modes) |
| 7 | Shift | modifier (see below) |

**Utility mode (GP7 high)** — ButtonModule8 bits 0–7:

| Bit | Function |
|---|---|
| 0 | Play/Stop (long-press while stopped opens settings) |
| 1 | Delay on/off toggle |
| 2 | Scale cycle |
| 3 | Swing template cycle |
| 4 | Theme cycle |
| 5 | Encoder-control target cycle (hold = gate-seq-length mode) |
| 6 | Randomize selected voice (long-press = reset) |
| 7 | Shift (modifier) |

**Shift modifier (both modes):**
- Shift + param tap: latch/unlatch that parameter (at most one latch;
  latching another moves it; latched params read as held without a finger).
- Shift + step pad: clear that step (gate off + params reset) on the pad's
  own voice.
- Shift + Voice1..4 (slider buttons): Play/Stop, Randomize selected voice,
  Scale cycle, Delay toggle.

**Faders** — Param mode: Filter, Attack, Decay, Velocity for the selected
voice (record into the step in edit via the shared recording path when the
matching param is armed). Utility mode: Tempo (uClock), Swing amount,
Delay mix, Gate length (selected voice). ~8-count deadband, send on change.

**Step pads (all 32 matrix indices):** `bank = index / 16`,
`step = index % 16`; selected voice 1/2 → banks are voices 1/2, selected
voice 3/4 → banks are voices 3/4. Every pad action (gate toggle, long-press
step edit, param-hold programming, gate-seq-length entry, slide-mode
toggling) resolves through this bank mapping instead of assuming the single
selected voice.

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
| `BUTTON_ENCODER_CONTROL` | Cycle encoder parameter | Rotates through available parameter controls |
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

### UIEventHandler / AlchemyControlBridge Integration

The matrix path handles only step pads now; parameter/utility buttons arrive
from `AlchemyControlBridge`, and both share the same entry points:

```cpp
// From loop1()'s 1 ms control slice (Pico2Seq.ino):
Matrix_scan();  // 32 step pads -> matrixEventHandler -> handleStepButtonEvent
alchemyBridge.update(currentMillis, uiState, bridgeSequencers, 4, midiNoteManager);

// Inside AlchemyControlBridge, tile edges call the shared handlers:
handleParameterButtonById(paramId, pressed, uiState);   // param buttons
handleSlideModePress(uiState);                          // Slide tile button
handleControlButton(BUTTON_PLAY_STOP, uiState);         // utility buttons
beginRandomizePress(v, uiState); handleRandomizeButton(v, uiState);
selectVoice(uiState, midiNoteManager, v);               // Voice1..4 buttons
clearSequencerStep(seq, step);                          // Shift + pad
```

Long-press detection for tile randomize (reset) and the encoder-control hold
(gate-seq-length mode) is promoted by `pollUIHeldButtons()`, which runs every
`loop1()` pass exactly as before.

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
├── AlchemyControlBridge.cpp  # Alchemy tile -> firmware UI glue (Core 1)
├── AlchemyControlBridge.h
├── ControlSurfaceLogic.cpp   # Pure decision logic (unit-tested)
├── ControlSurfaceLogic.h
├── ButtonHandlers.cpp        # Implementation of button handling logic
├── ButtonHandlers.h          # Interface definitions and function declarations
├── ButtonManager.cpp         # ParamId-keyed param name/hold helpers
├── UIEventHandler.cpp        # Matrix step-pad events + shared entry points
├── UIEventHandler.h          # UI event processing interface
├── UIConstants.h             # Button ID definitions and mappings
└── UIState.h                 # State management structure
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