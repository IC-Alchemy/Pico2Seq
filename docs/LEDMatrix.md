# LEDMatrix Module Documentation

## Overview

The `src/LEDMatrix/` subsystem provides comprehensive visual feedback for Pico2Seq through an 8×8 WS2812B RGB LED matrix (64 total LEDs) driven by the `FastLED` library on **GPIO pin 1**.

> **Disambiguation Note:**
> - `docs/LEDMatrix.md` (this document) describes the **WS2812B 8×8 RGB LED visual output system** on GPIO 1 (`src/LEDMatrix/`).
> - [`docs/matrix.md`](matrix.md) describes the **MPR121 capacitive touch input matrix** providing 32 dedicated step pads on I2C `Wire` @ `0x5A` (`src/matrix/`).

---

## Hardware Configuration & Pinout

- **LED Type:** WS2812B Addressable RGB LEDs
- **Matrix Dimensions:** 8 columns × 8 rows (64 total LEDs)
- **Data Pin:** `GPIO 1` (`LEDConstants::MATRIX_DATA_PIN`)
- **Default Brightness:** 120 (on a 0–255 scale)
- **Power Supply:** 5V rail capable of supplying up to ~1.5A for full-white illumination; internal brightness scaling is applied to limit peak current draw.

---

## Architecture & Subsystem Components

```
                +-------------------------------------------------------+
                |                       UIState                         |
                |  (selectedVoiceIndex, modes, step selections, timers) |
                +--------------------------+----------------------------+
                                           |
                    +----------------------+---------------------+
                    |                                            |
                    v                                            v
         +--------------------+                       +--------------------+
         |   LEDController    |                       | LEDMatrixFeedback  |
         | (Parameter & Voice |                       | (Step gates, play- |
         |  indicators)       |                       |  heads, menus)     |
         +----------+---------+                       +----------+---------+
                    |                                            |
                    +----------------------+---------------------+
                                           |
                                           v
                               +-----------------------+
                               |       LEDMatrix       |
                               | (FastLED abstraction) |
                               +-----------+-----------+
                                           |
                                           v
                             [ WS2812B 8x8 Grid @ GP1 ]
```

### 1. `LEDConstants.h` & `LEDColors`

Centralized namespace declarations for timing, layout geometry, color categories, and brightness levels:

#### `namespace LEDConstants`
- **Matrix Geometry:** `MATRIX_WIDTH = 8`, `MATRIX_HEIGHT = 8`, `MATRIX_DATA_PIN = 1`, `MATRIX_TOTAL_LEDS = 64`, `DEFAULT_BRIGHTNESS = 120`.
- **Layout Offsets:**
  - `TOP_HALF_OFFSET = 0` (Row 0: low voice step row)
  - `BOTTOM_HALF_OFFSET = 24` (Row 4: high voice step row in 8×8 matrix)
  - `VOICE_PAIR_SEPARATION = 3` (Rows between voice pair tracks)
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
- **Standard:** `BLACK = CRGB::Black`, `WHITE = CRGB::White`.
- **Delay Effects:** `DELAY_INDICATOR = CRGB(0, 166, 55)`, `DELAY_TIME_BASE = CRGB(0, 44, 33)`, `DELAY_FEEDBACK_BASE = CRGB(0, 55, 22)`.
- **Breathing Animation:** `BREATHING_BLUE_BASE = CRGB(0, 0, 94)`, `BREATHING_MIN_INTENSITY = 16`, `BREATHING_MAX_INTENSITY = 80`.
- **Polyrhythmic Track Accents:**
  - `POLYRHYTHM_NOTE = CRGB(0, 32, 32)` (Cyan tint)
  - `POLYRHYTHM_VELOCITY = CRGB(0, 32, 0)` (Green tint)
  - `POLYRHYTHM_FILTER = CRGB(0, 0, 32)` (Blue tint)

---

### 2. `LEDMatrix` Class (`ledMatrix.h`, `LEDmatrix.cpp`)

Provides the direct hardware abstraction wrapping `FastLED`:

```cpp
class LEDMatrix {
public:
  static constexpr uint8_t WIDTH = 8;
  static constexpr uint8_t HEIGHT = 8;
  static constexpr uint8_t NUM_LEDS = WIDTH * HEIGHT; // 64

  LEDMatrix();
  void begin(uint8_t brightness = LEDConstants::DEFAULT_BRIGHTNESS);
  void setLED(uint8_t x, uint8_t y, const CRGB& color);
  void setAll(const CRGB& color);
  void show();
  void clear();
  CRGB* getLeds();
};
```

---

### 3. `LEDController` (`LEDController.h`, `LEDController.cpp`)

Manages the visual state of control and indicator LEDs on the matrix surface.

#### 4-Voice Pair Indicator Logic
The physical control surface provides two voice LEDs (`VOICE1_LED_INDEX` and `VOICE2_LED_INDEX`). In Pico2Seq's 4-voice architecture, these two LEDs dynamically reflect the currently active voice pair (Voices 0 & 1 vs. Voices 2 & 3):

```cpp
// Check if the selected voice is the first voice in its pair (Voice 0 or Voice 2)
const bool selectedIsFirstInPair = (uiState.selectedVoiceIndex % 2) == 0;
const CRGB selectedVoiceLEDColor = activeThemeColors->defaultActive;
const CRGB partnerVoiceLEDColor = activeThemeColors->defaultInactive;

setLEDByLinearIndex(ControlLEDIndices::VOICE1_LED_INDEX,
                    selectedIsFirstInPair ? selectedVoiceLEDColor : partnerVoiceLEDColor);
setLEDByLinearIndex(ControlLEDIndices::VOICE2_LED_INDEX,
                    selectedIsFirstInPair ? partnerVoiceLEDColor : selectedVoiceLEDColor);
```

- **Voice 0 Selected:** Voice 1 LED is bright (active), Voice 2 LED is dim (partner).
- **Voice 1 Selected:** Voice 1 LED is dim, Voice 2 LED is bright (active).
- **Voice 2 Selected:** Voice 1 LED is bright (active), Voice 2 LED is dim (partner).
- **Voice 3 Selected:** Voice 1 LED is dim, Voice 2 LED is bright (active).

#### Parameter & Sensor Feedback
- **Held Parameter Buttons:** Pulse dynamically using sine modulation (`PULSE_BASE_BRIGHTNESS + sinf(...) * PULSE_AMPLITUDE`).
- **TMAG5273 Magnetic Encoder Assignment:** When an encoder parameter is selected (`Velocity`, `Filter`, `Attack`, `Decay`), the corresponding button LED scales brightness proportionally to the current normalized parameter value.
- **Delay Controls:** Delay Time and Feedback LEDs scale with encoder values when selected.
- **Flashing Indicators:** Randomize and Delay Toggle buttons support timed flash animations (`flash23Until`, `flash31Until`).

---

### 4. `LEDMatrixFeedback` (`LEDMatrixFeedback.h`, `LEDMatrixFeedback.cpp`)

Implements the multi-mode sequencing and navigation visualizer:

#### Display Modes
1. **Idle Breathing Mode:** When sequencers are stopped, renders a smooth pulsing breathing wave across the matrix using `BREATHING_BLUE_BASE`.
2. **Step Gate & Playhead Visualization:** Displays active gates for the current voice pair across rows 0–1 and 4–5, with a distinct `playheadAccent` indicating the current 16th-note playhead position.
3. **Polyrhythmic Track Overlays:** Visualizes independent parameter track step lengths and positions for Note, Velocity, and Filter tracks.
4. **Parameter Edit Mode:** Shows step values, track lengths, and value adjustments when holding a parameter button or editing a step.
5. **Settings & Preset Selection:** Shows voice configurations and allows scrolling through the 7 voice presets with cursor highlighting.

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
├── LEDController.cpp       # Control LED mapping, pulsing & pair-based voice indicators
├── LEDController.h         # Control LED public interface
├── LEDmatrix.cpp           # FastLED driver wrapper implementation
├── ledMatrix.h             # LEDMatrix class definition
├── LEDMatrixFeedback.cpp   # Multi-mode step rendering & theme implementations
└── LEDMatrixFeedback.h     # Themes enum, LEDThemeColors & feedback API
```

---

## Performance & Concurrency Considerations

- **Dual-Core Execution:** All LED rendering occurs exclusively on **Core 1** (`loop1()`) at a ~50 Hz (20 ms) update rate, leaving Core 0 dedicated to real-time 48 kHz audio processing.
- **Batching:** Frame changes are drawn into an internal buffer and updated to hardware with a single `ledMatrix.show()` call per frame.
- **Zero Heap Allocations:** All color calculations, blending tables, and state trackers use static memory.

---

## Related Documentation

- [`docs/matrix.md`](matrix.md) — MPR121 32-pad capacitive touch input matrix
- [`docs/oled.md`](oled.md) — 128×64 SH1106G OLED display subsystem
- [`docs/architecture.md`](architecture.md) — System architecture and dual-core split
- [`docs/voice.md`](voice.md) — Synthesizer voice DSP and preset definitions