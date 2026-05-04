# Matrix Module

The grid is where the sequence becomes physical.

This module scans the Pico2Seq 4x8 capacitive touch matrix through an Adafruit MPR121 controller. It turns raw touch state into button state and press/release events for the rest of the firmware.

The code is deliberately small. The matrix should be boring in the best way: initialize it, scan it often, and let UI handlers decide what a button means.

## What It Handles

- **32 touch positions** - 4 rows by 8 columns.
- **MPR121 input** - reads touch state from an Adafruit MPR121 over I2C.
- **Debounced state** - tracks current button state instead of exposing raw sensor noise.
- **Press/release events** - dispatches changes through callback handlers.
- **Direct state queries** - lets UI code ask whether a button is currently held.

## Hardware Notes

The main project wiring documents the matrix as:

- GP0, GP1, GP2, GP3 - MPR121 electrodes 0-3, rows
- GP4, GP5, GP6, GP7, GP8, GP9, GP10, GP11 - MPR121 electrodes 4-11, columns

The main README also documents GP1 as WS2812B LED data. Check the active hardware revision before changing firmware pin assumptions.

## Dependency

- `Adafruit_MPR121` - Arduino library for the MPR121 capacitive touch controller.

## API Reference

### Constants

- `MATRIX_BUTTON_COUNT` - number of buttons, currently 32.
- `MATRIX_ROW_INPUTS` - row electrode assignments.
- `MATRIX_COL_INPUTS` - column electrode assignments.

### Data Types

- `MatrixButton` - row/column pairing for a button.
- `MatrixButtonEventType` - pressed or released event type.
- `MatrixButtonEvent` - event record passed to handlers.

### Core Functions

| Function | Purpose |
| --- | --- |
| `void Matrix_init(Adafruit_MPR121*)` | Attach the MPR121 instance and initialize matrix state. |
| `void Matrix_scan()` | Poll the sensor and dispatch button changes. Call this from the UI loop. |
| `bool Matrix_getButtonState(uint8_t)` | Read the current debounced state for one button. |
| `void Matrix_setEventHandler(func)` | Set a handler for press and release events. |
| `void Matrix_setRisingEdgeHandler(func)` | Set a handler for press events only. |
| `void Matrix_printState()` | Print all button states to the serial console. |

## Example

```cpp
#include <Adafruit_MPR121.h>
#include "Matrix.h"

Adafruit_MPR121 touchSensor = Adafruit_MPR121();

void setup()
{
    Serial.begin(115200);
    touchSensor.begin(0x5A);
    Matrix_init(&touchSensor);

    Matrix_setEventHandler([](const MatrixButtonEvent &event) {
        if (event.type == MATRIX_BUTTON_PRESSED)
        {
            Serial.print("Pressed: ");
        }
        else
        {
            Serial.print("Released: ");
        }

        Serial.println(event.buttonIndex);
    });
}

void loop()
{
    Matrix_scan();
    delay(10);
}
```

## Where It Fits

- `src/ui/` decides what a button press means in the current mode.
- `src/sequencer/` receives step and parameter edits through UI logic.
- `src/LEDMatrix/` renders the visual feedback that follows button state and sequencer state.
- `Pico2Seq.ino` calls `Matrix_scan()` from the Core 1 control loop.

For broader hardware, build, and runtime notes, see the main project README.
