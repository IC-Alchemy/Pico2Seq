# LEDMatrix Module Documentation

## Overview

The `src/LEDMatrix/` subsystem provides comprehensive visual feedback for Pico2Seq through an 8×4 WS2812B RGB LED matrix (32 total LEDs) driven by the `FastLED` library on **GPIO pin 1**.

The panel **mirrors the touch matrix**: the MPR121 touch surface (`src/matrix/`) is 4 rows × 8 columns, and the LED panel is 8 wide × 4 tall, so **the same (band, step) always lands on the same physical index on both surfaces** — band 0 (the selected voice pair's low voice) at indices 0–15 (touch/LED rows 0–1) and band 1 (the pair's high voice) at indices 16–31 (touch/LED rows 2–3). All rendering goes through the host-tested `ControlSurface::LedLayout` helper (`src/ui/ControlSurfaceLogic.h`) so this alignment cannot drift.

> **Disambiguation Note:**
> - `docs/LEDMatrix.md` (this document) describes the **WS2812B 8×4 RGB LED visual output system** on GPIO 1 (`src/LEDMatrix/`).
> - [`docs/matrix.md`](matrix.md) describes the **MPR121 capacitive touch input matrix** providing 32 dedicated step pads on I2C `Wire` @ `0x5A` (`src/matrix/`).

---

## Hardware Configuration & Pinout

- **LED Type:** WS2812B Addressable RGB LEDs
- **Matrix Dimensions:** 8 columns × 4 rows (32 total LEDs)
- **Data Pin:** `GPIO 1` (`LEDConstants::MATRIX_DATA_PIN`)
- **Default Brightness:** `LEDConstants::DEFAULT_BRIGHTNESS` = 120 (on a 0–255 scale); the sketch initializes the matrix with `ledMatrix.begin(100)` in `setup1()`, so runtime brightness is 100.
- **Power Supply:** 5V rail capable of supplying up to ~1.5A for full-white illumination; internal brightness scaling is applied to limit peak current draw.

---

## Band Layout (pad-mirror)

```
                 cols 0 .. 7                    cols 0 .. 7
touch rows 0-1 = pads  0-15  (pair low voice)  LED rows 0-1 = indices  0-15
touch rows 2-3 = pads 16-31  (pair high voice) LED rows 2-3 = indices 16-31
```

Both bands always render the **selected** voice pair (`PadBank::pairFor(selectedVoiceIndex)`): voice 0/1 selected → voices 1+2, voice 2/3 selected → voices 3+4 (0-based internally).

---

## Architecture & Subsystem Components

```
                +-------------------------------------------------------+
                |                       UIState                         |
                |  (selectedVoiceIndex, modes, step selections, timers) |
                +--------------------------+----------------------------+
                                           |
                                           v
                              +----------------------+
                              |  LEDMatrixFeedback   |
                              | (step gates, play-   |
                              |  heads, menus, over- |
                              |  lays via LedLayout) |
                              +----------+-----------+
                                         |
                                         v
                              +-----------------------+
                              |       LEDMatrix       |
                              | (FastLED abstraction) |
                              +-----------+-----------+
                                          |
                                          v
                            [ WS2812B 8x4 Grid @ GP1 ]
```

Control indicators (parameter buttons, delay, voice pair, randomize) were moved off the matrix to the **OLED** (see below) — the panel is now 100% step-grid mirror.

### 1. `LEDConstants.h` & `LEDColors`

Centralized namespace declarations for timing, layout geometry, color categories, and brightness levels:

#### `namespace LEDConstants`
- **Matrix Geometry:** `MATRIX_WIDTH = 8`, `MATRIX_HEIGHT = 4`, `MATRIX_DATA_PIN = 1`, `MATRIX_TOTAL_LEDS = 32`, `DEFAULT_BRIGHTNESS = 120`.
- **Layout Offsets:**
  - `TOP_HALF_OFFSET = 0` (Band 0 start: pair low voice)
  - `BOTTOM_HALF_OFFSET = 16` (Band 1 start: pair high voice; touch rows 2–3)
  - `VOICE_PAIR_SEPARATION = 1` (Rows between pair bands in an 8×4 matrix)
  - `MAX_STEP_BUTTONS = 16`
- **Animation Timing:**
  - `PULSE_FREQUENCY = 0.006f`
  - `PULSE_BASE_BRIGHTNESS = 22`, `PULSE_AMPLITUDE = 188`
  - `BREATHING_CYCLE_MS = 2000`
  - `BLINK_INTERVAL_MS = 500`
  - `VOICE_PARAM_TIMEOUT_MS = 3000`
  - `VOICE_PARAM_SETTINGS_TIMEOUT_MS = 5000`
- **Color Blending Amounts:**
  - `TARGET_SMOOTHING_BLEND_AMOUNT = 180`
  - `STANDARD_BLEND_AMOUNT = 166`
  - `DIM_BLEND_AMOUNT = 122`
  - `FADE_BLEND_AMOUNT = 64`
  - `SUBTLE_BLEND_AMOUNT = 32`
- **Brightness Scaling Tiers:**
  - `FULL_BRIGHTNESS = 200`
  - `HIGH_BRIGHTNESS = 180`
  - `MEDIUM_BRIGHTNESS = 128`
  - `LOW_BRIGHTNESS = 64`
  - `DIM_BRIGHTNESS = 32`
  - `SUBTLE_BRIGHTNESS = 12`
- **Polyrhythmic Overlays:** `POLYRHYTHM_INTENSITY = 32`, `POLYRHYTHM_PARAM_COUNT = 3`.

#### `namespace LEDColors`
- **Standard:** `BLACK = CRGB::Black`, `WHITE = CRGB(66, 66, 66)` (dimmed white).
- **Delay Effects:** `DELAY_INDICATOR = CRGB(0, 166, 55)`, `DELAY_TIME_BASE = CRGB(0, 44, 33)`, `DELAY_FEEDBACK_BASE = CRGB(0, 55, 22)`.
- **Breathing Animation:** `BREATHING_BLUE_BASE = CRGB(0, 0, 94)`, `BREATHING_MIN_INTENSITY = 16`, `BREATHING_MAX_INTENSITY = 80`.
- **Polyrhythmic Track Accents:**
  - `POLYRHYTHM_NOTE = CRGB(0, 32, 66)` (Cyan tint)
  - `POLYRHYTHM_VELOCITY = CRGB(0, 66, 0)` (Green tint)
  - `POLYRHYTHM_FILTER = CRGB(0, 0, 66)` (Blue tint)

---

### 2. `LEDMatrix` Class (`ledMatrix.h`, `LEDmatrix.cpp`)

Provides the direct hardware abstraction wrapping `FastLED`:

```cpp
class LEDMatrix {
public:
  static constexpr uint8_t WIDTH = 8;
  static constexpr uint8_t HEIGHT = 4;
  static constexpr uint8_t NUM_LEDS = WIDTH * HEIGHT; // 32

  LEDMatrix();
  void begin(uint8_t brightness = LEDConstants::DEFAULT_BRIGHTNESS);
  void setLED(int x, int y, const CRGB& color);
  void setAll(const CRGB& color);
  void show();
  void clear();
  CRGB* getLeds();
};
```

---

### 3. Control Indicators → OLED (formerly `LEDController`)

The old 8×8 control-cluster LEDs (parameter button LEDs, delay time/feedback, voice pair indicators, randomize/delay-toggle flashes at indices 40–59) do not exist on the 8×4 panel; that code was removed. The same information now appears on the OLED:

- **Delay on/off and randomize confirmations** — transient full-screen notices (`DELAY ON` / `DELAY OFF` / `RANDOMIZED` + voice number), ~800 ms, rendered just below the PARAM/UTIL banner in `oled.cpp`, then the previous view resumes. Triggered from `handleControlButton(BUTTON_TOGGLE_DELAY)` and `handleRandomizeButton()` via the `UIState::oledNotice*` fields.
- **Encoder parameter** — while the TMAG5273 encoder controls a parameter (`uiState.currentEncoderParameter`), the default status screen shows `ENC: <parameter> <value>`, replacing the old value-fade LED.
- **Held parameter editing and voice selection** — were already OLED-covered (param info screen, status screen).

---

### 4. `LEDMatrixFeedback` (`LEDMatrixFeedback.h`, `LEDMatrixFeedback.cpp`)

Implements the multi-mode sequencing and navigation visualizer:

#### Display Modes
1. **Idle Breathing Mode:** When sequencers are stopped, renders a smooth pulsing breathing wave across the matrix using `BREATHING_BLUE_BASE`.
2. **Step Gate & Playhead Visualization:** Displays active gates for the current voice pair across band rows 0–1 (pair low voice) and 2–3 (pair high voice), with a distinct `playheadAccent` indicating the current 16th-note playhead position.
3. **Polyrhythmic Track Overlays:** Visualizes independent parameter track step lengths and positions for Note, Velocity, and Filter tracks.
4. **Parameter Edit Mode:** Shows step values, track lengths, and value adjustments when holding a parameter button or editing a step.
5. **Settings & Preset Selection:** Shows voice configurations and allows scrolling through the 15 voice presets with cursor highlighting.

#### 10 LED Color Themes (`enum class LEDTheme`)

Pico2Seq includes 10 fully realized color palettes selectable in settings or via utility controls:

| Theme Enum | Name | Visual Character |
|---|---|---|
| `DEFAULT` (0) | Standard | Classic Blue/Green palette |
| `OCEANIC` (1) | Oceanic | Deep ocean blues, cyan, and teal accents |
| `VOLCANIC` (2) | Volcanic | Intense reds, fiery oranges, and warm ambers |
| `FOREST` (3) | Forest | Earthy greens, moss, and warm brown highlights |
| `NEON` (4) | Neon | High-energy vibrant magenta, purple, and electric cyan |
| `MODERN` (5) | Modern | Refined muted tones optimized for high legibility |
| `DARK_NOCTIS` (6) | Dark Noctis | Low-light stealth theme with cool midnight blue accents |
| `DARK_EMBER` (7) | Dark Ember | Low-light theme with warm glowing ember highlights |
| `BLUE` (8) | Blue Contrast | High-contrast monochromatic blue palette |
| `GREEN` (9) | Green Contrast | High-contrast monochromatic green palette |

#### `LEDThemeColors` Structure
Each theme defines colors for:
- `gateOnV1`, `gateOffV1`, `gateOnV2`, `gateOffV2` (Gate states for voice pair)
- `playheadAccent`, `idleBreathingBlue` (Transport indicators)
- `editModeDimBlueV1`, `editModeDimBlueV2` (Step edit indicators)
- `modNoteActive`/`Inactive`, `modVelocityActive`/`Inactive`, `modFilterActive`/`Inactive`, `modDecayActive`/`Inactive`, `modAttackActive`/`Inactive`, `modOctaveActive`/`Inactive`, `modSlideActive`/`Inactive` (Parameter buttons)
- `defaultActive`, `defaultInactive`, `modParamModeActive`/`Inactive`, `modGateModeActive`/`Inactive` (System modes)
- `randomizeFlash`, `randomizeIdle` (Randomize button states)

---

## File Structure

```
src/LEDMatrix/
├── LEDConstants.h          # Hardware pins, timing, layout & OLED geometry constants
├── LEDmatrix.cpp           # FastLED driver wrapper implementation
├── ledMatrix.h             # LEDMatrix class definition
├── LEDMatrixFeedback.cpp   # Multi-mode step rendering & theme implementations
└── LEDMatrixFeedback.h     # Themes enum, LEDThemeColors & feedback API
```

---

## Performance & Concurrency Considerations

- **Dual-Core Execution:** All LED rendering occurs exclusively on **Core 0** (`loop()`) at a ~50 Hz (20 ms) update rate, leaving Core 1 dedicated to real-time 48 kHz audio processing.
- **Batching:** Frame changes are drawn into an internal buffer and updated to hardware with a single `ledMatrix.show()` call per frame.
- **Zero Heap Allocations:** All color calculations, blending tables, and state trackers use static memory.

---

## Related Documentation

- [`docs/matrix.md`](matrix.md) — MPR121 32-pad capacitive touch input matrix
- [`docs/oled.md`](oled.md) — 128×64 SH1106G OLED display subsystem
- [`docs/architecture.md`](architecture.md) — System architecture and dual-core split
- [`docs/voice.md`](voice.md) — Synthesizer voice DSP and preset definitions