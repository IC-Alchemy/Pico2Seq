# Matrix Module Documentation

## Overview

The `src/matrix` folder contains the button matrix scanning and event handling system for the PicoMudrasSequencer. This module provides a robust interface for a 32-button (4×8) grid using the Adafruit MPR121 capacitive touch sensor, with built-in debouncing and event dispatch capabilities.

## Key Components

### 1. Matrix.h - Interface Definition

**Constants:**
- `MATRIX_BUTTON_COUNT = 32` - Total number of buttons in the 4×8 matrix

**Data Types:**
```cpp
typedef struct {
    uint8_t rowInput;    // MPR121 electrode for row
    uint8_t colInput;    // MPR121 electrode for column
} MatrixButton;

typedef enum {
    MATRIX_BUTTON_PRESSED,   // Button press event
    MATRIX_BUTTON_RELEASED   // Button release event
} MatrixButtonEventType;

typedef struct {
    uint8_t buttonIndex;           // Linear button index (0-31)
    MatrixButtonEventType type;    // Press or release
} MatrixButtonEvent;
```

**Pin Mapping:**
- Row inputs: Electrodes 0, 1, 2, 3
- Column inputs: Electrodes 4, 5, 6, 7, 8, 9, 10, 11

### 2. Matrix.cpp - Implementation

**Core Functions:**
- `Matrix_init(Adafruit_MPR121*)` - Initialize with MPR121 sensor
- `Matrix_scan()` - Poll for button state changes (call in main loop)
- `Matrix_getButtonState(uint8_t)` - Query current button state
- `Matrix_setEventHandler(func)` - Set general event handler
- `Matrix_setRisingEdgeHandler(func)` - Set press-only handler
- `Matrix_printState()` - Debug output of all button states

**Key Features:**
- **Debouncing**: Automatic state change detection with reliable debouncing
- **Event Dispatch**: Supports both press/release and press-only callbacks
- **State Tracking**: Maintains current state for all 32 buttons
- **Early Exit**: Optimized scanning with early termination for performance
- **Bounds Checking**: Safe access with index validation

**Button Mapping:**
- Linear indexing: 0-31 corresponds to rows 0-3, columns 0-7
- Physical layout: Button at row R, column C = index (R × 8 + C)
- MPR121 integration: Uses electrode touch bits for reliable detection

### 3. README.md - Module Documentation

Provides comprehensive module documentation including:
- Feature overview and dependencies
- Complete API reference with examples
- Integration guidelines for main loop usage
- Related module connections (LEDMatrix, Sequencer)

## Dependencies

- **Adafruit MPR121**: Library for I2C capacitive touch sensor interface
- **Arduino.h**: Standard Arduino framework
- **OneButton.h**: Button debouncing utilities (referenced but not directly used)

## Usage Pattern

### Basic Setup
```cpp
#include <Adafruit_MPR121.h>
#include "Matrix.h"

Adafruit_MPR121 capSensor = Adafruit_MPR121();
MatrixButtonEvent lastEvent;

void setup() {
    Serial.begin(115200);
    capSensor.begin(0x5A);  // Default I2C address
    Matrix_init(&capSensor);
}

void loop() {
    Matrix_scan();  // Poll for changes
    delay(10);      // Timing control
}
```

### Event Handling
```cpp
// General event handler (press and release)
Matrix_setEventHandler([](const MatrixButtonEvent &evt) {
    if (evt.type == MATRIX_BUTTON_PRESSED) {
        Serial.printf("Button %d pressed\n", evt.buttonIndex);
    } else {
        Serial.printf("Button %d released\n", evt.buttonIndex);
    }
});

// Press-only handler
Matrix_setRisingEdgeHandler([](uint8_t buttonIndex) {
    Serial.printf("Button %d activated\n", buttonIndex);
});
```

### State Query
```cpp
// Check specific button state
if (Matrix_getButtonState(15)) {  // Button at row 1, column 7
    // Button is currently pressed
}

// Debug output
Matrix_printState();  // Print 4×8 grid of button states
```

## Hardware Integration

### MPR121 Sensor
- **I2C Address**: 0x5A (default)
- **Electrodes**: 12 total (0-11)
- **Touch Detection**: Capacitive sensing with configurable thresholds
- **Update Rate**: Polled in main loop via `Matrix_scan()`

### Button Matrix Layout
```
Electrode Layout:
    Columns (4-11):   0   1   2   3   4   5   6   7
Rows (0-3):      0 [0] [1] [2] [3] [4] [5] [6] [7]
                 1 [8] [9] [10][11][12][13][14][15]
                 2 [16][17][18][19][20][21][22][23]
                 3 [24][25][26][27][28][29][30][31]
```

## Performance Characteristics

- **Low Latency**: Immediate response to touch events
- **Efficient Scanning**: Early exit optimization when no electrodes touched
- **Minimal Memory**: Static arrays for state tracking
- **Debounced Output**: Reliable state changes without false triggers

## Integration Points

- **LEDMatrix Module**: Visual feedback for button presses
- **Sequencer Module**: Step parameter control and pattern editing
- **UI System**: Button state queries for interface logic
- **Main Loop**: Requires regular `Matrix_scan()` calls for responsiveness

## File Structure

```
src/matrix/
├── Matrix.cpp          # Implementation with scanning logic
├── Matrix.h            # Interface definitions and API
└── README.md           # Module documentation and examples
```

## Error Handling

- **Sensor Validation**: Null pointer checks for MPR121 instance
- **Bounds Checking**: Index validation for button state queries
- **Debug Output**: Serial logging for initialization and state changes
- **Graceful Degradation**: Safe returns when sensor unavailable

## Development Notes

- **Self-Contained**: Can be reused in other projects with compatible hardware
- **Extensible**: Event handler pattern allows flexible integration
- **Well-Documented**: Comprehensive code comments and API documentation
- **Tested**: Debug functions for hardware validation and troubleshooting