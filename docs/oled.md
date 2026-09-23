# OLED Module Documentation

## Arpeggiator play page

[Arpeggiator mode](arpeggiator.md#oled) has an eight-row play page with a
persistent preset band, transport/rate/latch status, scale-key summary,
hit/rest strip, tempo, gate duration and swing ratio. Long chords use a `+N`
summary; long names end in `~`. Shift reveals the rhythm controls. Recent
control movement changes only the middle rows for 1.4 seconds. Formatting in
`src/ui/ArpDisplay.h` is shared with host checks; hardware rendering remains in
`OLEDDisplay::displayArpPage()`.

## Overview

The `src/OLED/` subsystem manages the 128×64 monochrome OLED display for Pico2Seq using an **Adafruit SH1106G** driver over I2C (`Wire`, I2C0 @ 400 kHz, address `0x3C`).

The OLED provides real-time visualization of parameter values, sequence lengths, settings sub-menus, voice presets, and system status through a deterministic **7-tier priority rendering hierarchy**.

---

## Hardware Configuration & Pinout

- **Display Controller:** SH1106G 128×64 Monochrome I2C OLED
- **Driver Library:** `Adafruit_SH1106G` (via `Adafruit_SH110X` / `Adafruit_GFX`)
- **Bus:** `Wire` (I2C0 @ 400 kHz, shared with the touch pads, encoder and distance sensor)
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
| Priority 1: Voice Editing Screen                                        |
| (Active when uiState.voiceEditor.active — "EDIT V1".."EDIT V4")         |
+-------------------------------------------------------------------------+
                                    | (if inactive)
                                    v
+-------------------------------------------------------------------------+
| Priority 2: Transitory PARAM / UTIL Mode Strap Banner                  |
| (Active when millis() < uiState.alchemyModeBannerUntil)                |
+-------------------------------------------------------------------------+
                                    | (if expired)
                                    v
+-------------------------------------------------------------------------+
| Priority 3: Transitory Confirmation Notice                              |
| (Active when millis() < uiState.oledNoticeUntil: "RANDOMIZED" + voice,  |
|  "CLEARED" + voice, "ALL CLEAR", "SAVED"/"LOADED"/"LOAD ERR";           |
|  replacing the old control-cluster LED flashes)                         |
+-------------------------------------------------------------------------+
                                    | (if expired)
                                    v
+-------------------------------------------------------------------------+
| Priority 3b: Held Parameter Button (no step selected)                   |
| (heldParamId != ParamId::Count: live value + distance sensor mm)        |
+-------------------------------------------------------------------------+
                                    | (if none held)
                                    v
+-------------------------------------------------------------------------+
| Priority 4: Settings & Preset Management Screen                         |
| (Active when uiState.settingsMode == true)                              |
|   ├── SubMode VOICE_PARAMETER: Parameter toggles (Filter/Env/Drive)     |
|   └── SubMode PRESET_SELECTION: Preset browser & Sound Buffet 4-voice   |
+-------------------------------------------------------------------------+
                                    | (if inactive)
                                    v
+-------------------------------------------------------------------------+
| Priority 5: Gate Sequence Length Gauge                                  |
| (Active when uiState.gateSeqLengthMode == true - holding encoder)       |
+-------------------------------------------------------------------------+
                                    | (if inactive)
                                    v
+-------------------------------------------------------------------------+
| Priority 6: Step Edit Screen                                            |
|   └── Step Edit Mode (uiState.selectedStepForEdit != -1)                |
+-------------------------------------------------------------------------+
                                    | (if inactive)
                                    v
+-------------------------------------------------------------------------+
| Priority 7: Default System Status Screen                                |
| (Preset, BPM, encoder target's value or base, scale, step indicators)   |
+-------------------------------------------------------------------------+
```

---

### Priority Screen Details

#### 1. Voice Editing Screen (Priority 1 — Highest)
Active while `uiState.voiceEditor.active` (entered with Shift + slider button 4). Overrides
every other view until the editor exits:
- Header: `EDIT V1` through `EDIT V4` (1-based) with a `*` modified marker when the voice's
  parameter cursor has pending unsaved edits.
- Body: the selected parameter's name and formatted value/units, filtered by the active
  engine and enabled processors.
- Sequence-lane indicator: shows when a sequencer lane modifies the displayed base.
- Control guide (toggled with editor button 7): `ENC edit 7 Help 8 Exit`, or
  `FINE    7 Help 8 Exit` while fine adjustment is held.

#### 2. Transitory Mode Strap Banner (Priority 2)
Triggered for a brief timeout window whenever the hardware GP7 mode strap changes position:
- **PARAM Mode:** Displays centered size-3 **"PARAM"** with subtitle `> params <`.
- **UTIL Mode:** Displays centered size-3 **"UTIL"** with subtitle `> utility <`.

#### 3. Transitory Confirmation Notice (Priority 3)
Shown for a short window after randomize/save/load/clear actions (replacing the old control-cluster LED flashes):
- `RANDOMIZED` with a `Voice N` sub-line (1-based). *(The `DELAY ON`/`DELAY OFF` notices were
  removed with the delay effect, 2026-09-11.)*
- `CLEARED` with a `Voice N` sub-line after Shift + Randomize tap clears that voice;
  `ALL CLEAR` after Shift + Randomize long-press clears every voice (added 2026-09-16).
- `SAVED` / `LOADED` after a session save or restore; `LOAD ERR` when storage fails
  (also used for a failed save).

#### 4. Settings & Preset Menus (Priority 4)
Activated when `uiState.settingsMode` is true:
- **Voice Parameter Sub-Mode (`SettingsSubMode::VOICE_PARAMETER`):** Shows toggle states for voice architecture:
  - Envelope (ON/OFF)
  - Overdrive (ON/OFF)
  - Filter Mode (`LP24`, `LP12`, `BP24`, `BP12`, `HP24`, `HP12`)
  - Filter Resonance (%)
- **Preset Selection Sub-Mode (`SettingsSubMode::PRESET_SELECTION`):**
  - Reachable while the transport runs (long-press Play/Stop toggles settings; short-press while running inside settings exits without stopping).
  - Displays currently selected preset name centered in size-2 (or size-1 if name exceeds 10 chars) text.
  - Animated underline indicator.
  - Preset counter (`#1/29` through `#29/29`; dynamic from `VoicePresets::getPresetCount()`).
  - Pad range (`Pads 0-28`): pad N applies preset N+1 (1-based on screen). All presets share one page; pads 0–30 are reserved for presets and pad 31 is unassigned.
  - Hint line `V1-V4 select voice`: only the voice buttons change the target voice.
  - When browsing root settings, displays the **"Sound Buffet"** listing current presets assigned across all 4 voices (0–3).

The parameter name/value screens are preset-aware: for voices whose preset re-purposes the
Filter/Attack/Decay slots (`VoiceConfig::paramSet`), the OLED shows the slot's re-purposed
name (e.g. Bright/Pick/T60 on a waveguide voice, via `VoicePresets::getSequencerParamName`)
and formats the value in its own unit (%, seconds for T60, semitones for detune) via `MusicalValues::format`.

#### 5. Gate Sequence Length Gauge (Priority 5)
Activated when `uiState.gateSeqLengthMode` is active (holding the encoder while rotating):
- Header: `"Sequence Length"`
- Voice: `1..4` (1-based display)
- Length: Numeric sequence length (1–64) displayed in size-2 font.
- Visual Gauge: Proportional horizontal bar across the bottom displaying length relative to 64 steps.

#### 6. Parameter Edit Screen (Priority 3b when held, 6 in Step Edit)
Displayed when a parameter button is held (`heldParamId`) or a step is selected for editing (`selectedStepForEdit`).
A held parameter shows the composed value at that lane's playing cursor (`getPlaybackStep()`), which is
the value live recording writes and the voice plays; it outranks the settings and sequence-length screens.
In Step Edit a held parameter shows the selected step's value; with a toggled parameter (and no ENV
fader moved in the last 1.5 s) the screen shows that parameter — the same one the encoder edits.

**ENV page (Step Edit with nothing held or toggled, and for 1.5 s after an ENV fader move,
`OLEDDisplay::displayEnvelopePage()`):** the selected step's four envelope lanes — the ENV faders —
one per row: Attack, Decay, Sustain, Release (or the engine's names for them, e.g. Pick / T60 /
Position / Stiffness on strings). A value in parentheses follows the patch; a bare value is the
step's own. `>` marks the lane a fader last moved (`uiState.envFaderLane`). The footer reads
`()=patch` and names the lane the encoder edits (`Enc:Velocity`). It replaced the old
"Step N / Hold parameter to edit this step" screen, which hid fader and encoder edits.
- **Distance:** while a parameter button is held, the current VL53L1X reading in mm at the right of the
  `LIVE`/`STEP` line: `412mm` inside the recording window, `(812mm)` outside it (nothing recorded), `--mm`
  with no measurement.
- **Header:** Parameter name (`Note`, `Velocity`, `Filter`, `Attack`, `Decay`, `Octave`, `GateLength`, `Slide`) in size-2 text.
- **Indicators:** Voice ID (`V0`–`V3`) and Step Index (`S1`–`S16`) in top right.
- **Separator:** Horizontal rule dividing header and value.
- **Formatted Value:** Rendered via `MusicalValues::format` with context-aware units:
  - `Note`: Pitch note name with octave (e.g. `C3`) derived from active scale
  - `Velocity`: `0%`–`100%`
  - `Filter`: Frequency in Hz (`20Hz`–`20000Hz` via `rpdsp::fmap`) or repurposed name
  - `Attack` / `Decay`: Milliseconds or seconds (e.g. `250ms`, `1.20s`)
  - `Octave`: `-1`, `0`, `+1`
  - `GateLength`: `0%`–`100%`
  - `Gate` / `Slide`: `ON` / `OFF`
- **Progress Bar:** 10px tall bordered progress bar for continuous parameters (Velocity, Filter, Attack, Decay, GateLength).

#### 7. Default Status Screen (Priority 7 — Lowest)
Displayed when no transient, settings, or edit modes are active. Its value line shows the encoder
target's composed value at the playing step; for 1.5 s after an encoder turn
(`uiState.encoderBaseViewUntil`) it shows that target's **base** instead (`MusicalValues::baseStep()`,
labelled `Base`), so the edit is visible even where a step's own value would hide it:
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

The display borrows `AppState::sequencers` in voice order with an explicit
count, as does `updateStepLEDs()`. It does not own the sequencers or construct a
second routing table. Settings views use `UIState`'s derived predicates rather
than mirrored mode flags.

```cpp
class OLEDDisplay : public VoiceParameterObserver {
public:
  OLEDDisplay();
  bool begin();
  void update(const UIState &uiState, Sequencer *const *sequencers,
              size_t sequencerCount, VoiceManager *voiceManager = nullptr);
  void clear();
  bool isInitialized() const;
  void setVoiceManager(VoiceManager *voiceManager);

  void onVoiceParameterChanged(uint8_t voiceId, const VoiceState &state) override;
  void onVoiceSwitched(uint8_t newVoiceId) override;
  void onVoiceSwitched(const UIState &uiState, VoiceManager *voiceManager);

private:
  void forceUpdate(const UIState &uiState, VoiceManager *voiceManager);
};

extern OLEDDisplay oledDisplay;
```

---

## Concurrency & Performance

- **Core 0 Execution:** All OLED drawing, formatting, and I2C transmission occur on **Core 0** inside `loop()`, on the shared OLED/LED display slice (`kDisplayIntervalMs` in `src/app/ControlIO.cpp`, currently 40 ms ≈ 25 fps).
- **Dirty-Page Refresh:** Geometry and text operations write into Adafruit GFX's 1024-byte RAM buffer. `commitFrame()` then compares that buffer against `frameShadow_` one 128-byte page at a time and pushes only the changed pages to the SH1106 GDDRAM — each as a page/column command pair (`0xB0 | page`, column nibbles for the 2-column panel offset) followed by one 128-byte data write — so a static screen costs no I2C traffic at all.
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
- [`docs/voice-edit.md`](voice-edit.md) — Voice Editing mode (Priority 1 screen and its controls)