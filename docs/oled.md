# OLED Module Documentation

## Overview

The `src/OLED/` subsystem manages the 128×64 monochrome OLED display for Pico2Seq using an **Adafruit SH1106G** driver over I2C (`Wire` @ `0x3C`).

The OLED provides real-time visualization of parameter values, sequence lengths, settings sub-menus, voice presets, and system status through a deterministic **5-tier priority rendering hierarchy**.

---

## Hardware Configuration & Pinout

- **Display Controller:** SH1106G 128×64 Monochrome I2C OLED
- **Driver Library:** `Adafruit_SH1106G` (via `Adafruit_SH110X` / `Adafruit_GFX`)
- **Bus:** `Wire` (I2C0)
  - `SDA`: GP4
  - `SCL`: GP5
- **I2C Address:** `0x3C` (`OLEDConstants::I2C_ADDRESS`)
- **Reset Pin:** `-1` (unconnected / software reset)
- **Display Dimensions:** 128 pixels wide × 64 pixels high

---

## Priority-Based Screen Hierarchy

In `OLEDDisplay::update()`, the screen is updated by evaluating active states in a strict top-to-bottom priority hierarchy. Only the highest active priority view is rendered in any single frame:

```
+-------------------------------------------------------------------------+
| Priority 1: Transitory PARAM / UTIL Mode Strap Banner                  |
| (Active when millis() < uiState.alchemyModeBannerUntil)                |
+-------------------------------------------------------------------------+
                                    | (if expired)
                                    v
+-------------------------------------------------------------------------+
| Priority 2: Settings & Preset Management Screen                         |
| (Active when uiState.settingsMode == true)                              |
|   ├── SubMode VOICE_PARAMETER: Parameter toggles (Filter/Env/Drive)     |
|   └── SubMode PRESET_SELECTION: Preset browser & Sound Buffet 4-voice   |
+-------------------------------------------------------------------------+
                                    | (if inactive)
                                    v
+-------------------------------------------------------------------------+
| Priority 3: Gate Sequence Length Gauge                                  |
| (Active when uiState.gateSeqLengthMode == true - holding encoder)       |
+-------------------------------------------------------------------------+
                                    | (if inactive)
                                    v
+-------------------------------------------------------------------------+
| Priority 4: Parameter Editing Screens                                   |
|   ├── Held Parameter Button (heldParamId != ParamId::Count)             |
|   └── Step Edit Mode (uiState.selectedStepForEdit != -1)                |
+-------------------------------------------------------------------------+
                                    | (if inactive)
                                    v
+-------------------------------------------------------------------------+
| Priority 5: Default System Status Screen                                |
| (Scale name, Shuffle template, 0-based Voice index, Step indicators)    |
+-------------------------------------------------------------------------+
```

---

### Priority Screen Details

#### 1. Transitory Mode Strap Banner (Priority 1 — Highest)
Triggered for a brief timeout window whenever the hardware GP7 mode strap changes position:
- **PARAM Mode:** Displays centered size-3 **"PARAM"** with subtitle `> params <`.
- **UTIL Mode:** Displays centered size-3 **"UTIL"** with subtitle `> utility <`.

#### 2. Settings & Preset Menus (Priority 2)
Activated when `uiState.settingsMode` is true:
- **Voice Parameter Sub-Mode (`SettingsSubMode::VOICE_PARAMETER`):** Shows toggle states for voice architecture:
  - Envelope (ON/OFF)
  - Overdrive (ON/OFF)
  - Filter Mode (`LP12`, `LP24`, `LP36`, `BP12`, `BP24`)
  - Filter Resonance (%)
- **Preset Selection Sub-Mode (`SettingsSubMode::PRESET_SELECTION`):**
  - Reachable while the transport runs (long-press Play/Stop toggles settings; short-press while running inside settings exits without stopping).
  - Displays currently selected preset name centered in size-2 text.
  - Previous (`<`) and next (`>`) preset previews.
  - Preset counter (`1/15` through `15/15`; dynamic from `VoicePresets::getPresetCount()`).
  - All 15 presets are selectable on matrix pads 8–22 (`VoicePresets::presetIndexForPad`); the prompt line shows the live pad range (e.g. "Pads 8-22").
  - When browsing root settings, displays the **"Sound Buffet"** listing current presets assigned across all 4 voices (0–3).

The parameter name/value screens are preset-aware: for voices whose preset re-purposes the
Filter/Attack/Decay slots (`VoiceConfig::paramSet`), the OLED shows the slot's re-purposed
name (e.g. Bright/Pick/T60 on a waveguide voice, via `VoicePresets::getSequencerParamName`)
and formats the value in its own unit (%, seconds for T60, semitones for detune).

#### 3. Gate Sequence Length Gauge (Priority 3)
Activated when `uiState.gateSeqLengthMode` is active (holding the encoder while rotating):
- Header: `"Sequence Length"`
- Voice: `0..3` (0-based indexing)
- Length: Numeric sequence length (1–16) displayed in size-2 font.
- Visual Gauge: Proportional horizontal bar across the bottom displaying length relative to 16 steps.

#### 4. Parameter Edit Screen (Priority 4)
Displayed when a parameter button is held (`heldParamId`) or a step is selected for editing (`selectedStepForEdit`):
- **Header:** Parameter name (`Note`, `Velocity`, `Filter`, `Attack`, `Decay`, `Octave`, `GateLength`, `Slide`) in size-2 text.
- **Indicators:** Voice ID (`V0`–`V3`) and Step Index (`S1`–`S16`) in top right.
- **Separator:** Horizontal rule dividing header and value.
- **Formatted Value:** Large size-2 text showing human-readable units:
  - `Note`: Integer semitone
  - `Velocity`: `0%`–`100%`
  - `Filter`: Exponential frequency in Hz (`20Hz`–`20000Hz` via `rpdsp::fmap`)
  - `Attack` / `Decay`: Seconds with millisecond resolution (e.g. `0.250s`)
  - `Octave`: `-1`, `0`, `+1`
  - `GateLength`: `0%`–`100%`
  - `Gate` / `Slide`: `ON` / `OFF`
- **Progress Bar:** 10px tall bordered progress bar for continuous parameters (Velocity, Filter, Attack, Decay, GateLength).

#### 5. Default Status Screen (Priority 5 — Lowest)
Displayed when no transient, settings, or edit modes are active:
- **Scale:** Name of active musical scale (e.g., `Chromatic`, `Major`, `Minor`, `Dorian`, `Pentatonic Major`, etc.).
- **Shuffle:** Active shuffle template name (e.g., `No Shuffle`, `Classic 16th`, `Light Swing`).
- **Voice Index:** Active voice displayed in 0-based format (`Voice: 0` through `Voice: 3`) in large size-3 typography.
- **Step Indicators:** Real-time beat-synchronized dot playhead indicators across the bottom for the active voice sequencer.

---

## Layout & Geometry Constants (`LEDConstants.h` / `namespace OLEDConstants`)

```cpp
namespace OLEDConstants {
  static constexpr uint8_t I2C_ADDRESS = 0x3C;
  static constexpr uint8_t SCREEN_WIDTH = 128;
  static constexpr uint8_t SCREEN_HEIGHT = 64;
  static constexpr int8_t RESET_PIN = -1;
  
  // Animation Timing
  static constexpr uint32_t BORDER_ANIM_INTERVAL_MS = 80;
  static constexpr uint8_t BORDER_ANIM_PHASES = 8;
  static constexpr uint32_t STARTUP_WIPE_DELAY_MS = 12;
  static constexpr uint32_t STARTUP_BOUNCE_DELAY_MS = 20;
  static constexpr uint32_t STARTUP_SETTLE_DELAY_MS = 300;
  
  // Layout Metrics
  static constexpr uint8_t BORDER_THICKNESS = 1;
  static constexpr uint8_t TEXT_MARGIN = 5;
  static constexpr uint8_t LINE_SPACING = 10;
  static constexpr uint8_t HEADER_HEIGHT = 14;
  static constexpr uint8_t PROGRESS_BAR_HEIGHT = 10;
  static constexpr uint8_t STEP_INDICATOR_HEIGHT = 8;
}
```

---

## Software Architecture & Observer Interface

### `VoiceParameterObserver`
`OLEDDisplay` implements `VoiceParameterObserver` to receive instant notifications when voice parameters are modified from background tasks or external events:

```cpp
class VoiceParameterObserver {
public:
  virtual ~VoiceParameterObserver() = default;
  virtual void onVoiceParameterChanged(uint8_t voiceId, const VoiceState &state) = 0;
  virtual void onVoiceSwitched(uint8_t newVoiceId) = 0;
};
```

### Main Class Interface (`src/OLED/oled.h`)
```cpp
class OLEDDisplay : public VoiceParameterObserver {
public:
  OLEDDisplay();
  bool begin();
  void update(const UIState &uiState, const Sequencer &seq1, const Sequencer &seq2,
              const Sequencer &seq3, const Sequencer &seq4, VoiceManager *voiceManager);
  void clear();
  bool isInitialized() const;
  void setVoiceManager(VoiceManager *voiceManager);

  void onVoiceParameterChanged(uint8_t voiceId, const VoiceState &state) override;
  void onVoiceSwitched(uint8_t newVoiceId) override;
  void onVoiceSwitched(const UIState &uiState, VoiceManager *voiceManager);
  void forceUpdate(const UIState &uiState, VoiceManager *voiceManager);
};

extern OLEDDisplay oledDisplay;
```

---

## Concurrency & Performance

- **Core 0 Execution:** All OLED drawing, formatting, and I2C transmission occur on **Core 0** inside `loop()` at a dedicated 50 Hz frame rate (~20 ms interval).
- **Single-Frame Buffer:** Geometry and text operations write into Adafruit GFX's 1024-byte RAM buffer, followed by a single non-blocking `display()` burst over I2C.
- **Zero Heap Allocations:** Frame rendering avoids dynamic strings in the hot path, utilizing static buffers and integer math.

---

## File Structure

```
src/OLED/
├── oled.cpp          # SH1106G rendering pipeline and priority state machine
└── oled.h            # OLEDDisplay class and VoiceParameterObserver interface
```

---

## Related Documentation

- [`docs/LEDMatrix.md`](LEDMatrix.md) — 8×4 WS2812B RGB LED matrix visualizer
- [`docs/matrix.md`](matrix.md) — MPR121 32-pad touch input matrix
- [`docs/architecture.md`](architecture.md) — Dual-core architecture and UI thread loop
- [`docs/voice.md`](voice.md) — Voice parameters, presets, and VoiceManager architecture