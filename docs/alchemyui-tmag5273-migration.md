# Migration Plan: MPR121 → AlchemyUI, AS5600 → TMAG5273

## Status: COMPLETED (Dual-Surface Architecture Implemented & Verified)

> **HISTORICAL DOCUMENT NOTICE (Updated 2026-09-01)**:  
> Both **Phase 1 (Sensors)** and **Phase 2 (Control Surface)** migrations are **100% completed and merged** in the codebase.
>
> - **Phase 1 (Sensors) — COMPLETED**:
>   - TI TMAG5273A magnetic velocity encoder on Wire at `0x35` (`TMAG5273::ADDRESS_A`) managed by `MagEncoder` (`src/VelocityEncoder/`) and `EncoderManager` (`src/sensors/`), replacing legacy AS5600.
>   - ToF distance sensor modernized to `Adafruit_VL53L1X` on Wire at `0x29` with non-blocking continuous acquisition.
>   - All `AS5600` identifiers renamed across `SequencerDefs.h`, `UIState.h`, `ButtonHandlers.cpp`, `UIEventHandler.cpp`, and display modules.
> - **Phase 2 (Control Surface) — COMPLETED via Dual-Surface Design**:
>   - *Initial proposal (historical below)*: Proposed dropping the MPR121 touch matrix entirely and compressing 32 functions into 20 tile buttons with paged 8-step banks.
>   - *Approved & implemented architecture*: Adopted the **Dual-Surface Architecture** specified in `docs/superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md`. The 32 capacitive pads on the MPR121 (`Adafruit_MPR121` on Wire at `0x5A`) are **retained** as dedicated step pads across two 16-step voice banks, while the Alchemy UI panel (SliderModule 4 faders + ButtonModule8 8 buttons) is integrated on a separate bus (`Wire1`, GP14/15 @ 100 kHz) with a hardware `GP7` mode strap.
>   - Pure decision logic is factored into `src/ui/ControlSurfaceLogic.h/.cpp` (covered by host unit tests in `tests/unit/test_control_surface_logic.cpp`), and hardware glue is encapsulated in `src/ui/AlchemyControlBridge.h/.cpp`.
>
> The sections below record the original planning analysis and trade-offs for historical context.

---

## 1. Historical Baseline Analysis & Assumptions

**{confirmed}** The touch input is `Adafruit_MPR121` driving a custom 32-button scanner (`src/matrix/Matrix.cpp/h`, `MATRIX_BUTTON_COUNT = 32`), constructed in `Pico2Seq.ino` as `Adafruit_MPR121 touchSensor`, initialized in `setup()` via `touchSensor.begin(0x5A)` + `Matrix_init(&touchSensor)`, polled every 1ms via `Matrix_scan()`, and dispatched through one callback registered with `Matrix_setEventHandler()` that forwards `MatrixButtonEvent{buttonIndex, type}` into `matrixEventHandler()`.

**{confirmed}** The legacy 32 button indices were heavily overloaded by UI mode:
- 0–15: step pads — also reinterpreted as voice-select (settings mode), preset picker (settings), voice-parameter toggles (voice-parameter sub-mode), and gate-track-length picker (while the encoder-control button was long-held).
- 16–21: parameter "hold to record" buttons (Note/Velocity/Filter/Attack/Decay/Octave) — holding one also auto-selected that parameter as the magnetic encoder's live target.
- 22: dual-purpose — slide-mode toggle *and* a 7th parameter-record button ("Slide").
- 23: delay on/off toggle.
- 24: voice switch (cycles voice 0→1→2→3).
- 25: encoder-target cycle button; short-press cycled, long-press entered "gate sequence length" mode, and inside Settings it toggled the settings sub-mode instead.
- 26: play/stop; stopping also opened Settings.
- 27: scale change. 28: LED theme change. 29: shuffle/swing pattern change.
- 30–31: randomize, each covering two voices depending on a "page" flag (`resolveRandomizeVoiceIndex`).

**{confirmed}** The magnetic encoder (`src/sensors/as5600.cpp/h`, `AS5600Sensor`) was not just a sensor driver — `AS5600ParameterMode` (an enum) and `AS5600BaseValues`/`AS5600BaseValuesVoice1` (structs) were defined in `src/sequencer/SequencerDefs.h` itself, and `AS5600Manager.cpp/h` (~40 functions) implemented the "shift and scale" mapping between encoder offsets and sequencer values, bounds/clamping, and OLED-string formatting. `UIState.h`, `ButtonHandlers.cpp`, `UIEventHandler.cpp`, `oled.cpp`, and `LEDMatrixFeedback.cpp` all consumed these types directly.

**{confirmed}** `src/AlchemyUI/` and `src/VelocityEncoder/` (Git submodule) were vendored and integrated into the build. `VelocityEncoder`'s `MagEncoder` class is a single driver wrapping either an AS5600 or a TMAG5273 behind one identical API (`begin/update/isConnected/getRawAngle/getParameterIncrement/...`).

**{confirmed}** I2C addresses post-migration do not collide:
- Main Bus (`Wire`, GP4/GP5 @ 100 kHz): OLED `0x3C`, VL53L1X `0x29`, TMAG5273A `0x35` (`TMAG5273::ADDRESS_A`), MPR121 `0x5A`.
- Tile Bus (`Wire1`, GP14/GP15 @ 100 kHz): SliderModule `0x08`, ButtonModule8 `0x0B`.

---

## 2. Design Evolution: Initial 20-Button Proposal vs. Implemented Dual-Surface Architecture

### The Initial Proposal (Superseded)
The initial proposal suggested eliminating the MPR121 touch matrix entirely and consolidating all functions onto 20 buttons across 5 AlchemyUI tiles:
- 8 step pads (`Step1`–`Step8`) using a `Page` flip button for steps 9–16.
- 6 parameter buttons (Note, Velocity, Filter, Attack, Decay, Octave).
- 5 transport/utility buttons (Play, Voice, Scale, Shuffle, Random).
- 5 orphaned functions (Slide toggle, Delay toggle, Theme cycle, Dedicated encoder cycle, Gate seq length) relegated to submenus in Settings mode.

### The Implemented Dual-Surface Architecture (Approved & Active)
During architectural refinement (`docs/superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md`), the user and engineering team recognized that 8-step pagination compromised live polyphonic performance. Instead, the **Dual-Surface Architecture** was adopted:

1. **Preserve MPR121 for 32 Dedicated Step Pads**:
   - All 32 physical pads on the MPR121 touch grid operate exclusively as step pads.
   - Dynamically mapped via `ControlSurface::PadBank` into two 16-step banks (Voices 1 & 2 or Voices 3 & 4).
   - Instant 16-step visual feedback and manipulation without step pagination.
2. **Dedicate Alchemy Tiles on Wire1 (100 kHz) for Controls & Faders**:
   - **SliderModule (Slot 0, 0x08)**: 4 faders (12-bit ADC) for continuous parameter/utility editing + 4 buttons for direct Voice 1–4 selection (plus Shift chords).
   - **ButtonModule8 (Slot 1, 0x0B)**: 8 buttons carrying parameter arming (Param mode) or transport/utilities (Utility mode), plus Shift modifier.
   - **GP7 Mode Strap Switch**: Selects Param vs Utility mode, debounced in software (20 ms).

---

## 3. Event Model & Architecture Implementation

Rather than using an adapter shim translating tile events into synthetic matrix integers, the final architecture cleanly decoupled policy from hardware:

1. **Pure C++ Policy (`src/ui/ControlSurfaceLogic.h/.cpp`)**:
   - `ModeStabilizer`: Debounces GP7 mode strap (20 ms window) and generates clean mode flip transitions.
   - `PadBank`: Resolves pad index (0–31) and selected voice (0–3) into `{voice, step}`.
   - `ShiftLatch`: Manages Shift + tap parameter latching and momentary holds.
   - `FaderMap`: Normalizes 12-bit counts (0–4095) with an 8-count deadband filter to eliminate bus traffic.
2. **Hardware Bridge (`src/ui/AlchemyControlBridge.h/.cpp`)**:
   - Direct master driver polling `AlchemyPanel` on `Wire1` @ 100 kHz within `loop()` (1 ms slice).
   - Invokes shared firmware handlers: `handleParameterButtonById`, `handleSlideModePress`, `handleControlButton`, `selectVoice`, `clearSequencerStep`.
3. **Matrix Handler (`src/ui/UIEventHandler.cpp`)**:
   - Step pad events from `Matrix_scan()` resolve through `PadBank::resolve` and dispatch to `handleStepButtonEvent()`.

---

## 4. Completed Identifier Renaming Table

The full mechanical rename from `AS5600` to `Encoder` was executed across the codebase:

| Old Identifier | New Identifier | Location | Status |
|---|---|---|---|
| `AS5600ParameterMode` (enum) | `EncoderParameterMode` | `SequencerDefs.h` | ✅ Renamed |
| `AS5600BaseValues` / `AS5600BaseValuesVoice1` (structs) | `EncoderBaseValues` / `EncoderBaseValuesVoice1` | `SequencerDefs.h` | ✅ Renamed |
| `src/sensors/AS5600Manager.cpp/h` | `src/sensors/EncoderManager.cpp/h` | Files | ✅ Renamed |
| `as5600Sensor` (global `AS5600Sensor`) | `magEncoder` (global `MagEncoder`) | `Pico2Seq.ino`, `EncoderManager.h` | ✅ Renamed |
| `as5600BaseValuesVoice1` / `as5600BaseValuesVoice2` | `encoderBaseValues[VoiceSystem::MAX_VOICES]` (per-voice array) + `encoderDelayValues` | `EncoderManager.cpp` | ✅ Renamed (superseded again 2026-09-04 by commit 5a6f9a0's per-voice array) |
| `applyAS5600BaseValues` | `applyEncoderBaseValues` | `EncoderManager.cpp/h` | ✅ Renamed |
| `applyAS5600DelayValues` / `applyAS5600SlideTimeValues` | `applyEncoderDelayValues` / `applyEncoderSlideTimeValues` | `EncoderManager.cpp/h` | ✅ Renamed |
| `updateAS5600BaseValues` / `updateAS5600StepParameterValues` | `updateEncoderBaseValues` / `updateEncoderStepParameterValues` | `EncoderManager.cpp/h` | ✅ Renamed |
| `getAS5600BaseValueRange` / `clampAS5600BaseValue` | `getEncoderBaseValueRange` / `clampEncoderBaseValue` | `EncoderManager.cpp/h` | ✅ Renamed |
| `convertAS5600ParameterToParamId` | `convertEncoderParameterToParamId` | `EncoderManager.cpp/h` | ✅ Renamed |
| `resetAS5600BaseValues` / `initAS5600BaseValues` | `resetEncoderBaseValues` / `initEncoderBaseValues` | `EncoderManager.cpp/h` | ✅ Renamed |
| `BUTTON_AS5600_CONTROL` | `BUTTON_ENCODER_CONTROL` | `UIConstants.h` | ✅ Renamed |
| `uiState.currentAS5600Parameter` | `uiState.currentEncoderParameter` | `UIState.h` | ✅ Renamed |
| `uiState.lastAS5600ButtonPressTime` | `uiState.lastEncoderButtonPressTime` | `UIState.h` | ✅ Renamed |
| `uiState.as5600ControlPressTime` | `uiState.encoderControlPressTime` | `UIState.h` | ✅ Renamed |
| `handleAS5600ControlButton` / `cycleAS5600Parameter` | `handleEncoderControlButton` / `cycleEncoderParameter` | `ButtonHandlers.cpp` | ✅ Renamed |

---

## 5. Verification & Final Results

1. **Host Unit Tests**:
   - `tests/unit/test_control_surface_logic.cpp`: Covers `ModeStabilizer`, `PadBank`, `ShiftLatch`, and `FaderMap`.
   - `tests/unit/test_alchemy_proto.cpp`: Covers the Alchemy tile wire format (`AlchemyProto.h`) — per-tile-TYPE DATA block offsets (slider DATA 8..10 vs button DATA 0..2), frame checksum, identity decoding — and `TileButton` press/hold/tap edges.
   - `tests/unit/test_rpdsp_additions.cpp`, `test_voice.cpp`, `test_voiceoscillator.cpp`, `test_scales.cpp`, `test_sequencer.cpp`.
2. **I2C Bus Separation**:
   - Wire (GP4/5 @ 100 kHz) handles OLED, TMAG5273, VL53L1X, and MPR121.
   - Wire1 (GP14/15 @ 100 kHz) handles SliderModule (0x08) and ButtonModule8 (0x0B).
3. **Dual-Core Safety**:
   - Audio synthesis remains 100% isolated on Core 1.
   - All sensor acquisition, matrix scanning, and tile polling run non-blocking in Core 0's 1 ms control loop.

---

## Related References
- `docs/sensors.md`: Hardware specifications for TMAG5273, VL53L1X, and MPR121.
- `docs/ButtonHandlers.md`: Full event handling and Dual-Surface control guide.
- `docs/superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md`: Canonical engineering specification for the Alchemy tile integration.
