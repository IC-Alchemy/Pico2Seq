# Matrix Module Documentation

Thirty-two touch points. Four rows. Eight columns.

The `src/matrix` folder contains the capacitive button matrix scanner for Pico2Seq. It reads a 4 x 8 grid through the Adafruit MPR121, tracks state, debounces transitions, and dispatches press/release events to the UI layer.

## Components

### `Matrix.h`

Constants:

- `MATRIX_BUTTON_COUNT = 32`: total buttons in the 4 x 8 matrix.

Data types:

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

Pin mapping:

- Row inputs: electrodes `0, 1, 2, 3`.
- Column inputs: electrodes `4, 5, 6, 7, 8, 9, 10, 11`.

### `Matrix.cpp`

Core functions:

- `Matrix_init(Adafruit_MPR121*)`: initialize with the MPR121 sensor.
- `Matrix_scan()`: poll for state changes. Call from the main loop.
- `Matrix_getButtonState(uint8_t)`: query a button by index.
- `Matrix_setEventHandler(func)`: set a press/release event handler.
- `Matrix_setRisingEdgeHandler(func)`: set a press-only handler.
- `Matrix_printState()`: print all button states for debug.

Behavior:

- Debounces state changes.
- Tracks all 32 button states.
- Emits press and release events.
- Supports a press-only rising-edge handler.
- Exits early when no electrodes are touched.
- Bounds-checks button indices.

### `README.md`

The module README carries the fuller local API reference, examples, and links to related modules such as LEDMatrix and Sequencer.

## Dependencies

- Adafruit MPR121 for I2C capacitive touch sensing.
- Arduino.h for the Arduino framework.
- OneButton.h is referenced, though not directly used by the matrix scanner.

## Basic Setup

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

## Event Handling

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

## State Query

```cpp
// Check specific button state
if (Matrix_getButtonState(15)) {  // Button at row 1, column 7
    // Button is currently pressed
}

// Debug output
Matrix_printState();  // Print 4 x 8 grid of button states
```

## Hardware Layout

- I2C address: `0x5A` by default.
- MPR121 electrodes: `0..11`.
- Rows use electrodes `0..3`.
- Columns use electrodes `4..11`.
- `Matrix_scan()` is polled from the main loop.

```text
Electrode Layout:
    Columns (4-11):   0   1   2   3   4   5   6   7
Rows (0-3):      0 [0] [1] [2] [3] [4] [5] [6] [7]
                 1 [8] [9] [10][11][12][13][14][15]
                 2 [16][17][18][19][20][21][22][23]
                 3 [24][25][26][27][28][29][30][31]
```

Linear index formula:

```text
buttonIndex = row * 8 + column
```

## Integration Points

- LEDMatrix uses matrix events for visual feedback.
- Sequencer uses matrix input for step editing and parameter control.
- UI code reads button state for mode logic.
- The main loop must call `Matrix_scan()` often enough to keep touch response clean.

## File Structure

```text
src/matrix/
|-- Matrix.cpp          # Implementation with scanning logic
|-- Matrix.h            # Interface definitions and API
`-- README.md           # Module documentation and examples
```

## Error Handling

- Null MPR121 pointers are checked.
- Button index reads are bounds-checked.
- Serial debug output is available for initialization and state changes.
- Calls return safely if the sensor is unavailable.

## Development Notes

Keep this module small. It should know how to scan the touch grid and report events. It should not know what a button means musically. That belongs in the UI and sequencer layers.
