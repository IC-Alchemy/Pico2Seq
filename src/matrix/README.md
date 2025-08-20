# Matrix Module

This module implements matrix scanning and event dispatch for the Mudras Sequencer, handling a 32-button grid using the Adafruit MPR121 capacitive touch sensor. See the [main README](../../README.md) for overall project context.

---

## Overview

- Supports a 4x8 (32-button) matrix with low-latency scanning
- Reliable debouncing and event callbacks for press/release
- Integrates with the Adafruit MPR121 I2C sensor for touch input
- Designed for expressive, responsive hardware control and sequencing

---

## Features

- 32-button (4-row × 8-column) grid
- Debounced button state tracking
- Callback hooks for all button press/release events
- State query at any time for responsive UI logic
- Simple, robust initialization and main loop integration

---

## Dependencies

- **[Adafruit MPR121](https://github.com/adafruit/Adafruit_MPR121):** Library for interfacing with the MPR121 capacitive touch controller over I2C on Arduino-compatible devices.

---

## API Reference

### Constants

- `MATRIX_BUTTON_COUNT` – Number of buttons (32)
- `MATRIX_ROW_INPUTS`, `MATRIX_COL_INPUTS` – Pin assignments

### Data Types

- `MatrixButton` – Row/column pairing for each button
- `MatrixButtonEventType` – Pressed or released event
- `MatrixButtonEvent` – Event record for callback handlers

### Core Functions

| Function                               | Description                                         |
|:----------------------------------------|:----------------------------------------------------|
| `void Matrix_init(Adafruit_MPR121*)`    | Initialize with sensor instance                     |
| `void Matrix_scan()`                    | Poll for button state changes (call in main loop)   |
| `bool Matrix_getButtonState(uint8_t)`   | Query current button state                          |
| `void Matrix_setEventHandler(func)`     | Set general event handler for button events         |
| `void Matrix_setRisingEdgeHandler(func)`| Set handler for button press only                   |
| `void Matrix_printState()`              | Print all button states to serial console           |

---

## Example Usage

```cpp
#include <Adafruit_MPR121.h>
#include "Matrix.h"

Adafruit_MPR121 capSensor = Adafruit_MPR121();

void setup() {
    Serial.begin(115200);
    capSensor.begin(0x5A);
    Matrix_init(&capSensor);

    Matrix_setEventHandler([](const MatrixButtonEvent &evt) {
        if (evt.type == MATRIX_BUTTON_PRESSED) {
            Serial.print("Button pressed: ");
        } else {
            Serial.print("Button released: ");
        }
        Serial.println(evt.buttonIndex);
    });
}

void loop() {
    Matrix_scan();
    delay(10);
}
```

---

## Related Modules

- [LEDMatrix Module](../LEDMatrix/README.md): Visualizes sequencer/play states triggered by button input
- [Sequencer Module](../sequencer/README.md): Uses button events to drive real-time step parameter changes

---

## See Also

- [Main Project README](../../README.md)
- [Technical Manual](../../PROGRAMMERS_MANUAL.md)