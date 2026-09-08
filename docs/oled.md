# OLED Module Documentation

## Overview

The `src/OLED/` subsystem manages the 128×64 monochrome OLED display for Pico2Seq using an **Adafruit SH1106G** driver over I2C (`Wire` @ `0x3C`).

The OLED provides real-time visualization of parameter values, sequence lengths, settings sub-menus, voice presets, and system status through a deterministic **6-tier priority rendering hierarchy**.

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
- **I2C Clock:** the `Adafruit_SH1106G` object is constructed with
  `preclk=400000`, so every OLED frame push (`display()`) runs at 400 kHz fast
  mode — it always has. The *durable* bus clock is the `postclk` constructor
  argument, because `Adafruit_SH110X` re-programs `Wire` to `postclk` after
  each frame: `100000` by default, `400000` when `PICO2SEQ_I2C_FASTMODE=1`
  (see [`src/FeatureConfig.h`](../src/FeatureConfig.h)). The main `Wire` bus
  itself has **no** `setClock()` call by default (arduino-pico default speed);
  the opt-in flag adds `Wire.setClock(400000)` in
  `ControlIO::beginMainBusAndLeds()` to cover the window before the first
  frame push. Default is **OFF**: the 2026-09-07 bench run showed constant
  OLED glitches and freezes with 400 kHz idle/sensor traffic on this rig
  (matching the Wire1 tile-bus finding), so the flag exists to opt in per
  build, not to enable by default.

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
| Priority 2: Transitory Confirmation Notice                              |
| (Active when millis() < uiState.oledNoticeUntil: "DELAY ON"/"DELAY OFF"/|
|  "RANDOMIZED" + voice, replacing the old control-cluster LED flashes)   |
+-------------------------------------------------------------------------+
                                    | (if expired)
                                    v
+-------------------------------------------------------------------------+
| Priority 3: Settings & Preset Management Screen                         |
| (Active when uiState.settingsMode == true)                              |
|   ├── SubMode VOICE_PARAMETER: Parameter toggles (Filter/Env/Drive)     |
|   └── SubMode PRESET_SELECTION: Preset browser & Sound Buffet 4-voice   |
+-------------------------------------------------------------------------+
                                    | (if inactive)
                                    v
+-------------------------------------------------------------------------+
| Priority 4: Gate Sequence Length Gauge                                  |
| (Active when uiState.gateSeqLengthMode == true - holding encoder)       |
+-------------------------------------------------------------------------+
                                    | (if inactive)
                                    v
+-------------------------------------------------------------------------+
| Priority 5: Parameter Editing Screens                                   |
|   ├── Held Parameter Button (heldParamId != ParamId::Count)             |
|   └── Step Edit Mode (uiState.selectedStepForEdit != -1)                |
+-------------------------------------------------------------------------+
                                    | (if inactive)
                                    v
+-------------------------------------------------------------------------+
| Priority 6: Default System Status Screen                                |
| (Scale name, Shuffle template, 0-based Voice index, Step indicators)    |
+-------------------------------------------------------------------------+
```

---

### Priority Screen Details

#### 1. Transitory Mode Strap Banner (Priority 1 — Highest)
Triggered for a brief timeout window whenever the hardware GP7 mode strap changes position:
- **PARAM Mode:** Displays centered size-3 **"PARAM"** with subtitle `> params <`.
- **UTIL Mode:** Displays centered size-3 **"UTIL"** with subtitle `> utility <`.

#### 2. Transitory Confirmation Notice (Priority 2)
Shown for a short window after delay/randomize actions (replacing the old control-cluster LED flashes):
- `DELAY ON` / `DELAY OFF` in size-2 text, or `RANDOMIZED` with a `Voice N` sub-line (1-based).

#### 3. Settings & Preset Menus (Priority 3)
Activated when `uiState.settingsMode` is true:
- **Voice Parameter Sub-Mode (`SettingsSubMode::VOICE_PARAMETER`):** Shows toggle states for voice architecture:
  - Envelope (ON/OFF)
  - Overdrive (ON/OFF)
  - Filter Mode (`LP24`, `LP12`, `BP24`, `BP12`, `HP24`, `HP12`)
  - Filter Resonance (%)
- **Preset Selection Sub-Mode (`SettingsSubMode::PRESET_SELECTION`):**
  - Reachable while the transport runs (long-press Play/Stop toggles settings; short-press while running inside settings exits without stopping).
  - Displays currently selected preset name centered in size-2 text.
  - Preset counter (`#1/21` through `#21/21` for the current 21-preset bank; dynamic from `VoicePresets::getPresetCount()`).
  - Presets are paginated: `VoicePresets::kPresetsPerPage = 24` pads per page (pads 8–31, `VoicePresets::presetIndexForPad`), with `pad 6` = previous page and `pad 7` = next page (`kPreviousPagePad` / `kNextPagePad`). The prompt line shows the live pad range for the current page (`Pads 8-28` for the 21-preset bank, which fits on one page) plus `Page x/y 6< >7`.
  - When browsing root settings, displays the **"Sound Buffet"** listing current presets assigned across all 4 voices (0–3).

The parameter name/value screens are preset-aware: for voices whose preset re-purposes the
Filter/Attack/Decay slots (`VoiceConfig::paramSet`), the OLED shows the slot's re-purposed
name (e.g. Bright/Pick/T60 on a waveguide voice, via `VoicePresets::getSequencerParamName`)
and formats the value in its own unit (%, seconds for T60, semitones for detune).

#### 4. Gate Sequence Length Gauge (Priority 4)
Activated when `uiState.gateSeqLengthMode` is active (holding the encoder while rotating):
- Header: `"Sequence Length"`
- Voice: `0..3` (0-based indexing)
- Length: Numeric sequence length (1–16) displayed in size-2 font.
- Visual Gauge: Proportional horizontal bar across the bottom displaying length relative to 16 steps.

#### 5. Parameter Edit Screen (Priority 5)
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

#### 6. Default Status Screen (Priority 6 — Lowest)
Displayed when no transient, settings, or edit modes are active:
- **Scale:** Name of active musical scale (e.g., `Chromatic`, `Major`, `Minor`, `Dorian`, `Pentatonic Major`, etc.).
- **Shuffle:** Active shuffle template name (e.g., `No Shuffle`, `Classic 16th`, `Light Swing`).
- **Voice Index:** Active voice displayed in 0-based format (`Voice: 0` through `Voice: 3`) in large size-3 typography.
- **Step Indicators:** Real-time beat-synchronized dot playhead indicators across the bottom for the active voice sequencer.
- **Encoder Line:** `ENC: <parameter> <value>` appears while the magnetic encoder is controlling a parameter.

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

private:
  void forceUpdate(const UIState &uiState, VoiceManager *voiceManager);

  // Frame-shadow commit gate (see below)
  static constexpr uint16_t kFrameBytes =
      OLEDConstants::SCREEN_WIDTH * OLEDConstants::SCREEN_HEIGHT / 8; // 1024
  uint8_t frameShadow_[kFrameBytes];   // poisoned to 0xFF in the constructor
  uint32_t lastFramePushMs = 0;
  static constexpr uint32_t kForcedRefreshMs = 2000;
  void commitFrame();
};

extern OLEDDisplay oledDisplay;
```

---

## Frame-Shadow Commit Gate (`commitFrame()`)

Every view redraws the whole framebuffer after `clearDisplay()`, which resets
Adafruit's dirty-window tracking — the library's partial-update transfer never
engages and each `display()` costs a full ~1 KB I2C frame push, even when the
screen is static. That push was the single largest consumer of the Core-0 loop
budget, so `commitFrame()` gates every physical transfer:

1. `memcmp` the live framebuffer (`displayHardware.getBuffer()`) against
   `frameShadow_` (a copy of what the panel actually shows, `kFrameBytes` =
   128×64/8 = 1024 bytes).
2. If they match **and** less than `kForcedRefreshMs` (2000 ms) has passed
   since the last push, skip the I2C transfer entirely.
3. Otherwise call `display()` once, copy the buffer into `frameShadow_`, and
   stamp `lastFramePushMs`.

Two details make the gate safe:

- `frameShadow_` is `memset` to `0xFF` in the constructor, so the first
  `commitFrame()` after `begin()` always sees the freshly cleared buffer as
  "changed" and actually transfers it.
- **Self-heal forced refresh:** `display()` is fire-and-forget (no I2C
  ACK/error feedback), so if a push is corrupted at the wire level the shadow
  ends up matching the buffer while the panel shows garbage — and the gate
  would never re-push. A power glitch that resets the panel fails identically.
  The `kForcedRefreshMs` periodic re-push bounds either failure to a ~2 s
  visual artifact instead of a permanent freeze; the ~24 ms cost lands once
  per interval.

All drawing paths funnel through `commitFrame()` (including `clear()`), so
static screens cost zero I2C traffic.

---

## Concurrency & Performance

- **Core 0 Execution:** All OLED drawing, formatting, and I2C transmission occur on **Core 0** inside `ControlIO::refreshDisplays(nowMs)` (called from `Application::update()`, which is `loop()`) at a 20 ms interval (`kDisplayIntervalMs`, 50 frames/s). Boot/init runs in `ControlIO::beginDisplay()`; the voice-change observer is registered in `ControlIO::observeVoiceChanges()`.
- **Frame-Shadow Gate:** Unchanged frames skip the I2C push entirely (see above), so idle screens cost no bus time; only genuinely redrawn frames pay the ~1 KB transfer at the SH1106G's 400 kHz `preclk`.
- **Single-Frame Buffer:** Geometry and text operations write into Adafruit GFX's 1024-byte RAM buffer, followed by at most one non-blocking `display()` burst over I2C per refresh.
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