# LEDMatrix Module Documentation

## Overview

The grid speaks in light.

`src/LEDMatrix` drives the 8x8 WS2812B matrix for Pico2Seq. It shows step gates, playhead movement, parameter edits, settings feedback, voice state, and sensor-linked control feedback. The hardware surface is simple: 64 addressable LEDs on GPIO 1, driven through FastLED.

## Key Components

### 1. LEDConstants.h

Central LED matrix constants live here. Keep hardware numbers and timing values in one place.

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

The hardware wrapper for the 8x8 WS2812B matrix.

**Key Methods:**
- `LEDMatrix()` - Constructor initializes to black
- `begin(brightness)` - Initialize hardware with brightness level
- `setLED(x, y, color)` - Set individual LED color at coordinates
- `setAll(color)` - Set all LEDs to same color
- `show()` - Update physical LED matrix
- `clear()` - Set all LEDs to black
- `getLeds()` - Direct access to LED array (advanced use)

### 3. LEDController (LEDController.h)

The control LED layer. It maps UI state to visible indicators.

**Control LED Positions:**
- Parameter buttons: Note, Velocity, Filter, Attack, Decay, Octave, Slide
- Delay parameters: Time, Feedback (AS5600 encoder control)
- Mode indicators: Voice 1-4 (VoiceSystem integration), Delay toggle, Randomize

**VoiceSystem Integration:**
- Supports up to 4 voices through the centralized VoiceSystem
- Voice indicators use `voiceSystem.getVoiceId(index)` for consistent voice identification
- Preset feedback uses `uiState.voicePresetIndices[voiceIndex]` for array-based access

**Key Functions:**
- `initLEDController()` - System initialization
- `updateControlLEDs()` - Update all control LEDs based on UI state

### 4. LEDMatrixFeedback (LEDMatrixFeedback.h)

The feedback layer. It decides what the matrix should say during play, edit, and settings states.

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
|-- LEDConstants.h          # Constants and configuration
|-- LEDController.cpp       # Control LED implementation
|-- LEDController.h         # Control LED interface
|-- LEDmatrix.cpp           # LEDMatrix class implementation
|-- ledMatrix.h             # LEDMatrix class interface
|-- LEDMatrixFeedback.cpp   # Feedback system implementation
`-- LEDMatrixFeedback.h     # Feedback system interface
```

## Hardware Requirements

- 8x8 WS2812B LED matrix (64 individual LEDs)
- Data connection to GPIO pin 1
- 5V power supply capable of supporting LED current draw
- FastLED library compatible with target microcontroller

## Wiring Note

- LED data is documented on GP1.
- The touch matrix documentation also lists GP1 as one of the row electrodes.
- Check the hardware revision before changing pin definitions or wiring both surfaces.

## Performance Notes

- Batch LED writes and keep `show()` calls deliberate.
- Color blending uses optimized algorithms for smooth transitions.
- Direct array access is available for timing-sensitive work.
- Theme switching is immediate and may create brief visual artifacts.

## Integration Points

- **UI System**: Receives state updates from UIState
- **Sequencer**: Visualizes sequencer gate and playhead states
- **Voice System**: Shows voice parameter feedback and selection
- **Sensor System**: AS5600 encoder parameter visualization
- **Settings**: Theme selection and configuration feedback
