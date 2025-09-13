# LEDMatrix (src/LEDMatrix)

Overview

This folder contains the LED matrix driver, controller and feedback systems used by the Pico2Seq project. It manages the 16x16 WS2812B LED matrix hardware, color themes, animations and high-level UI feedback.

Files

- [`src/LEDMatrix/LEDConstants.h:1`](src/LEDMatrix/LEDConstants.h:1) — Matrix and OLED constants (sizes, colors, timing).
- [`src/LEDMatrix/ledMatrix.h:1`](src/LEDMatrix/ledMatrix.h:1) — `LEDMatrix` class declaration and hardware mapping.
- [`src/LEDMatrix/LEDmatrix.cpp:1`](src/LEDMatrix/LEDmatrix.cpp:1) — `LEDMatrix` implementation (FastLED integration).
- [`src/LEDMatrix/LEDMatrixFeedback.h:1`](src/LEDMatrix/LEDMatrixFeedback.h:1) — High-level feedback API, themes and helper declarations.
- [`src/LEDMatrix/LEDMatrixFeedback.cpp:1`](src/LEDMatrix/LEDMatrixFeedback.cpp:1) — Feedback implementation, themes, palettes, overlays.
- [`src/LEDMatrix/LEDController.h:1`](src/LEDMatrix/LEDController.h:1) — Control-LED indices and controller API.
- [`src/LEDMatrix/LEDController.cpp:1`](src/LEDMatrix/LEDController.cpp:1) — Controller implementation (parameter LEDs, AS5600 visualisation).

Module responsibilities

- Low-level hardware abstraction for the LED panel.
- Coordinate mapping and hardware-order handling (columns-major, zigzag).
- Theme-driven color management and multiple palettes.
- Step / gate / playhead visualisation for sequencers.
- Control LED handling (parameter buttons, mode indicators).
- Settings and voice-parameter display modes.
- Lightweight animation helpers (breathing, pulsing, blending).

Public API (quick reference)

- [`LEDMatrix` class](src/LEDMatrix/ledMatrix.h:15)
  - [`LEDMatrix::LEDMatrix()`](src/LEDMatrix/ledMatrix.h:26) — Constructor (clears buffer).
  - [`LEDMatrix::begin(uint8_t)`](src/LEDMatrix/ledMatrix.h:32) — Initialize FastLED and hardware.
  - [`LEDMatrix::setLED(int,int,const CRGB&)`](src/LEDMatrix/ledMatrix.h:40) — Set a single pixel by (x,y).
  - [`LEDMatrix::getLED(int,int) const`](src/LEDMatrix/ledMatrix.h:45) — Read pixel color (safe bounds).
  - [`LEDMatrix::setAll(const CRGB&)`](src/LEDMatrix/ledMatrix.h:51) — Fill entire buffer.
  - [`LEDMatrix::pixelAt(int,int)`](src/LEDMatrix/ledMatrix.h:56) — Fast reference to internal pixel.
  - [`LEDMatrix::show()`](src/LEDMatrix/ledMatrix.h:61) — Push buffer to LEDs.
  - [`LEDMatrix::clear()`](src/LEDMatrix/ledMatrix.h:66) — Clear to black.
  - [`LEDMatrix::getLeds()`](src/LEDMatrix/ledMatrix.h:72) — Direct hardware-order array access.

- Feedback and themes
  - [`setupLEDMatrixFeedback()`](src/LEDMatrix/LEDMatrixFeedback.h:103) — Initialize smoothed color buffers.
  - [`updateStepLEDs(...)`](src/LEDMatrix/LEDMatrixFeedback.h:123) — Main per-frame update for step/sequence visuals.
  - [`updateSettingsModeLEDs(...)`](src/LEDMatrix/LEDMatrixFeedback.h:143) — Settings menu UI on the matrix.
  - [`updateVoiceParameterLEDs(...)`](src/LEDMatrix/LEDMatrixFeedback.h:154) — Voice parameter editing visual feedback.
  - [`setLEDTheme(LEDTheme)`](src/LEDMatrix/LEDMatrixFeedback.h:167) — Change active color theme.
  - [`getActiveThemeColors()`](src/LEDMatrix/LEDMatrixFeedback.h:177) — Inspect active theme palette.

- Control LEDs
  - [`initLEDController()`](src/LEDMatrix/LEDController.h:63) — (Extension point) initialize control LED subsystem.
  - [`updateControlLEDs(LEDMatrix&, const UIState&)`](src/LEDMatrix/LEDController.h:76) — Update top-row control LEDs (parameter buttons, toggles).
  - Control LED indices are defined in [`ControlLEDIndices`](src/LEDMatrix/LEDController.h:28).

Important constants & layout

- Matrix dimensions and pin:
  - [`LEDConstants::MATRIX_WIDTH` / `MATRIX_HEIGHT`](src/LEDMatrix/LEDConstants.h:15) — Default 16x16.
  - [`LEDConstants::MATRIX_DATA_PIN`](src/LEDMatrix/LEDConstants.h:17) — Default DATA pin (1).
  - [`LEDConstants::MATRIX_TOTAL_LEDS`](src/LEDMatrix/LEDConstants.h:18)
  - Default brightness: [`LEDConstants::DEFAULT_BRIGHTNESS`](src/LEDMatrix/LEDConstants.h:19).

- Coordinate mapping (hardware order):
  - Columns-major + zigzag mapping is implemented by [`LEDMatrix::indexForXY()`](src/LEDMatrix/ledMatrix.h:76) and used by [`LEDMatrix::setLED()`](src/LEDMatrix/LEDmatrix.cpp:33).
  - Mapping summary: column 0 is top→bottom, column 1 bottom→top, column 2 top→bottom, etc. This matches typical WS2812 matrices wired in vertical strips.

Themes, palettes and color constants

- Per-theme color definitions live in the implementation [`src/LEDMatrix/LEDMatrixFeedback.cpp:59`](src/LEDMatrix/LEDMatrixFeedback.cpp:59) — see the `ALL_THEMES` array.
- Theme enum: [`LEDTheme`](src/LEDMatrix/LEDMatrixFeedback.h:31).
- Theme color structure: [`LEDThemeColors`](src/LEDMatrix/LEDMatrixFeedback.h:52).
- Global/small palettes:
  - [`parameterPalette` / `parameterColors`](src/LEDMatrix/LEDMatrixFeedback.cpp:254) — Gradient palette used for parameter coloring.
- Common reusable color constants are declared in [`LEDConstants.h`](src/LEDMatrix/LEDConstants.h:56).

Usage examples

Minimal initialization (pseudo-code):

```cpp
#include "ledMatrix.h"
#include "LEDMatrixFeedback.h"

LEDMatrix matrix;

void setup() {
    matrix.begin(LEDConstants::DEFAULT_BRIGHTNESS); // [`LEDMatrix::begin()`](src/LEDMatrix/ledMatrix.h:32)
    setupLEDMatrixFeedback();                       // [`setupLEDMatrixFeedback()`](src/LEDMatrix/LEDMatrixFeedback.h:103)
}

void loop() {
    // Call into higher level update functions once per frame/tick
    // e.g. updateStepLEDs(matrix, seq1, seq2, seq3, seq4, uiState, 0);
    matrix.show(); // [`LEDMatrix::show()`](src/LEDMatrix/ledMatrix.h:61)
}
```

Using control LED updates (from the UI loop):

```cpp
// [`updateControlLEDs()`](src/LEDMatrix/LEDController.h:76) expects the central UI state.
updateControlLEDs(matrix, uiState);
matrix.show();
```

Coordinate & index helpers

- The helper `ControlLEDIndices::idx(x,y)` is defined in [`LEDController.h`](src/LEDMatrix/LEDController.h:31) and produces the linear index into the matrix buffer using the same column-major logic.
- To convert a linear index to (x,y) for high-level operations, use:
  - x = index % [`LEDMatrix::WIDTH`](src/LEDMatrix/ledMatrix.h:18)
  - y = index / [`LEDMatrix::WIDTH`](src/LEDMatrix/ledMatrix.h:18)
  (This pattern is used across the controller and feedback code.)

Implementation notes & caveats

- Hardware ordering: The internal buffer uses a hardware-order layout (columns-major, zigzag). Avoid directly iterating x,y without using the provided helpers (`indexForXY`, `pixelAt`, `getLeds`).
- Performance:
  - The project runs audio on Core0 and UI on Core1 (see repository notes). Keep LED update code short and non-blocking to avoid UI scheduling issues. Avoid long CPU-bound operations inside `updateStepLEDs` or `updateControlLEDs`.
  - Use the `smoothedTargetColorBuffer` and palette blending helpers in [`LEDMatrixFeedback.cpp`](src/LEDMatrix/LEDMatrixFeedback.cpp:24) for smooth visuals without heavy computation every frame.
- AS5600 encoder integration:
  - The control LED code references the external AS5600 sensor via `as5600Sensor` and `getAS5600ParameterValue()` in [`LEDController.cpp`](src/LEDMatrix/LEDController.cpp:4). If AS5600 is not present, the code falls back to base colors.
- Themes:
  - Changing themes via [`setLEDTheme()`](src/LEDMatrix/LEDMatrixFeedback.h:174) swaps the active palette (see [`getActiveThemeColors()`](src/LEDMatrix/LEDMatrixFeedback.h:177)).
- OLED interaction:
  - OLED constants for an onboard display are provided in [`LEDConstants.h`](src/LEDMatrix/LEDConstants.h:77). The matrix module doesn't drive the OLED directly but provides shared layout/timing constants.

Testing and debugging tips

- To verify hardware mapping, set a distinct color per column using `pixelAt(x,y)` and observe the physical ordering.
- Use `FastLED.show()` sparingly in loops; batch updates and call `show()` once after all per-frame writes.
- The repository includes a `Debug` utility referenced by the feedback code — use it to log state when troubleshooting (`src/utils/Debug.h`).

Extensibility guide

- Adding a new theme:
  1. Add a new `LEDTheme` enum value in [`LEDMatrixFeedback.h:31`](src/LEDMatrix/LEDMatrixFeedback.h:31).
  2. Append a `LEDThemeColors` entry to `ALL_THEMES` in [`LEDMatrixFeedback.cpp:59`](src/LEDMatrix/LEDMatrixFeedback.cpp:59).
  3. Use [`setLEDTheme()`](src/LEDMatrix/LEDMatrixFeedback.h:174) to activate.

- Supporting different matrix sizes:
  - Update [`LEDConstants::MATRIX_WIDTH` / `MATRIX_HEIGHT`](src/LEDMatrix/LEDConstants.h:15) and verify hardware mapping assumptions in [`ledMatrix.h:75`](src/LEDMatrix/ledMatrix.h:75) and controller index placements in [`LEDController.h:33`](src/LEDMatrix/LEDController.h:33).
  - Be cautious: control indices and UI layouts are tuned for 16x16 and may require re-layout for larger matrices.

Where to look next

- Control LED logic: [`src/LEDMatrix/LEDController.cpp:1`](src/LEDMatrix/LEDController.cpp:1)
- Feedback implementation and themes: [`src/LEDMatrix/LEDMatrixFeedback.cpp:1`](src/LEDMatrix/LEDMatrixFeedback.cpp:1)
- Core LED abstraction: [`src/LEDMatrix/LEDmatrix.cpp:1`](src/LEDMatrix/LEDmatrix.cpp:1) and [`src/LEDMatrix/ledMatrix.h:1`](src/LEDMatrix/ledMatrix.h:1)
- Central constants: [`src/LEDMatrix/LEDConstants.h:1`](src/LEDMatrix/LEDConstants.h:1)

License & attribution

This module is part of the Pico2Seq project and follows the repository license (see root [`LICENSE`](LICENSE:1)).

-- End of file