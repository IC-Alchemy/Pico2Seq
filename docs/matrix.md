# Matrix Module Documentation (MPR121 Touch Input)

## Overview

The `src/matrix/` subsystem provides the capacitive touch step-input interface for Pico2Seq using an **Adafruit MPR121** 12-channel capacitive touch sensor over I2C (`Wire` @ `0x5A`).

> **Disambiguation Note:**
> - `docs/matrix.md` (this document) describes the **MPR121 capacitive touch step-pad input subsystem** (`src/matrix/`).
> - [`docs/LEDMatrix.md`](LEDMatrix.md) describes the **WS2812B 8×8 RGB LED visual output system** on GPIO 1 (`src/LEDMatrix/`).

---

## Hardware Configuration & Pinout

- **Sensor IC:** Adafruit MPR121 12-channel Capacitive Touch Controller
- **Bus:** `Wire` (I2C0)
  - `SDA`: GP4
  - `SCL`: GP5
- **I2C Address:** `0x5A` (default)
- **Matrix Topology:** 4 Row electrodes (Electrodes 0–3) × 8 Column electrodes (Electrodes 4–11), multiplexed to sense 32 discrete touch pads.
- **Scanning Frequency:** Polled on Core 1 in `loop1()` every 1 ms with automatic debouncing.

---

## 32 Dedicated Step Pads & Bank Architecture

With the integration of the dedicated [Alchemy Modular UI tile panel](superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md) (which offloads parameter and utility buttons to I2C1), all 32 MPR121 touch pads function as **dedicated step sequencing pads**.

The 32 pads are organized into two 16-step banks that address the currently selected voice pair:

```
                            32-PAD TOUCH GRID
  +-------------------------------------------------------------------+
  | Row 0: Pads  0 -  7  -->  Low Voice  (Steps  0 -  7)               |
  | Row 1: Pads  8 - 15  -->  Low Voice  (Steps  8 - 15)               |
  +-------------------------------------------------------------------+
  | Row 2: Pads 16 - 23  -->  High Voice (Steps  0 -  7)               |
  | Row 3: Pads 24 - 31  -->  High Voice (Steps  8 - 15)               |
  +-------------------------------------------------------------------+
```

### Voice Pair Resolution (`PadBank` Logic)
The 32 pads dynamically map to voices based on the currently selected voice (`uiState.selectedVoiceIndex` 0–3):

| Selected Voice | Low Bank (Pads 0–15) | High Bank (Pads 16–31) |
|---|---|---|
| **Voice 0** | Voice 0 (Steps 0–15) | Voice 1 (Steps 0–15) |
| **Voice 1** | Voice 0 (Steps 0–15) | Voice 1 (Steps 0–15) |
| **Voice 2** | Voice 2 (Steps 0–15) | Voice 3 (Steps 0–15) |
| **Voice 3** | Voice 2 (Steps 0–15) | Voice 3 (Steps 0–15) |

For any pad index `0..31`:
```cpp
// From src/ui/ControlSurfaceLogic.h
const uint8_t bank = padIndex / 16;  // 0 = low bank, 1 = high bank
const uint8_t step = padIndex % 16;  // 0..15 step index within voice
const uint8_t voice = (selectedVoiceIndex / 2) * 2 + bank;
```

---

## Interaction with the Alchemy UI Panel

The user interface separates physical responsibilities across two buses:

1. **Step Grid (MPR121 on `Wire` GP4/GP5):**
   - **Short Tap:** Toggles the gate state for the corresponding step on that voice.
   - **Step Edit Mode:** Long-pressing a step pad enters Step Edit mode for that specific step, routing parameter adjustments from the magnetic encoder or Alchemy faders into that step's automation memory.
   - **Real-Time Recording:** Touching step pads while holding parameter buttons records live parameter values into the pattern.

2. **Alchemy Tiles (Wire1 GP14/GP15 @ 100kHz):**
   - `SliderModule`: 4 motorized/analog faders + 4 Voice Select buttons (Voice 1–4).
   - `ButtonModule8`: 8 multi-function buttons (Param mode: Note, Velocity, Filter, Attack, Decay, Octave, Slide; Utility mode: Play/Stop, Delay, Scale, Swing, Theme, Encoder Target, Randomize; bit 7 is Shift in both modes).
   - `GP7 Mode Strap`: Hardware switch selecting Param mode (LOW) vs Utility mode (HIGH).

---

## Software API & Core Types (`src/matrix/Matrix.h`)

### Data Structures
```cpp
#define MATRIX_BUTTON_COUNT 32

typedef struct {
    uint8_t rowInput;    // MPR121 electrode for row (0-3)
    uint8_t colInput;    // MPR121 electrode for column (4-11)
} MatrixButton;

typedef enum {
    MATRIX_BUTTON_PRESSED,   // Pad touch detected
    MATRIX_BUTTON_RELEASED   // Pad release detected
} MatrixButtonEventType;

typedef struct {
    uint8_t buttonIndex;           // Linear index 0-31
    MatrixButtonEventType type;    // Press or release event
} MatrixButtonEvent;
```

### Core Functions
| Function | Signature | Description |
|---|---|---|
| `Matrix_init` | `void Matrix_init(Adafruit_MPR121 *sensor)` | Initializes matrix state and binds to the MPR121 instance |
| `Matrix_scan` | `void Matrix_scan()` | Scans electrode states, performs debouncing, and dispatches callbacks |
| `Matrix_getButtonState` | `bool Matrix_getButtonState(uint8_t idx)` | Queries current state of pad `idx` (0–31) |
| `Matrix_setEventHandler` | `void Matrix_setEventHandler(void (*handler)(const MatrixButtonEvent &))` | Sets general callback for press and release events |
| `Matrix_setRisingEdgeHandler` | `void Matrix_setRisingEdgeHandler(void (*handler)(uint8_t buttonIndex))` | Sets callback invoked only on touch press events |
| `Matrix_printState` | `void Matrix_printState()` | Prints 4×8 matrix debug state to Serial |

---

## Integration Workflow

```cpp
#include <Adafruit_MPR121.h>
#include "src/matrix/Matrix.h"

Adafruit_MPR121 capSensor;

void setup1() {
    Wire.setSDA(4);
    Wire.setSCL(5);
    Wire.begin();
    
    if (capSensor.begin(0x5A, &Wire)) {
        Matrix_init(&capSensor);
    }
    
    Matrix_setEventHandler([](const MatrixButtonEvent &evt) {
        if (evt.type == MATRIX_BUTTON_PRESSED) {
            handleStepPadPress(evt.buttonIndex);
        } else {
            handleStepPadRelease(evt.buttonIndex);
        }
    });
}

void loop1() {
    Matrix_scan(); // Polled non-blocking on Core 1
}
```

---

## Performance & Optimization

- **Early Exit:** If the MPR121 returns zero touched electrodes, `Matrix_scan()` exits immediately in O(1) time.
- **Debounced Transitions:** Software hysteresis ensures clean edge transitions without contact chatter.
- **Zero Allocations:** Uses static arrays for state tracking and event routing.

---

## File Structure

```
src/matrix/
├── Matrix.cpp          # Scanning engine and electrode-to-pad mapping
├── Matrix.h            # Matrix data types and API definitions
└── README.md           # Module overview
```

---

## Related Documentation

- [`docs/LEDMatrix.md`](LEDMatrix.md) — 8×8 WS2812B visual feedback system
- [`docs/sensors.md`](sensors.md) — TMAG5273 encoder and VL53L1X distance sensor
- [`docs/ButtonHandlers.md`](ButtonHandlers.md) — UI button event dispatching
- [`docs/superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md`](superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md) — Alchemy tile control surface design spec
- [`docs/architecture.md`](architecture.md) — System architecture and dual-core division