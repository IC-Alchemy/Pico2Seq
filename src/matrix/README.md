# Pico2Seq Matrix Module

This module implements touch matrix scanning and event dispatch for Pico2Seq, handling a 32-button grid using the Adafruit MPR121 capacitive touch sensor. See the [main README](../../README.md) for overall project context.

---

## Overview

- Drives a 4×8 (32-pad) capacitive touch grid with low-latency hardware scanning on Core 0
- Provides 32 dedicated step pads across two 16-step voice banks addressing the active voice pair
- IRQ-gated state-change scanning with press/release event callbacks
- Integrates with the Adafruit MPR121 I2C sensor on `Wire` @ `0x5A`
- Seamlessly pairs with the Alchemy Modular UI tile panel which hosts parameter and utility controls

---

## Features

- 32-pad (4-row × 8-column) step sequencing grid
- Dual 16-step voice banks (Low Bank = Voice 0 or 2, High Bank = Voice 1 or 3)
- IRQ-gated state-change tracking: `Matrix_scan()` is a no-op until the MPR121's GP8 `/IRQ` flags a change — no separate debounce stage
- Callback hooks for all button press/release events
- State query at any time for responsive UI logic
- Lightweight initialization and non-blocking main loop integration

---

## Dependencies

- **[Adafruit MPR121](https://github.com/adafruit/Adafruit_MPR121):** Library for interfacing with the MPR121 capacitive touch controller over I2C on Arduino-compatible devices.

---

## API Reference

### Constants

- `MATRIX_BUTTON_COUNT` – Number of buttons (32)
- `MATRIX_ROW_INPUTS`, `MATRIX_COL_INPUTS` – Electrode assignments

### Data Types

- `MatrixButton` – Row/column pairing for each button
- `MatrixButtonEventType` – `MATRIX_BUTTON_PRESSED` or `MATRIX_BUTTON_RELEASED`
- `MatrixButtonEvent` – Event record for callback handlers

### Core Functions

| Function | Description |
|:---|:---|
| `void Matrix_init(Adafruit_MPR121*)` | Initialize with sensor instance |
| `void Matrix_scan()` | IRQ-gated scan for state changes (no-op until the MPR121 IRQ; call in main loop on Core 0) |
| `bool Matrix_getButtonState(uint8_t)` | Query current button state |
| `void Matrix_setEventHandler(func)` | Set general event handler for button events |
| `void Matrix_setRisingEdgeHandler(func)` | Set handler for button press only |
| `void Matrix_printState()` | Print all button states to serial console |

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

## Related Documentation

- [Touch Matrix Documentation](../../docs/matrix.md): In-depth subsystem and voice pair bank architecture
- [LED Matrix Documentation](../../docs/led-matrix.md): Visualizes sequencer/play states triggered by button input
- [Sequencer Documentation](../../docs/sequencer.md): Uses button events to drive real-time step parameter changes
- [Architecture Documentation](../../docs/architecture.md): Dual-core division and system overview
- [Main Project README](../../README.md): Project overview and setup instructions
