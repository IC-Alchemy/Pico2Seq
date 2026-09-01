# Alchemy Tile Control Surface — Design

- **Date:** 2026-09-01
- **Branch:** Working-Audio
- **Status:** Approved design, pending implementation plan
- **Approach:** Bridge layer (Approach A) — tiles integrate through one new glue
  module that feeds the existing handler code; legacy matrix button handling for
  indices 16–31 is deleted.

## Summary

Replace the firmware's param/utility matrix buttons (MPR121 indices 16–31) with
two Alchemy Modular UI I2C tiles driven by the vendored `src/AlchemyUI/` library:

- **SliderModule** — 4 faders + 4 buttons (TYPE_ID 0x01, addresses 0x08–0x0A).
- **ButtonModule8** — 8 buttons (TYPE_ID 0x02, addresses 0x0B–0x0D, full 8-bit
  button bitmaps in the existing 3 DATA bytes).

The 8 tile buttons carry the 7 parameter buttons plus a new 8th **Shift**
button. A GP7 strap switch selects the tile function set (Param / Utility).
The MPR121 matrix becomes 32 dedicated step pads controlling two voices at a
time (voice pair 1+2 or 3+4).

## Hardware

| Surface | Connection | Role |
|---|---|---|
| MPR121 (existing) | existing Wire bus | 32 step pads |
| SliderModule + ButtonModule8 | **Wire1**, dedicated bank, 400 kHz | param/utility buttons, voice select, faders |
| GP7 | `INPUT_PULLUP`, switch to GND | mode select: **LOW = Param mode, HIGH = Utility mode** (constant `kModeParamLevel`, flippable) |

Wire1 pins are named constants in `includes.h` (proposed SDA=GP8, SCL=GP9,
avoiding gate pins 10–12). **Bench item:** confirm against actual panel wiring.

## Mode system

- GP7 is read by the bridge each control-loop pass and debounced in software
  (20 ms stable before the mode flips).
- On a mode flip the bridge clears all `parameterButtonHeld[]` states and all
  shift-latches (nothing sticks across a mode change), flashes a control LED,
  and shows a `PARAM` / `UTIL` banner on the OLED.

## Control semantics

### Param mode (GP7 low)

**ButtonModule8** — bits 0–7 in PCB order:

| Bit | Function | Behavior (identical to today's matrix 16–22) |
|---|---|---|
| 0 | Note | hold-to-arm; step presses program notes |
| 1 | Velocity | hold-to-arm + encoder auto-select |
| 2 | Filter | hold-to-arm + encoder auto-select |
| 3 | Attack | hold-to-arm + encoder auto-select |
| 4 | Decay | hold-to-arm + encoder auto-select |
| 5 | Octave | hold-to-arm + encoder auto-select |
| 6 | Slide | exact current Slide-button behavior incl. slide-mode interplay |
| 7 | **Shift** | modifier, see below |

**SliderModule buttons:** Voice 1–4 direct select (one button per voice), in
**both** modes. Selecting a voice also switches the step-pad banks when crossing
the 1/2 ↔ 3/4 boundary.

### Utility mode (GP7 high)

**ButtonModule8** — bits 0–7:

| Bit | Function |
|---|---|
| 0 | Play/Stop (long-press while stopped opens settings — preserved) |
| 1 | Delay on/off toggle |
| 2 | Scale cycle |
| 3 | Swing template cycle |
| 4 | Theme cycle |
| 5 | Encoder-control target cycle |
| 6 | Randomize selected voice (long-press = reset) |
| 7 | Shift (modifier; in utility mode its chords are Shift+pad clear and Shift+Voice transport — no param latching) |

**SliderModule buttons:** Voice 1–4 direct select (same as param mode), plus
Shift chords (below).

### Shift modifier (works in both modes)

- **Shift + param tap** (param mode): latch/unlatch that parameter. At most one
  param is latched at a time (latching another moves the latch); a latched param
  behaves as held without a finger, and any other param buttons pressed
  physically remain ordinary momentary holds alongside it.
- **Shift + step pad**: clear that step (gate off + params reset) on the pad's
  own voice.
- **Shift + Voice1..4** (slider buttons): Play/Stop, Randomize selected voice,
  Scale cycle, Delay toggle respectively — in **both** modes. Chords are taps
  (no long-press variant; reset lives on the utility-mode tile Randomize).

### Faders (SliderModule channels 0–3)

| Channel | Param mode (selected voice) | Utility mode |
|---|---|---|
| 0 | Filter | Tempo (uClock) |
| 1 | Attack | Swing amount |
| 2 | Decay | Delay mix |
| 3 | Velocity | Gate length (selected voice) |

- Faders are live controls and feed the same recording path as the lidar: while
  a param is armed/latched and a step is in edit, movement records into that
  step via `updateParametersForStep`.
- Deadband ~8 counts, send on change only.

### Step pads (MPR121, all 32 indices)

- `bank = index / 16`, `step = index % 16`.
- Selected voice 1 or 2 → low bank = voice 1, high bank = voice 2.
- Selected voice 3 or 4 → low bank = voice 3, high bank = voice 4.
- Pad press/release keeps today's step semantics (toggle gate, long-press enters
  step edit, param-hold programming, gate-seq-length mode) but resolved through
  the bank mapping instead of assuming the single selected voice.

## Software design

### AlchemyUI library changes (`src/AlchemyUI/`)

1. `AlchemyProto.h`: `kButtonsPerTile` 4 → **8**. Button bitmaps are already
   full 8-bit for both tile types; slider-tile bits 4–7 read 0. Frame format,
   DATA_LEN, and register map untouched.
2. `AlchemyTiles.cpp`: button array and update-loop bounds follow
   `kButtonsPerTile`.
3. `ButtonMap.h`: rewritten for this 2-tile rig. One logical `Btn` enum:
   `Note, Velocity, Filter, Attack, Decay, Octave, Slide, Shift, Voice1..4,
   Play, DelayToggle, ScaleCycle, SwingCycle, ThemeCycle, EncoderCycle,
   Randomize` and one logical→(slot, bit) table. Mode membership lives in the
   bridge, so `AlchemyPanel::button(Btn)` keeps its API. `btnName()` extended.

### New: `src/ui/ControlSurfaceLogic.h/.cpp` (pure C++, unit-tested)

No Arduino includes. Owns the decisions worth testing:

- **ModeStabilizer** — raw GP7 readings in, `Mode::{Param,Utility}` out, with
  20 ms stability requirement and a change edge.
- **PadBank** — `selectedVoice (0..3) → {lowBankVoice, highBankVoice}` and
  `padIndex → (voice, step)`.
- **ShiftLatch** — shift level + param press/release edges →
  `parameterButtonHeld` semantics including latching (Shift+param toggles
  latch; latching another param moves it).
- **FaderMap** — mode + channel → parameter target; deadband filter.

### New: `src/ui/AlchemyControlBridge.h/.cpp` (glue, not unit-tested)

- Owns `AlchemyPanel`; `begin()` / `update(now, uiState, sequencers…)`.
- Called from `loop1()` in the 1 ms control slice alongside `Matrix_scan()`.
- Translates tile `TileButton` edges and fader values into calls to the
  **existing** handler code:
  - param buttons → the same logic `handleParameterButtonEvent` runs today
    (keyed by `ParamId`, not matrix index);
  - utility buttons → the same functions the matrix utility buttons call in
    `ButtonHandlers.cpp`;
  - voice buttons / chords / shift → bridge + `UIState` updates.
- Clears held/latched state on mode flips (via `ControlSurfaceLogic`).

### Modified firmware files

- `src/ui/UIEventHandler.cpp` — param/utility handling for matrix indices 16–31
  deleted; all 32 indices flow through the extended step handler with bank
  resolution.
- `src/ui/ButtonManager.cpp/.h` — `PARAM_BUTTON_MAPPINGS` (index-keyed) is
  replaced by `ParamId`-keyed helpers (`paramName(ParamId)`,
  `paramIdFromName(const char*)`) so `oled.cpp` name lookups keep working.
- `src/ui/UIState.h` — adds shift state, param latch state, current Alchemy
  mode (single struct, no loose globals).
- `src/OLED/oled.cpp` — switches from `PARAM_BUTTON_MAPPINGS` to the new
  helpers; mode banner on flip.
- `src/LEDMatrix/LEDController.cpp` — step LEDs extend from 16 to 32 pads
  (both visible banks follow playhead/gates); param status LEDs 48–54 remain as
  armed-param indicators (status only — the physical buttons left the matrix);
  voice indicators extended to 4 voices where panel LEDs exist.
- `Pico2Seq.ino` — `setup1()`: `Wire1` pins/clock, `bridge.begin(Wire1, …)`;
  `loop1()`: `bridge.update(...)` in the control slice. MPR121-absent
  `while(1)` halt stays (the matrix is the only step input in this rig).

## Testing

- `tests/unit/test_control_surface_logic.cpp` — ModeStabilizer, PadBank,
  ShiftLatch, FaderMap; added to `tests/CMakeLists.txt`. Full suite green.
- Build toolchain for host tests: Ninja + clang++ with `-D_USE_MATH_DEFINES`
  (MSVC generator fails on GCC flags).
- Firmware: compile-verify with arduino-cli if configured; tile/OLED/LED
  behavior verified at bench.

## Docs

Update `docs/architecture.md` (data-flow diagram, dual-core notes),
`docs/ButtonHandlers.md` (new tile semantics, deleted matrix indices), and the
root `README.md` panel description.

## Out of scope

- No sequencer-core (`src/pico2seq-core/`) changes.
- No new tile firmware (ButtonModule8 1.04 is already flashed).
- No per-voice step counts beyond 16 via pads (core already supports more).

## Bench verification items

1. Wire1 pin constants vs. actual panel wiring.
2. GP7 polarity in practice (flip `kModeParamLevel` if inverted).
3. Voice 3/4 LED existence on the panel (else OLED-only voice feedback).
4. Fader taper/deadband feel; adjust deadband constant.
