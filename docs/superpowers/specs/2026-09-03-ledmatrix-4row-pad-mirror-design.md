# LED Matrix 8×4 Pad-Mirror + Control-Cluster-to-OLED — Design

Date: 2026-09-03
Status: Approved (user chose "Full LEDMatrix cleanup"; OLED-replaces-LED-decisions made interactively)

## Problem

The LED panel is driven as an 8×8 (64 LED) matrix, but only 32 LEDs exist in
use. The sequencer step grid uses rows 0–1 (indices 0–15) for the first voice
of the selected pair and indices 24–39 (rows 3–4) for the second voice, while
the MPR121 touch matrix is 4 rows × 8 cols with pads 0–15 (rows 0–1) = pair
low voice and pads 16–31 (rows 2–3) = pair high voice. So the second voice's
LEDs sit one row off from its touch pads, with a dead gap row, and ~150 lines
of `updateControlLEDs()` code write to indices 40–59 that light nothing on a
32-LED chain. Adjacent rot in the same files: `updateGateLEDs()` is dead code
that also hardcodes voices 1/2, and `ALL_THEMES[]` holds 7 palettes while
`LEDTheme` declares 10 + `COUNT` — themes 7–9 read past the array (UB) and
VOLCANIC/FOREST/NEON show the wrong palette.

## Goals

1. LED geometry = 8 wide × 4 tall (32 LEDs), with the invariant:
   **for the same (band, step), LED linear index == touch pad index**
   (band 0 = pads/LEDs 0–15 = pair low voice, band 1 = pads/LEDs 16–31 =
   pair high voice). Both bands always show the selected voice pair.
2. Delete the control-cluster LED code; convey the same information via
   quick transient OLED screens that return to the previous view.
3. Centralize band/step → LED index math in one host-tested helper so
   pad↔LED alignment cannot drift again.
4. Make all 10 declared themes real and safe to cycle.
5. Update docs to match.

## Non-goals

- No sequencer/voice/DSP changes, no dual-core or timing changes.
- No new hardware claims: firmware behavior is bench-verified later.
- No changes to the Alchemy tile wire format or PadBank semantics.

## 1. Geometry constants (`src/LEDMatrix/LEDConstants.h`)

- `MATRIX_HEIGHT` 8 → 4; `MATRIX_TOTAL_LEDS` = 32 (stays `WIDTH*HEIGHT`).
- `BOTTOM_HALF_OFFSET` 24 → 16; `VOICE_PAIR_SEPARATION` 3 → 1.
- Comments updated (no more "8x8" / "Row 4 start").

`LEDMatrix::setLED()`'s `y < HEIGHT` bound plus the 32-LED FastLED strip make
any stale out-of-range write a no-op.

## 2. `ControlSurface::LedLayout` helper (host-tested)

Lives in `src/ui/ControlSurfaceLogic.h/.cpp` (pure C++, no Arduino includes —
`LEDConstants.h` pulls in FastLED, so the helper carries its own constexprs;
`LEDMatrixFeedback.cpp` `static_assert`s they agree with `LEDConstants`).

```cpp
class LedLayout {
public:
  static constexpr uint8_t kWidth = 8;
  static constexpr uint8_t kRowsPerBand = 2;
  static constexpr uint8_t kStepsPerBand = 16;   // kWidth * kRowsPerBand
  static constexpr uint8_t kBandCount = 2;
  static constexpr uint8_t kLedCount = 32;       // kStepsPerBand * kBandCount
  static constexpr uint8_t bandOfVoiceInPair(uint8_t selectedVoiceIndex); // voice & 1 (voice clamped like PadBank::pairFor)
  static constexpr int linearIndex(uint8_t band, uint8_t step); // band*16 + step; -1 if out of range
  static constexpr int x(uint8_t step);                          // step % 8; -1 if out of range
  static constexpr int y(uint8_t band, uint8_t step);            // band*2 + step/8; -1 if out of range
};
```

All renderers in `LEDMatrixFeedback.cpp` switch to it: `renderVoicePair`,
`addPolyrhythmicOverlay` (also fixes its overlay-row misalignment),
slide mode (`y += 3` → `y(band, step)`), gate-length mode, parameter-edit
dim/paint, and the step-edit blink. The pair shown follows
`uiState.selectedVoiceIndex` exactly as today (`PadBank::pairFor`).

Tests in `tests/unit/test_control_surface_logic.cpp` (`[control_surface]`):
index equality with the touch-pad geometry, out-of-range → −1, band mapping
for voices 0–3 (+ clamp).

## 3. Control cluster → deleted; information moves to OLED

- Delete `src/LEDMatrix/LEDController.h/.cpp`; remove the
  `initLEDController()` call (`Pico2Seq.ino:804`) and the
  `updateControlLEDs()` call from the 20 ms frame (frame = `updateStepLEDs`
  → `show`).
- Remove the now write-only `flash23Until` / `flash25Until` / `flash31Until`
  from `UIState.h` and their setters/resets (`ButtonHandlers.cpp`,
  `AlchemyControlBridge.cpp`, `ButtonManager.cpp`).
- Already covered on OLED (no work): held-parameter editing, voice selection,
  step editing, PARAM/UTIL mode banner.
- New transient notice mechanism (extends `UIState`, per convention):
  `volatile unsigned long oledNoticeUntil = 0;`,
  `enum class OledNoticeKind : uint8_t { None, DelayOn, DelayOff, Randomized };`
  + `OledNoticeKind oledNoticeKind` + `uint8_t oledNoticeVoice`.
  Duration 800 ms (same as the mode banner). Rendered in `oled.cpp`'s
  priority stack directly below the PARAM/UTIL banner and above settings —
  big centered text, then the previous view resumes.
- Triggers (both control surfaces funnel through these single sites):
  - `handleControlButton(BUTTON_TOGGLE_DELAY)` → `DelayOn`/`DelayOff` per
    `state.delayOn` (replaces `flash23Until`).
  - `handleRandomizeButton()` → `Randomized` + voice number, set only when
    `randomizeParameters()` actually runs (short press) — more accurate than
    the old LED, which flashed on long press too (replaces `flash31Until`).
  - `flash25Until` (play/stop) had no visible consumer; simply removed.
  - `AlchemyControlBridge.cpp`'s mode-flip `flash31Until` write is dropped
    (the mode banner already covers it).
- Encoder feedback (was the control-cluster value fade): when
  `uiState.currentEncoderParameter != EncoderParameterMode::COUNT`, the
  default status screen gains one bottom line, e.g. `ENC: DelayTime 0.42`
  (value via `getEncoderParameterValue()`, declared `extern` in `oled.cpp`;
  defined in `EncoderManager.cpp`). No popup — the line simply appears while
  an encoder parameter is engaged.

## 4. Themes

Author three palettes in the existing 30-color format so `ALL_THEMES[]` has
all 10 entries in enum order:

- `VOLCANIC` — red/orange fire.
- `FOREST` — green/brown nature.
- `NEON` — bright cyan/magenta.

`setLEDTheme`'s `< COUNT` guard then never indexes out of bounds, and the
`BUTTON_CHANGE_THEME` cycler becomes safe. Delete dead `updateGateLEDs()`.

## 5. Docs

- `docs/LEDMatrix.md`: 8×4/32 geometry, pad-mirror band diagram, control
  cluster → OLED notices, 10 themes, updated layout constants.
- `docs/architecture.md`, `docs/matrix.md`, `docs/oled.md`, `README.md`,
  `.agents/skills/pico2seq-codebase/references/ui-input.md`: fix any 8×8/64/
  11-theme mentions of the LED matrix. Run
  `python tests/verify_docs_links.py` afterwards.

## Testing / verification

- Host suite (Ninja + clang++ per this machine): full suite green; new
  `LedLayout` tests under `[control_surface]`.
- Firmware is not buildable headlessly — bench items: band alignment vs touch
  pads on both pairs, notice timing/duration, encoder line, theme cycling
  through all 10 themes.

## Risks / notes

- `smoothedTargetColorBuffer` and all clear loops size off
  `MATRIX_TOTAL_LEDS` — they shrink automatically; no manual size edits.
- Settings-menu and voice-parameter OLED/LED views already stay within 4 rows.
- Concurrent sessions touch this tree — commit the spec and code changes in
  separate, path-scoped commits; re-check `git status` before each commit.
