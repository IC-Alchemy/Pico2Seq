# ButtonHandlers Module Documentation

## Overview

The `ButtonHandlers` module provides specialized button event handling and control surface dispatch for the Pico2Seq UI system. This module was extracted from the main `UIEventHandler` to improve code organization and maintainability by encapsulating button-specific logic into focused, reusable functions.

The Pico2Seq control surface operates on a **Dual-Surface Architecture** that pairs dedicated physical step pads with modular parameter/utility controls:

1. **MPR121 Capacitive Touch Step Matrix** (Main bus `Wire`, GP4/GP5 @ 100 kHz):
   - All 32 capacitive touch electrodes are dedicated exclusively as sequencer step pads.
   - Organized as two 16-step voice banks resolved dynamically through `ControlSurface::PadBank`.
2. **Alchemy Modular UI Panel** (Tile bus `Wire1`, GP14/GP15 @ 100 kHz):
   - **SliderModule (TYPE 0x01, base address 0x08)**: 4 continuous faders (12-bit ADC) + 4 buttons (direct Voice 1–4 selection).
   - **ButtonModule8 (TYPE 0x02, base address 0x0B)**: 8 tactile buttons (7 parameter/utility buttons + 1 Shift modifier).
   - **GP7 Mode Strap Switch**: Selects the active tile function set (LOW = Param Mode, HIGH = Utility Mode), debounced in software (20 ms window).
   - **Runtime Slot Resolution**: The bridge resolves which driver slot holds each tile by TYPE_ID (`AlchemyControlBridge::resolveSlots()`), not by fixed position — slot order is scan order, so a tile that did not answer at boot shifts the slots after it.

The firmware partitions the control surface implementation into two distinct layers:
- **`src/ui/ControlSurfaceLogic.h/.cpp`**: Pure, portable C++ decision logic (no Arduino dependencies, 100% unit-tested in `tests/unit/test_control_surface_logic.cpp`).
- The Alchemy tile wire-format decoder (`src/AlchemyUI/src/AlchemyProto.h`) is likewise host-tested in `tests/unit/test_alchemy_proto.cpp` (per-tile-TYPE DATA block offsets, frame checksum, identity decoding, `TileButton` press/hold/tap).
- **`src/ui/AlchemyControlBridge.h/.cpp`**: Hardware-bound translation glue on Core 1 that polls `AlchemyPanel` on Wire1, debounces GP7, and invokes the shared handler entry points in `ButtonHandlers.cpp` and `UIEventHandler.cpp`.

---

## Dual-Surface Control Architecture

```
                       +---------------------------------------+
                       |           Core 1 Control Loop         |
                       |       (1 ms non-blocking slice)       |
                       +---------------------------------------+
                                  /                \
                                 /                  \
   +------------------------------------+    +------------------------------------+
   |   MPR121 Touch Matrix (Wire I2C0)  |    |   Alchemy Tiles (Wire1 I2C1 100k)  |
   |   32 Capacitive Step Pads (0x5A)   |    |   SliderModule + ButtonModule8     |
   +------------------------------------+    +------------------------------------+
                     |                                         |
                     v                                         v
   +------------------------------------+    +------------------------------------+
   |         Matrix_scan()              |    |       AlchemyControlBridge         |
   |     matrixEventHandler()           |    |   (ModeStabilizer, ShiftLatch,     |
   |   (PadBank::resolve -> Voice/Step) |    |    FaderMap, edge detection)       |
   +------------------------------------+    +------------------------------------+
                     \                                         /
                      \                                       /
                       v                                     v
   +------------------------------------------------------------------------------+
   |                        Shared Firmware UI Dispatch                           |
   |  - ButtonHandlers (handleRandomizeButton, handleControlButton, etc.)        |
   |  - UIEventHandler (handleParameterButtonById, selectVoice, clearSequencer)   |
   |  - UIState (Single Source of Truth)                                          |
   +------------------------------------------------------------------------------+
```

---

## Alchemy Tile Semantics and Modes

### 1. Param Mode (`GP7` LOW / `ControlSurface::Mode::Param`)

In Param mode, ButtonModule8 provides instant parameter arming for real-time recording via distance sensor, magnetic encoder, or continuous faders.

| Bit / Button | Parameter / Function | Behavior |
|---|---|---|
| **0** | `Note` | Hold-to-arm parameter recording; step presses program note pitch |
| **1** | `Velocity` | Hold-to-arm parameter recording + auto-selects encoder target |
| **2** | `Filter` | Hold-to-arm parameter recording + auto-selects encoder target |
| **3** | `Attack` | Hold-to-arm parameter recording + auto-selects encoder target |
| **4** | `Decay` | Hold-to-arm parameter recording + auto-selects encoder target |
| **5** | `Octave` | Hold-to-arm parameter recording + auto-selects encoder target |
| **6** | `Slide` | Toggles slide/portamento mode (clears conflicting edit modes) |
| **7** | `Shift` | Modifier for parameter latching and secondary chords |

**Fader Channels in Param Mode:**
- **Fader 0**: Filter Cutoff for the currently selected voice.
- **Fader 1**: Attack Time for the currently selected voice.
- **Fader 2**: Decay Time for the currently selected voice.
- **Fader 3**: Velocity for the currently selected voice.
- *Recording Behavior*: When a matching parameter button is held (armed) and a step is in edit (`selectedStepForEdit >= 0`), moving the corresponding fader writes the normalized value directly into the sequencer step track.

---

### 2. Utility Mode (`GP7` HIGH / `ControlSurface::Mode::Utility`)

In Utility mode, ButtonModule8 carries transport, scale, swing, effects, and system controls.

| Bit / Button | Function | Behavior |
|---|---|---|
| **0** | `Play / Stop` | Starts/stops sequencer clock (stopping automatically opens Settings mode) |
| **1** | `Delay Toggle` | Toggles global delay effect ON/OFF; sets encoder target to DelayTime |
| **2** | `Scale Cycle` | Cycles forward through the 13 musical scales |
| **3** | `Swing Pattern` | Cycles through the 16 groove/shuffle templates in `ShuffleTemplates.h` |
| **4** | `Theme Cycle` | Cycles visual LED color themes across `LEDTheme` presets |
| **5** | `Encoder Target` | Short press cycles encoder target; hold enters Gate Sequence Length mode |
| **6** | `Randomize` | Short press randomizes selected voice; long press (>1000 ms) resets voice |
| **7** | `Shift` | Modifier for transport and utility chords |

**Fader Channels in Utility Mode:**
- **Fader 0**: Master Tempo (uClock BPM: 45–200 BPM).
- **Fader 1**: Swing Amount (continuous shuffle template depth).
- **Fader 2**: Delay Feedback Mix (`feedbackAmmount`: 0.0 to 0.91).
- **Fader 3**: Gate Length (applies gate length across active steps on the selected voice).

---

### 3. SliderModule Direct Voice Selection and Shift Chords

The 4 buttons on the SliderModule tile act as direct Voice 1–4 selectors in both Param and Utility modes:

- **Direct Press**:
  - Button 0: Select Voice 1 (`selectedVoiceIndex = 0`)
  - Button 1: Select Voice 2 (`selectedVoiceIndex = 1`)
  - Button 2: Select Voice 3 (`selectedVoiceIndex = 2`)
  - Button 3: Select Voice 4 (`selectedVoiceIndex = 3`)
- **Shift + Voice Button Chords** (Held Shift + Slider Button):
  - `Shift + Voice 1`: Play / Stop toggle
  - `Shift + Voice 2`: Randomize selected voice (short-press randomize only — the poll-driven long-press reset never triggers from a chord)
  - `Shift + Voice 3`: Cycle musical scale
  - `Shift + Voice 4`: Toggle delay effect ON / OFF

---

### 4. Shift Modifier Behavior Across Surfaces

- **Shift + Param Button Tap** (Param Mode): Latches the parameter hold state via `ControlSurface::ShiftLatch`. Only one parameter may be latched at a time; tapping another parameter shifts the latch; tapping the latched parameter clears it. Latched parameters behave as held without needing continuous finger contact.
- **Shift + Step Pad** (MPR121 Matrix): Clears that step on the pad's resolved voice (gate set to OFF, parameters reset to default values via `clearSequencerStep()`).

---

### 5. Step Pad Organization (32 MPR121 Pads)

All 32 pads on the capacitive matrix represent sequencer steps mapped into two 16-step banks:

```cpp
PadAddress addr = ControlSurface::PadBank::resolve(padIndex, uiState.selectedVoiceIndex);
// addr.voice -> voice index 0..3
// addr.step  -> step index 0..15 within that voice
```

- When **Voice 1 or Voice 2** is selected:
  - Bank 0 (Pads 0–15): Voice 1 steps 0–15
  - Bank 1 (Pads 16–31): Voice 2 steps 0–15
- When **Voice 3 or Voice 4** is selected:
  - Bank 0 (Pads 0–15): Voice 3 steps 0–15
  - Bank 1 (Pads 16–31): Voice 4 steps 0–15

All step actions (gate toggle, long-press step selection for editing, parameter-hold step entry, gate sequence length adjustment, and slide toggling) resolve to the pad's bank-mapped voice rather than assuming the single global selected voice.

---

## Pure C++ Decision Logic (`ControlSurfaceLogic.h/.cpp`)

The core control surface algorithms are implemented as portable C++ classes decoupled from hardware:

### 1. `ModeStabilizer`
Software debounce and edge detection for the GP7 mode strap switch.
- Stable duration requirement: 20 ms (`kModeStabilityMs = 20`).
- Bouncing resets candidate timer.
- `tookChange()` reports rising/falling mode flip edges to trigger clean state resets.

### 2. `PadBank`
Resolves physical pad indices (0–31) to specific sequencer voices (0–3) and step offsets (0–15).
- `PadPair pairFor(uint8_t selectedVoice)`: Returns `{0, 1}` for voices 0/1; `{2, 3}` for voices 2/3.
- `PadAddress resolve(uint8_t padIndex, uint8_t selectedVoice)`: Clamps input index and returns `{voice, step}`.

### 3. `ShiftLatch`
Maintains momentary button holds and single-parameter Shift latching.
- `onParamButton(uint8_t paramId, bool pressed, bool shiftHeld)`: Manages momentary and latched hold states.
- `applyTo(bool *heldOut, uint8_t count)`: Exports derived boolean states to `UIState::parameterButtonHeld`.
- `reset()`: Flushes all momentary holds and latches (called automatically on mode flip).

### 4. `FaderMap`
Fader target assignment, 12-bit ADC normalization (0–4095 to 0.0–1.0), and deadband filtering.
- `kDeadbandCounts = 8`: Suppresses jitter and spurious I2C updates.
- `accept(uint8_t channel, uint16_t rawCounts)`: Returns `true` only when movement exceeds deadband or immediately after a mode reset.
- `assignmentFor(Mode mode, uint8_t channel)`: Maps channel index to `FaderAssignment{target, paramId}`.

---

## Public Functions (`ButtonHandlers.h`)

```cpp
#ifndef BUTTON_HANDLERS_H
#define BUTTON_HANDLERS_H

#include <Arduino.h>

class UIState;
class Sequencer;
class MidiNoteManager;

// Core button handling functions
void handleRandomizeButton(int voiceIndex, UIState &state);
void handleVoiceParameterButton(int voiceIndex, int paramIndex, UIState &state);
void handleControlButton(int buttonId, UIState &state);

// Button state management helpers
void beginRandomizePress(int voiceIndex, UIState &state);
void endRandomizePress(int voiceIndex, UIState &state);

#endif // BUTTON_HANDLERS_H
```

---

## Detailed Function Reference

### `handleRandomizeButton(int voiceIndex, UIState &state)`
Processes randomize operations for a specific voice (0–3):
- **Short Press** (`< 1000 ms`): Randomizes parameters across the target voice's sequencer (`seq->randomizeParameters()`).
- **Long Press** (`≥ 1000 ms`): Promoted by `pollUIHeldButtons()` in `loop1()` to trigger complete parameter reset.
- Short press raises a transient OLED confirmation notice (`UIState::oledNoticeKind = Randomized`).

### `handleVoiceParameterButton(int voiceIndex, int paramIndex, UIState &state)`
Processes per-voice synthesizer configuration toggles:
- Index 8: Toggle envelope enable/disable (`config.hasEnvelope`).
- Index 9: Toggle overdrive saturation (`config.hasOverdrive`).
- Index 11: Cycle ladder filter mode across 6 modes (`LP24`, `LP12`, `BP24`, `BP12`, `HP24`, `HP12`) matching `voiceui::kFilterModeCount`.
- Index 12: Step filter resonance (`config.filterRes += 0.1f`, wraps at 1.0).

### `handleControlButton(int buttonId, UIState &state)`
Dispatches system-wide control actions based on button ID defined in `UIConstants.h`:

| Button ID Constant | Target Functionality |
|---|---|
| `BUTTON_SLIDE_MODE` | Toggles global slide/portamento mode |
| `BUTTON_ENCODER_CONTROL` | Cycles active magnetic encoder target parameter |
| `BUTTON_PLAY_STOP` | Toggles transport; stopping opens Settings mode; starting clears Settings |
| `BUTTON_CHANGE_SCALE` | Cycles through 13 musical scale tables (`currentScale = (currentScale + 1) % 13`) |
| `BUTTON_CHANGE_THEME` | Cycles LED matrix visual feedback themes |
| `BUTTON_CHANGE_SWING_PATTERN` | Cycles through 16 shuffle/swing groove templates |
| `BUTTON_TOGGLE_DELAY` | Toggles delay effect and sets encoder parameter to `DelayTime` |

---

## Verified Control Button Implementation Example

The verified `BUTTON_PLAY_STOP` logic in `ButtonHandlers.cpp` directly coordinates clock callbacks and Settings mode transitions:

```cpp
case BUTTON_PLAY_STOP:
    if (isClockRunning)
    {
        onClockStop();
        // Enter settings mode when stopping
        state.settingsMode = true;
        state.inPresetSelection = true;
    }
    else
    {
        onClockStart();
        // Exit settings mode if active
        if (state.settingsMode)
        {
            state.settingsMode = false;
            state.inPresetSelection = false;
            state.selectedStepForEdit = -1;
        }
    }
    break;
```

---

## UIState Centralized Integration

All button and control surface state is consolidated in `UIState` (`src/ui/UIState.h`):

```cpp
struct UIState {
    // Parameter Arming States (indexed by ParamId)
    bool parameterButtonHeld[PARAM_ID_COUNT] = {false};

    // Mode & Transport States
    bool delayOn = true;
    bool slideMode = false;
    uint8_t selectedVoiceIndex = 0; // 0..3
    int selectedStepForEdit = -1;
    ParamId currentEditParameter = ParamId::Count;
    EncoderParameterMode currentEncoderParameter = EncoderParameterMode::Velocity;

    // Settings Mode States
    bool settingsMode = false;
    bool inPresetSelection = false;
    uint8_t settingsMenuIndex = 0;
    uint8_t voicePresetIndices[4] = {4, 2, 1, 6};

    // Encoder Hold / Gate Seq Length
    unsigned long encoderControlPressTime = 0;
    bool encoderControlWasPressed = false;
    bool gateSeqLengthMode = false;

    // Alchemy Tile State (GP7 Mode Strap & Shift Latch)
    enum class AlchemyMode : uint8_t { Param = 0, Utility = 1 };
    AlchemyMode alchemyMode = AlchemyMode::Param;
    bool shiftHeld = false;
    int8_t latchedParameter = -1;
    volatile unsigned long alchemyModeBannerUntil = 0;

    // Transient OLED notice (delay toggle / randomize confirmations)
    enum class OledNoticeKind : uint8_t { None = 0, DelayOn, DelayOff, Randomized };
    volatile unsigned long oledNoticeUntil = 0;
    volatile OledNoticeKind oledNoticeKind = OledNoticeKind::None;
    volatile uint8_t oledNoticeVoice = 0;
};
```

---

## Source File Layout

```
src/ui/
├── ControlSurfaceLogic.h/.cpp # Pure C++ decision logic (ModeStabilizer, PadBank, ShiftLatch, FaderMap)
├── AlchemyControlBridge.h/.cpp# Wire1 tile hardware glue (polls AlchemyPanel, debounces GP7)
├── ButtonHandlers.h/.cpp      # Specialized button handling logic & control dispatch
├── ButtonManager.h/.cpp       # ParamId-keyed helpers, hold tracking, and name lookups
├── UIEventHandler.h/.cpp      # Matrix step pad event dispatch & shared bridge entry points
├── UIConstants.h              # Button ID definitions, timing constants, and matrix sizes
└── UIState.h                  # Centralized UI state structure
```

---

## Related Documentation
- `docs/sensors.md`: Magnetic encoder, ToF distance sensor, and MPR121 hardware specifications.
- `docs/matrix.md`: 32-pad capacitive touch matrix scanning and debounce mechanics.
- `docs/superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md`: Full specification for the Dual-Surface Alchemy Tile control system.
- `docs/alchemyui-tmag5273-migration.md`: Migration history and architectural decisions.