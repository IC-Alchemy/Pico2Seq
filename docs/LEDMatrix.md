# LEDMatrix Module Documentation

## Overview

The `src/LEDMatrix` folder contains the LED matrix display system for the PicoMudrasSequencer. This module provides comprehensive visual feedback through an 8x8 WS2812B LED matrix, including sequencer state visualization, parameter editing feedback, and user interface indicators.

## Key Components

### 1. LEDConstants.h
Centralized constants for LED matrix configuration and visual parameters.

**Hardware Configuration:**
- Matrix dimensions: 8x8 (64 total LEDs)
- Data pin: GPIO 1
- Default brightness: 120

**Animation Timing:**
- Pulse frequency: 0.006 Hz
- Breathing cycle: 2000ms
- Blink interval: 500ms

**Color Categories:**
- Standard colors (Black, White)
- Delay effect colors (Time, Feedback indicators)
- Breathing animation colors
- Polyrhythmic overlay colors

**Brightness Levels:**
- Full: 200, High: 180, Medium: 128, Low: 64, Dim: 32, Subtle: 16

### 2. LEDMatrix Class (ledMatrix.h)
Core hardware abstraction layer for the 8x8 WS2812B LED matrix.

**Key Methods:**
- `LEDMatrix()` - Constructor initializes to black
- `begin(brightness)` - Initialize hardware with brightness level
- `setLED(x, y, color)` - Set individual LED color at coordinates
- `setAll(color)` - Set all LEDs to same color
- `show()` - Update physical LED matrix
- `clear()` - Set all LEDs to black
- `getLeds()` - Direct access to LED array (advanced use)

### 3. LEDController (LEDController.h)
Manages all control LED functionality for user interface elements.

**Control LED Positions:**
- Parameter buttons: Note, Velocity, Filter, Attack, Decay, Octave, Slide
- Delay parameters: Time, Feedback (AS5600 encoder control)
- Mode indicators: Voice 1/2, Delay toggle, Randomize

**Key Functions:**
- `initLEDController()` - System initialization
- `updateControlLEDs()` - Update all control LEDs based on UI state

### 4. LEDMatrixFeedback (LEDMatrixFeedback.h)
Comprehensive visual feedback system for sequencer state and UI modes.

**Display Modes:**
- **Idle Mode**: Breathing animation when sequencers stopped
- **Step Gates**: Visualizes sequencer gate states with playhead
- **Parameter Editing**: Shows parameter values and editing feedback
- **Settings Menu**: Navigation and preset selection display
- **Voice Parameters**: Parameter button highlighting and feedback

**Color Themes:**
- DEFAULT, OCEANIC, VOLCANIC, FOREST, NEON
- MODERN, DARK_NOCTIS, DARK_EMBER, BLUE, GREEN

**Key Functions:**
- `setupLEDMatrixFeedback()` - Initialize feedback system
- `updateStepLEDs()` - Main LED update function
- `updateSettingsModeLEDs()` - Settings menu display
- `updateVoiceParameterLEDs()` - Voice parameter feedback
- `setLEDTheme()` - Change active color theme

## Dependencies

- **FastLED**: Arduino library for WS2812B LED control
- **Arduino.h**: Standard Arduino framework
- **UIState**: Central UI state management
- **VoiceManager**: Voice system integration
- **Sequencer**: Sequencer state access

## Usage Example

```cpp
// Initialize LED matrix
LEDMatrix ledMatrix;
ledMatrix.begin();

// Set individual LED
ledMatrix.setLED(0, 0, CRGB::Red);

// Update control LEDs
updateControlLEDs(ledMatrix, uiState);

// Update step visualization
updateStepLEDs(ledMatrix, seq1, seq2, seq3, seq4, uiState, 0);

// Show changes
ledMatrix.show();
```

## File Structure

```
src/LEDMatrix/
├── LEDConstants.h          # Constants and configuration
├── LEDController.cpp       # Control LED implementation
├── LEDController.h         # Control LED interface
├── LEDmatrix.cpp           # LEDMatrix class implementation
├── ledMatrix.h             # LEDMatrix class interface
├── LEDMatrixFeedback.cpp   # Feedback system implementation
└── LEDMatrixFeedback.h     # Feedback system interface
```

## Hardware Requirements

- 8x8 WS2812B LED matrix (64 individual LEDs)
- Data connection to GPIO pin 1
- 5V power supply capable of supporting LED current draw
- FastLED library compatible with target microcontroller

## Performance Considerations

- LED updates should be batched to minimize `show()` calls
- Color blending uses optimized algorithms for smooth transitions
- Direct array access available for performance-critical operations
- Theme switching is immediate but may cause brief visual artifacts

## Integration Points

- **UI System**: Receives state updates from UIState
- **Sequencer**: Visualizes sequencer gate and playhead states
- **Voice System**: Shows voice parameter feedback and selection
- **Sensor System**: AS5600 encoder parameter visualization
- **Settings**: Theme selection and configuration feedback