# OLED Module Documentation

## Overview

The `src/OLED` folder contains the display system for the PicoMudrasSequencer, providing comprehensive visual feedback through a 128×64 SH1106G OLED display. The module handles parameter editing, settings navigation, voice configuration, and real-time status display.

## Key Components

### 1. OLEDDisplay Class - Main Display Manager

**Core Interface:**
- `OLEDDisplay()` - Constructor
- `begin()` - Initialize display hardware
- `update()` - Main display update with UI state
- `clear()` - Clear display and turn off all pixels
- `isInitialized()` - Check display readiness

**Display Modes:**
- **Parameter Editing**: Real-time value display with progress bars
- **Settings Menu**: Navigation and preset selection interface
- **Voice Parameters**: Configuration toggles and status display
- **Default Status**: Scale, shuffle pattern, and voice indicators

### 2. VoiceParameterObserver Interface

**Observer Pattern Implementation:**
- `onVoiceParameterChanged()` - Immediate parameter change feedback
- `onVoiceSwitched()` - Voice switching notifications

**Real-time Updates:**
- Immediate visual feedback for parameter changes
- Observer pattern for decoupled event handling
- Non-blocking update mechanism

## Display Hierarchy

### Priority-based Rendering System

**Highest Priority:**
- Voice parameter editing mode (in settings)
- Recent parameter interactions (within timeout windows)

**Medium Priority:**
- Settings menu navigation
- Preset selection interface

**Lowest Priority:**
- Default status screen (scale, shuffle, voice)
- Step indicators and sequence visualization

### Timing and Animation

**Timeout Management:**
- Parameter edit mode: 5-second timeout
- Settings mode: Persistent during navigation
- Voice parameter mode: 3-second timeout

**Visual Enhancements:**
- Progress bars for continuous parameters
- Step indicators with beat synchronization
- Border animations for professional appearance
- Startup sequence with wipe and bounce effects

## Parameter Display System

### Value Formatting

**Parameter-specific formatting:**
- **Note**: Integer display
- **Velocity**: Percentage (0-100%)
- **Filter**: Frequency in Hz with exponential mapping
- **Attack/Decay**: Time in seconds with millisecond precision
- **Octave**: Discrete values (-1, 0, +1)
- **Gate Length**: Percentage of full duration
- **Gate/Slide**: Binary ON/OFF states

**Progress Bar System:**
- Visual representation for continuous parameters
- Excludes discrete parameters (Note, Octave, Gate, Slide)
- Real-time value updates during editing

## Hardware Integration

### SH1106G Display Specifications

**Physical Interface:**
- I2C communication protocol
- Default address: 0x3C
- Resolution: 128×64 pixels
- White monochrome display

**Electrical Requirements:**
- 3.3V-5V supply voltage
- I2C bus integration
- Reset pin configuration

### Performance Optimizations

**Rendering Strategy:**
- Single display clear per frame
- Batched drawing operations
- Minimal dynamic memory allocation
- Integer-based geometry calculations

**Timing Considerations:**
- Non-blocking update cycles
- Millisecond-based timeout management
- Efficient text rendering with size optimization

## Configuration Constants

### Layout Constants (from LEDConstants.h)

**Display Geometry:**
- Screen width: 128 pixels
- Screen height: 64 pixels
- Text margin: 5 pixels
- Line spacing: 10 pixels
- Header height: 14 pixels

**Visual Elements:**
- Progress bar height: 10 pixels
- Border thickness: 1 pixel
- Step indicator height: 8 pixels

## Dependencies

### External Libraries

- **Adafruit_SH110X**: SH1106G OLED display driver
- **Adafruit_GFX**: Graphics primitives and text rendering
- **Wire**: I2C communication protocol

### Internal Modules

- **UIState**: Current user interface state
- **ButtonManager**: Button input handling
- **Sequencer**: Sequence parameter access
- **VoiceManager**: Voice configuration management
- **LEDConstants**: Shared display constants

## Usage Examples

### Basic Display Setup

```cpp
// Initialize display
OLEDDisplay oledDisplay;
if (!oledDisplay.begin()) {
    Serial.println("OLED init failed!");
}

// Set voice manager reference
oledDisplay.setVoiceManager(&voiceManager);
```

### Display Update Cycle

```cpp
// Main loop update
oledDisplay.update(uiState, seq1, seq2, seq3, seq4, voiceManager);

// Alternative without voice manager
oledDisplay.update(uiState, seq1, seq2, seq3, seq4);
```

### Parameter Information Display

```cpp
// Show parameter editing screen
displayParameterInfo("Filter", 0.75f, 1, 8);
// Shows: "Filter" with progress bar at 75%
// Voice 1, Step 9 (0-indexed)
```

### Settings Menu Display

```cpp
// Show settings navigation
displaySettingsMenu(uiState);
// Displays preset selection or main menu
```

## Error Handling

### Initialization Failure

- Graceful degradation if display hardware unavailable
- Serial error logging for debugging
- Safe return from all methods when uninitialized

### Parameter Validation

- Bounds checking for parameter values
- Safe array access for voice configurations
- Null pointer protection for voice manager references

## File Structure

```
src/OLED/
├── oled.cpp          # Display implementation with rendering logic
└── oled.h            # Interface definitions and observer pattern
```

## Development Notes

### Design Principles

1. **Deterministic Rendering**: Clear priority hierarchy prevents display conflicts
2. **Low Overhead**: Minimal heap usage and efficient drawing operations
3. **Embedded-Friendly**: Non-blocking operations suitable for real-time systems
4. **Maintainable**: Clear separation of concerns and helper functions

### Performance Characteristics

- **Memory Usage**: Static allocation with minimal dynamic memory
- **CPU Load**: Efficient integer math and batched operations
- **Update Frequency**: Frame-based rendering with timeout management
- **Responsiveness**: Immediate feedback for parameter changes

### Future Enhancements

- **Custom Fonts**: Enhanced typography options
- **Animation System**: Extended visual feedback capabilities
- **Theming Support**: Multiple visual themes and color schemes
- **Debug Mode**: Enhanced diagnostic display modes