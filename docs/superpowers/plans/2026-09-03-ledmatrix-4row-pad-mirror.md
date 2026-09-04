# LED Matrix 8×4 Pad-Mirror Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Shrink the LED panel to 8×4 (32 LEDs) so LEDs mirror the 4×8 touch matrix 1:1, move the deleted control-cluster information to OLED notices, and make all 10 themes real.

**Architecture:** One new pure-C++ `ControlSurface::LedLayout` helper owns the band/step → LED-index math (host-tested against the touch-pad geometry); `LEDMatrixFeedback.cpp` renderers all route through it; `LEDController.*` and the dead `updateGateLEDs()` are deleted; `flash23/25/31Until` UIState fields become an `oledNotice*` transient rendered by `oled.cpp`.

**Tech Stack:** Arduino sketch (firmware, not buildable headlessly), Catch2 host test suite via CMake+Ninja+clang++, FastLED (unchanged), Adafruit_SH110X (unchanged).

**Spec:** `docs/superpowers/specs/2026-09-03-ledmatrix-4row-pad-mirror-design.md`

## Global Constraints

- **Invariant:** for the same (band, step), LED linear index == touch pad index (band 0 = pair low voice = indices 0–15, band 1 = pair high voice = indices 16–31).
- `src/ui/ControlSurfaceLogic.h/.cpp` stays pure C++ — **no** `Arduino.h`/`FastLED.h` includes (they would break the host build).
- **No heap allocation** in any render path; all edited code already runs on Core 1's 20 ms frame — keep it there.
- `src/LEDMatrix/` and `src/OLED/` are hardware-bound and **not** compiled by the host suite — their correctness is verified by careful review + bench, only `ControlSurfaceLogic` changes are host-verified.
- Host test toolchain on this machine (MSVC generator is broken here):
  ```bash
  cmake -B build_test -G Ninja -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang++.exe" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_USE_MATH_DEFINES"
  cmake --build build_test --parallel
  ./build_test/tests/pico2seq_tests "[control_surface]"
  ./build_test/tests/pico2seq_tests --reporter console
  ```
- A concurrent session may touch `README.md` / `src/voice/VoicePresets.cpp` — re-check `git status` before every commit; stage only paths this plan edited.
- Voice indices are 0-based (0–3) internally; OLED copy may add +1 for display.

---

### Task 1: `ControlSurface::LedLayout` helper (TDD)

**Files:**
- Modify: `src/ui/ControlSurfaceLogic.h` (append after the `PadBank` class, before the `ShiftLatch` section)
- Modify: `src/ui/ControlSurfaceLogic.cpp` (only if any helper code needs out-of-line definition — the design keeps it header-inline, so likely untouched)
- Test: `tests/unit/test_control_surface_logic.cpp` (append a `LedLayout` section)

**Interfaces:**
- Consumes: nothing (self-contained constexprs).
- Produces: `ControlSurface::LedLayout` with `kWidth=8`, `kRowsPerBand=2`, `kStepsPerBand=16`, `kBandCount=2`, `kLedCount=32`, `static constexpr uint8_t bandOfVoiceInPair(uint8_t)`, `static constexpr int linearIndex(uint8_t, uint8_t)`, `static constexpr int x(uint8_t)`, `static constexpr int y(uint8_t, uint8_t)`. Later tasks call these exact names.

- [ ] **Step 1: Write the failing tests** — append to `tests/unit/test_control_surface_logic.cpp` (before the final `ShiftLatch`/`FaderMap` sections is fine; order doesn't matter):

```cpp
// ---------------------------------------------------------------------------
// LedLayout (pad-mirror LED geometry)
// ---------------------------------------------------------------------------

TEST_CASE("LedLayout mirrors the touch-pad geometry", "[control_surface]")
{
    // The 8x4 LED panel mirrors the 4x8 touch matrix: band b covers LED
    // linear indices b*16..b*16+15, and touch pads b*16..b*16+15 occupy
    // rows 2b..2b+1. The same (band, step) must resolve to the same index
    // on both surfaces.
    for (uint8_t band = 0; band < LedLayout::kBandCount; ++band)
    {
        for (uint8_t step = 0; step < LedLayout::kStepsPerBand; ++step)
        {
            const uint8_t padRow = static_cast<uint8_t>(2 * band + step / LedLayout::kWidth);
            const uint8_t padCol = static_cast<uint8_t>(step % LedLayout::kWidth);
            const uint8_t padIndex = static_cast<uint8_t>(padRow * LedLayout::kWidth + padCol);
            CHECK(LedLayout::linearIndex(band, step) == static_cast<int>(padIndex));
            CHECK(LedLayout::x(step) == static_cast<int>(padCol));
            CHECK(LedLayout::y(band, step) == static_cast<int>(padRow));
        }
    }
    CHECK(LedLayout::kLedCount == 32);
    CHECK(LedLayout::kStepsPerBand == 16);
}

TEST_CASE("LedLayout rejects out-of-range coordinates", "[control_surface]")
{
    CHECK(LedLayout::linearIndex(LedLayout::kBandCount, 0) == -1);
    CHECK(LedLayout::linearIndex(0, LedLayout::kStepsPerBand) == -1);
    CHECK(LedLayout::y(LedLayout::kBandCount, 0) == -1);
    CHECK(LedLayout::y(0, LedLayout::kStepsPerBand) == -1);
    CHECK(LedLayout::x(LedLayout::kStepsPerBand) == -1);
}

TEST_CASE("LedLayout maps selected voices onto pair bands", "[control_surface]")
{
    CHECK(LedLayout::bandOfVoiceInPair(0) == 0);
    CHECK(LedLayout::bandOfVoiceInPair(1) == 1);
    CHECK(LedLayout::bandOfVoiceInPair(2) == 0);
    CHECK(LedLayout::bandOfVoiceInPair(3) == 1);
    CHECK(LedLayout::bandOfVoiceInPair(4) == 0); // clamped like PadBank::pairFor
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build_test --parallel && ./build_test/tests/pico2seq_tests "[control_surface]"`
Expected: compile ERROR — `LedLayout` is not a member of `ControlSurface` (or, if using a stale build dir, re-run the configure command from Global Constraints first).

- [ ] **Step 3: Implement the helper** — in `src/ui/ControlSurfaceLogic.h`, insert between the end of the `PadBank` class and the `// Shift latch semantics` comment:

```cpp
// ---------------------------------------------------------------------------
// LED layout (pad-mirror geometry)
// ---------------------------------------------------------------------------

/**
 * @brief Linear LED-index geometry for the 8x4 WS2812B panel.
 *
 * The LED panel mirrors the 4x8 MPR121 touch matrix: band b (0 = the voice
 * pair's low voice, 1 = its high voice) occupies LED linear indices
 * b*16..b*16+15 — exactly the pad indices of touch rows 2b..2b+1. All step
 * rendering in src/LEDMatrix/ must go through this helper so the two
 * surfaces cannot drift apart. The constexprs intentionally duplicate
 * LEDConstants.h, which cannot be included here (it pulls in FastLED);
 * LEDMatrixFeedback.cpp static_asserts the agreement.
 */
class LedLayout
{
public:
  static constexpr uint8_t kWidth = 8;                            // LEDs/pads per row
  static constexpr uint8_t kRowsPerBand = 2;                      // rows per voice band
  static constexpr uint8_t kStepsPerBand = kWidth * kRowsPerBand; // 16 steps per voice
  static constexpr uint8_t kBandCount = 2;                        // voices visible at once
  static constexpr uint8_t kLedCount = kStepsPerBand * kBandCount; // 32 LEDs

  /** Band (0/1) of a selected voice (0..3, clamped like PadBank::pairFor). */
  static constexpr uint8_t bandOfVoiceInPair(uint8_t selectedVoiceIndex)
  {
    if (selectedVoiceIndex >= 4)
    {
      selectedVoiceIndex = 0;
    }
    return static_cast<uint8_t>(selectedVoiceIndex & 1u);
  }

  /** Linear LED index for (band, step); -1 when out of range. */
  static constexpr int linearIndex(uint8_t band, uint8_t step)
  {
    if (band >= kBandCount || step >= kStepsPerBand)
    {
      return -1;
    }
    return static_cast<int>(band * kStepsPerBand + step);
  }

  /** X column for a step; -1 when out of range. */
  static constexpr int x(uint8_t step)
  {
    return (step < kStepsPerBand) ? static_cast<int>(step % kWidth) : -1;
  }

  /** Y row for (band, step); -1 when out of range. */
  static constexpr int y(uint8_t band, uint8_t step)
  {
    if (band >= kBandCount || step >= kStepsPerBand)
    {
      return -1;
    }
    return static_cast<int>(band * kRowsPerBand + step / kWidth);
  }
};
```

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build_test --parallel && ./build_test/tests/pico2seq_tests "[control_surface]"`
Expected: PASS — all `LedLayout` cases green, existing 23 cases unaffected.

- [ ] **Step 5: Commit**

```bash
git add src/ui/ControlSurfaceLogic.h tests/unit/test_control_surface_logic.cpp
git commit -m "feat(ui): add host-tested LedLayout pad-mirror geometry helper"
```

---

### Task 2: Geometry constants + LEDMatrixFeedback rewire

**Files:**
- Modify: `src/LEDMatrix/LEDConstants.h:15-18,46-48`
- Modify: `src/LEDMatrix/LEDMatrixFeedback.cpp` (whole-file rewire; exact blocks below)

**Interfaces:**
- Consumes: `ControlSurface::LedLayout` from Task 1 (`#include "../ui/ControlSurfaceLogic.h"`).
- Produces: rendering that only touches LED indices 0–31; `renderVoicePair(...)` and `addPolyrhythmicOverlay(...)` signatures change from offset/bool args to a `uint8_t band` arg (both are `static`/file-local except `updateStepLEDs`, whose public signature is unchanged).

- [ ] **Step 1: Update `LEDConstants.h`** — replace:

```cpp
  static constexpr uint8_t MATRIX_WIDTH = 8;
  static constexpr uint8_t MATRIX_HEIGHT = 8;
```
with:
```cpp
  static constexpr uint8_t MATRIX_WIDTH = 8;
  static constexpr uint8_t MATRIX_HEIGHT = 4; // 8x4 panel: mirrors the 4x8 touch matrix
```
and replace the layout block:
```cpp  static constexpr uint8_t TOP_HALF_OFFSET = 0;
  static constexpr uint8_t BOTTOM_HALF_OFFSET = 24;  // Row 4 start for 8x8 matrix
  static constexpr uint8_t VOICE_PAIR_SEPARATION = 3;  // Rows between voice pairs
```
with:
```cpp
  static constexpr uint8_t TOP_HALF_OFFSET = 0;          // Band 0 start (pair low voice)
  static constexpr uint8_t BOTTOM_HALF_OFFSET = 16;      // Band 1 start (pair high voice; touch rows 2-3)
  static constexpr uint8_t VOICE_PAIR_SEPARATION = 1;    // Rows between pair bands in an 8x4 matrix
```
(`MATRIX_TOTAL_LEDS` stays `MATRIX_WIDTH * MATRIX_HEIGHT` — it becomes 32 automatically.)

- [ ] **Step 2: Add include + static asserts** — in `LEDMatrixFeedback.cpp`, add `#include "../ui/ControlSurfaceLogic.h"` to the include block, and directly after the `static constexpr uint8_t SEQ_STEPS = 16;` line add:

```cpp
// The host-tested layout helper must agree with the hardware constants.
static_assert(LEDConstants::MATRIX_WIDTH == ControlSurface::LedLayout::kWidth);
static_assert(LEDConstants::MATRIX_HEIGHT == ControlSurface::LedLayout::kBandCount * ControlSurface::LedLayout::kRowsPerBand);
static_assert(LEDConstants::MATRIX_TOTAL_LEDS == ControlSurface::LedLayout::kLedCount);
static_assert(LEDConstants::BOTTOM_HALF_OFFSET == ControlSurface::LedLayout::kStepsPerBand);
```

- [ ] **Step 3: Delete the dead `updateGateLEDs()`** — remove the entire function (the block from `void updateGateLEDs(` through its closing brace, `LEDMatrixFeedback.cpp:618-697`, including its doc comment at 510-527). It is never called and hardcodes voices 1/2.

- [ ] **Step 4: Rewrite `addPolyrhythmicOverlay`** — replace the whole function with:

```cpp
void addPolyrhythmicOverlay(
    LEDMatrix &ledMatrix,
    const Sequencer &sequencer,
    uint8_t band,
    uint8_t overlayIntensity = LEDConstants::POLYRHYTHM_INTENSITY)
{
    // Only add overlay if sequencer is actively running
    if (!sequencer.isRunning())
    {
        return;
    }

    // Parameter overlay configuration for polyrhythmic visualization
    struct PolyrhythmicParameterOverlay
    {
        ParamId parameterID;
        CRGB overlayColor;
    };

    const PolyrhythmicParameterOverlay overlayParameters[LEDConstants::POLYRHYTHM_PARAM_COUNT] = {
        {ParamId::Note, LEDColors::POLYRHYTHM_NOTE},
        {ParamId::Velocity, LEDColors::POLYRHYTHM_VELOCITY},
        {ParamId::Filter, LEDColors::POLYRHYTHM_FILTER}};

    // Apply overlay for each parameter type
    for (size_t paramIndex = 0; paramIndex < LEDConstants::POLYRHYTHM_PARAM_COUNT; ++paramIndex)
    {
        const ParamId currentParameter = overlayParameters[paramIndex].parameterID;
        const uint8_t currentParameterStep = sequencer.getCurrentStepForParameter(currentParameter);
        const uint8_t parameterStepCount = sequencer.getParameterStepCount(currentParameter);

        // Only apply overlay if parameter is within valid bounds
        if (currentParameterStep < LEDConstants::MAX_STEP_BUTTONS &&
            parameterStepCount > 1 &&
            parameterStepCount <= LEDConstants::MAX_STEP_BUTTONS)
        {

            const int ledLinearIndex = ControlSurface::LedLayout::linearIndex(band, currentParameterStep);
            if (ledLinearIndex < 0)
            {
                continue;
            }
            CRGB currentLEDColor = ledMatrix.getLeds()[ledLinearIndex];

            // Blend overlay color with existing LED color
            currentLEDColor += overlayParameters[paramIndex].overlayColor;

            ledMatrix.setLED(ControlSurface::LedLayout::x(currentParameterStep),
                             ControlSurface::LedLayout::y(band, currentParameterStep),
                             currentLEDColor);
        }
    }
}
```

- [ ] **Step 5: Rewrite `renderVoicePair`** — replace the whole function with (body logic unchanged, only positioning goes through `LedLayout`):

```cpp
static void renderVoicePair(
    LEDMatrix &ledMatrix,
    const Sequencer &firstVoiceSequencer,
    const Sequencer &secondVoiceSequencer,
    const LEDThemeColors *themeColors,
    uint8_t band)
{
    // Validate sequencer gate step counts
    const uint8_t firstVoiceGateStepCount = firstVoiceSequencer.getParameterStepCount(ParamId::Gate);
    const uint8_t secondVoiceGateStepCount = secondVoiceSequencer.getParameterStepCount(ParamId::Gate);

    if (firstVoiceGateStepCount == 0)
    {
        DBG_WARN("renderVoicePair: First voice has zero gate step count");
        return;
    }
    if (secondVoiceGateStepCount == 0)
    {
        DBG_WARN("renderVoicePair: Second voice has zero gate step count");
        return;
    }

    // Render each step for both voices in the pair
    for (int stepIndex = 0; stepIndex < LEDConstants::MAX_STEP_BUTTONS; ++stepIndex)
    {
        // === First Voice (band top row pair) Processing ===
        const Step &firstVoiceStep = firstVoiceSequencer.getStep(stepIndex);
        const bool isFirstVoicePlayhead = (firstVoiceSequencer.getCurrentStepForParameter(ParamId::Gate) == stepIndex &&
                                           firstVoiceSequencer.isRunning());

        CRGB firstVoiceColor = firstVoiceStep.isGateActive ? themeColors->gateOnV1 : themeColors->gateOffV1;

        if (firstVoiceSequencer.getStepParameterValue(ParamId::Slide, stepIndex) > 0)
        {
            nblend(firstVoiceColor, themeColors->modSlideActive, LEDConstants::MEDIUM_BRIGHTNESS);
        }

        if (isFirstVoicePlayhead)
        {
            firstVoiceColor += themeColors->playheadAccent;
        }

        const int topRowLEDIndex = ControlSurface::LedLayout::linearIndex(band, stepIndex);
        nblend(smoothedTargetColorBuffer[topRowLEDIndex], firstVoiceColor, TARGET_SMOOTHING_BLEND_AMOUNT);
        nblend(ledMatrix.getLeds()[topRowLEDIndex], smoothedTargetColorBuffer[topRowLEDIndex],
               LEDConstants::STANDARD_BLEND_AMOUNT);

        // === Second Voice (other band) Processing ===
        const Step &secondVoiceStep = secondVoiceSequencer.getStep(stepIndex);
        const bool isSecondVoicePlayhead = (secondVoiceSequencer.getCurrentStepForParameter(ParamId::Gate) == stepIndex &&
                                            secondVoiceSequencer.isRunning());

        CRGB secondVoiceColor = secondVoiceStep.isGateActive ? themeColors->gateOnV2 : themeColors->gateOffV2;

        if (secondVoiceSequencer.getStepParameterValue(ParamId::Slide, stepIndex) > 0)
        {
            nblend(secondVoiceColor, themeColors->modSlideActive, LEDConstants::MEDIUM_BRIGHTNESS);
        }

        if (isSecondVoicePlayhead)
        {
            secondVoiceColor += themeColors->playheadAccent;
        }

        const int bottomRowLEDIndex = ControlSurface::LedLayout::linearIndex(
            static_cast<uint8_t>(band + 1), stepIndex);
        nblend(smoothedTargetColorBuffer[bottomRowLEDIndex], secondVoiceColor, TARGET_SMOOTHING_BLEND_AMOUNT);
        nblend(ledMatrix.getLeds()[bottomRowLEDIndex], smoothedTargetColorBuffer[bottomRowLEDIndex],
               LEDConstants::STANDARD_BLEND_AMOUNT);
    }
}
```

- [ ] **Step 6: Rewire `updateStepLEDs` blocks** — inside `updateStepLEDs`, make these exact replacements (all offsets now flow from `LedLayout`):

  a. **Gate-length mode block** — replace
```cpp
        const bool isSecondInPair = (uiState.selectedVoiceIndex % 2) == 1;
        const int baseOffset = isSecondInPair ? LED_MATRIX_BOTTOM_HALF_OFFSET : 0; // fixed offset for bottom row
```
with
```cpp
        const uint8_t selBand = ControlSurface::LedLayout::bandOfVoiceInPair(uiState.selectedVoiceIndex);
        const bool isSecondInPair = selBand == 1;
        const int baseOffset = ControlSurface::LedLayout::linearIndex(selBand, 0);
```
and replace (same block)
```cpp
            const int otherIndex = (isSecondInPair ? 0 : LED_MATRIX_BOTTOM_HALF_OFFSET) + step;
```
with
```cpp
            const int otherIndex = ControlSurface::LedLayout::linearIndex(static_cast<uint8_t>(1 - selBand), step);
```
and replace
```cpp
            const int ledIndex = baseOffset + step;
```
with
```cpp
            const int ledIndex = ControlSurface::LedLayout::linearIndex(selBand, step);
```

  b. **Slide mode block** — replace
```cpp
            int x = step % LEDMatrix::WIDTH;
            int y = step / LEDMatrix::WIDTH;
            // Place on top/bottom half based on voice within pair (0/1 top, 2/3 page uses same rows)
            if ((uiState.selectedVoiceIndex % 2) == 1)
            {
                y += 3; // second voice in pair uses lower band
            }
            ledMatrix.setLED(x, y, color);
```
with
```cpp
            const int x = ControlSurface::LedLayout::x(step);
            const int y = ControlSurface::LedLayout::y(
                ControlSurface::LedLayout::bandOfVoiceInPair(uiState.selectedVoiceIndex), step);
            if (x >= 0 && y >= 0)
            {
                ledMatrix.setLED(x, y, color);
            }
```

  c. **Parameter-edit block** — replace
```cpp
        bool isSecondInPair = (uiState.selectedVoiceIndex % 2) == 1;
        for (int step = 0; step < SEQ_STEPS; ++step)
        {
            int topIndex = step;
            int bottomIndex = LED_MATRIX_BOTTOM_HALF_OFFSET + step; // Fixed offset for 8x8 matrix
```
with
```cpp
        const uint8_t selBand = ControlSurface::LedLayout::bandOfVoiceInPair(uiState.selectedVoiceIndex);
        bool isSecondInPair = selBand == 1;
        for (int step = 0; step < SEQ_STEPS; ++step)
        {
            int topIndex = ControlSurface::LedLayout::linearIndex(0, step);
            int bottomIndex = ControlSurface::LedLayout::linearIndex(1, step);
```
and in the same block replace both occurrences of the pattern `(isSecondInPair ? LED_MATRIX_BOTTOM_HALF_OFFSET : 0) + step` with `ControlSurface::LedLayout::linearIndex(selBand, step)` (the paint loop's `int ledIndex = ...` line).

  d. **`anyParamForLengthHeld` block** — replace
```cpp
        bool isSecondInPair = (uiState.selectedVoiceIndex % 2) == 1;
```
with
```cpp
        const uint8_t selBand = ControlSurface::LedLayout::bandOfVoiceInPair(uiState.selectedVoiceIndex);
        bool isSecondInPair = selBand == 1;
```
then replace `(isSecondInPair ? LED_MATRIX_BOTTOM_HALF_OFFSET : 0) + step` with `ControlSurface::LedLayout::linearIndex(selBand, step)` and `(isSecondInPair ? 0 : LED_MATRIX_BOTTOM_HALF_OFFSET) + step` with `ControlSurface::LedLayout::linearIndex(static_cast<uint8_t>(1 - selBand), step)` in both loops of this block.

  e. **Main branch** — replace the two `renderVoicePair` calls' last arg: keep `0` (band base) for both pages (the grid position is identical; only the sequencers differ), and replace the overlay calls
```cpp
            addPolyrhythmicOverlay(ledMatrix, seq1, false, 32);
            addPolyrhythmicOverlay(ledMatrix, seq2, true, 32);
```
with
```cpp
            addPolyrhythmicOverlay(ledMatrix, seq1, 0, 32);
            addPolyrhythmicOverlay(ledMatrix, seq2, 1, 32);
```
(and likewise `seq3`/`seq4` in the else branch). Finally replace the step-edit blink index
```cpp
            int ledIndex = uiState.selectedVoiceIndex % 2 == 1 ? (LED_MATRIX_BOTTOM_HALF_OFFSET + uiState.selectedStepForEdit) : uiState.selectedStepForEdit; // Fixed offset
```
with
```cpp
            int ledIndex = ControlSurface::LedLayout::linearIndex(
                ControlSurface::LedLayout::bandOfVoiceInPair(uiState.selectedVoiceIndex),
                static_cast<uint8_t>(uiState.selectedStepForEdit));
```
and delete the now-unused line `static constexpr int LED_MATRIX_BOTTOM_HALF_OFFSET = LEDConstants::BOTTOM_HALF_OFFSET;` near the top of the file.

- [ ] **Step 7: Self-review pass (compensating control — this file is not host-compiled)**

Re-read the full `LEDMatrixFeedback.cpp` top to bottom and verify: no remaining reference to `LED_MATRIX_BOTTOM_HALF_OFFSET`; every linear index comes from `LedLayout::linearIndex` (0..31 only); `updateSettingsModeLEDs` and `updateVoiceParameterLEDs` write only rows 0–2 (they do — presets at `y+1`, menu at `y=0`, param LED at `button-1 ≤ 23`); all clear loops use `MATRIX_TOTAL_LEDS` / `WIDTH*HEIGHT` (auto-shrunk to 32).
Run: `cmake --build build_test --parallel && ./build_test/tests/pico2seq_tests --reporter console`
Expected: full suite PASS (unchanged — guards the helper, not this file).

- [ ] **Step 8: Commit**

```bash
git add src/LEDMatrix/LEDConstants.h src/LEDMatrix/LEDMatrixFeedback.cpp
git commit -m "feat(led): 8x4 pad-mirror geometry, route all rendering through LedLayout"
```

---

### Task 3: Delete control cluster; UIState notices

**Files:**
- Delete: `src/LEDMatrix/LEDController.h`, `src/LEDMatrix/LEDController.cpp`
- Modify: `includes.h:26` (remove the include)
- Modify: `Pico2Seq.ino:804` (remove `initLEDController();`) and `Pico2Seq.ino:1058` (remove `updateControlLEDs(ledMatrix, uiState);`)
- Modify: `src/ui/UIState.h:34-36` (replace flash fields with notice fields)
- Modify: `src/ui/ButtonHandlers.cpp:82,218,255`
- Modify: `src/ui/AlchemyControlBridge.cpp:146`
- Modify: `src/ui/ButtonManager.cpp:93-97`
- Modify: `src/ui/UIConstants.h:42` (replace flash duration with notice duration)

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: `UIState::oledNoticeUntil` (`volatile unsigned long`), `UIState::oledNoticeKind` (`OledNoticeKind`), `UIState::oledNoticeVoice` (`uint8_t`), `enum class OledNoticeKind : uint8_t { None, DelayOn, DelayOff, Randomized }` (declared inside `UIState` like `SettingsSubMode`), and `UIConstants::OLED_NOTICE_DURATION_MS = 800`. Task 4 consumes these exact names.

- [ ] **Step 1: Remove the call sites and files**

Delete from `includes.h`:
```cpp
#include "src/LEDMatrix/LEDController.h"
```
Delete from `Pico2Seq.ino` (setup, line ~804):
```cpp
    initLEDController();
```
Delete from `Pico2Seq.ino` (20 ms LED frame, line ~1058) so the frame reads `updateStepLEDs(...)` then `ledMatrix.show();`:
```cpp
        updateControlLEDs(ledMatrix, uiState);
```
Then `git rm src/LEDMatrix/LEDController.h src/LEDMatrix/LEDController.cpp`.

- [ ] **Step 2: Replace the flash fields in `UIState.h`** — replace

```cpp
    volatile unsigned long flash23Until = 0;
    volatile unsigned long flash25Until = 0;
    volatile unsigned long flash31Until = 0;
```
with
```cpp
    // --- Transient OLED notice (replaces the old control-cluster LED flashes) ---
    enum class OledNoticeKind : uint8_t { None = 0, DelayOn, DelayOff, Randomized };
    volatile unsigned long oledNoticeUntil = 0;
    volatile OledNoticeKind oledNoticeKind = OledNoticeKind::None;
    volatile uint8_t oledNoticeVoice = 0; // 0-based voice, valid for Randomized
```

- [ ] **Step 3: Add the duration constant** — in `UIConstants.h` replace

```cpp
constexpr unsigned long CONTROL_LED_FLASH_DURATION_MS = 250;       // ms: brief confirmation flash for control actions
```
with
```cpp
constexpr unsigned long OLED_NOTICE_DURATION_MS = 800;             // ms: transient OLED confirmation notice
```
(then update any other `CONTROL_LED_FLASH_DURATION_MS` references — grep first; after Step 4 there should be none).

- [ ] **Step 4: Re-point the trigger sites** — in `ButtonHandlers.cpp`:

  a. Randomize (`handleRandomizeButton`, line ~82): delete `state.flash31Until = millis() + CONTROL_LED_FLASH_DURATION_MS;` and instead set the notice only when randomization actually ran — inside the `if (!isLongPress(heldTime))` branch, right after `seq->randomizeParameters();` add:
```cpp
        state.oledNoticeKind = UIState::OledNoticeKind::Randomized;
        state.oledNoticeVoice = static_cast<uint8_t>(voiceIndex);
        state.oledNoticeUntil = millis() + OLED_NOTICE_DURATION_MS;
```
  b. Play/stop (line ~218): delete `state.flash25Until = millis() + CONTROL_LED_FLASH_DURATION_MS;` (no replacement — had no visible consumer).
  c. Delay toggle (`BUTTON_TOGGLE_DELAY`, line ~255): replace `state.flash23Until = millis() + CONTROL_LED_FLASH_DURATION_MS;` with
```cpp
        state.oledNoticeKind = state.delayOn ? UIState::OledNoticeKind::DelayOn : UIState::OledNoticeKind::DelayOff;
        state.oledNoticeUntil = millis() + OLED_NOTICE_DURATION_MS;
```

In `AlchemyControlBridge.cpp` (line ~146): delete `uiState.flash31Until = nowMs + CONTROL_LED_FLASH_DURATION_MS;` (the mode banner on the previous line already covers the flip; also update the comment above that mentions "flash a control LED" to say "raise the OLED banner").

In `ButtonManager.cpp` (lines ~93-97) replace the three flash resets with:
```cpp
  // Reset transient OLED notice state
  uiState.oledNoticeUntil = 0;
  uiState.oledNoticeKind = UIState::OledNoticeKind::None;
  uiState.oledNoticeVoice = 0;
```

- [ ] **Step 5: Verify nothing dangles + suite green**

Run: `grep -rn "flash23Until\|flash25Until\|flash31Until\|CONTROL_LED_FLASH_DURATION_MS\|updateControlLEDs\|initLEDController\|ControlLEDIndices" src Pico2Seq.ino includes.h tests`
Expected: no matches.
Run: `cmake --build build_test --parallel && ./build_test/tests/pico2seq_tests --reporter console`
Expected: PASS (none of these symbols were host-compiled, but UIState.h is included by tests — this catches syntax errors).

- [ ] **Step 6: Commit**

```bash
git add -A src/LEDMatrix/LEDController.h src/LEDMatrix/LEDController.cpp includes.h Pico2Seq.ino src/ui/UIState.h src/ui/ButtonHandlers.cpp src/ui/AlchemyControlBridge.cpp src/ui/ButtonManager.cpp src/ui/UIConstants.h
git commit -m "refactor(led): drop 8x8 control cluster; flash fields become OLED notice state"
```

---

### Task 4: OLED notice screen + encoder status line

**Files:**
- Modify: `src/OLED/oled.cpp` (banner block ~line 225 for the notice; default status screen `else` branch ~line 365 for the encoder line)

**Interfaces:**
- Consumes: `UIState::oledNoticeUntil/oledNoticeKind/oledNoticeVoice`, `UIConstants::OLED_NOTICE_DURATION_MS` (Task 3); `EncoderParameterMode` from `SequencerDefs.h` (already included via UIState.h).
- Produces: visible behavior only.

- [ ] **Step 1: Render the notice** — in `OLEDDisplay::update(...)`, directly after the `alchemyModeBannerUntil` block closes and before the `if (uiState.settingsMode)` block, insert:

```cpp
  // Transient confirmation notice (replaces the old control-cluster LED
  // flashes). Shown below the PARAM/UTIL banner, above everything else,
  // then the previous view resumes.
  if (uiState.oledNoticeUntil != 0 && millis() < uiState.oledNoticeUntil &&
      uiState.oledNoticeKind != UIState::OledNoticeKind::None)
  {
    const char *line1 = nullptr;
    if (uiState.oledNoticeKind == UIState::OledNoticeKind::DelayOn)
    {
      line1 = "DELAY ON";
    }
    else if (uiState.oledNoticeKind == UIState::OledNoticeKind::DelayOff)
    {
      line1 = "DELAY OFF";
    }
    else
    {
      line1 = "RANDOMIZED";
    }

    displayHardware.setTextSize(2);
    const uint8_t line1Width = static_cast<uint8_t>(strlen(line1) * 12); // size-2 chars are 12px wide
    displayHardware.setCursor((OLEDConstants::SCREEN_WIDTH - line1Width) / 2, 16);
    displayHardware.print(line1);

    if (uiState.oledNoticeKind == UIState::OledNoticeKind::Randomized)
    {
      displayHardware.setTextSize(1);
      char voiceLine[12];
      snprintf(voiceLine, sizeof(voiceLine), "Voice %u", static_cast<unsigned>(uiState.oledNoticeVoice) + 1);
      const uint8_t voiceLineWidth = static_cast<uint8_t>(strlen(voiceLine) * 6);
      displayHardware.setCursor((OLEDConstants::SCREEN_WIDTH - voiceLineWidth) / 2, 44);
      displayHardware.print(voiceLine);
    }

    displayHardware.display();
    return;
  }
```

- [ ] **Step 2: Encoder line on the default status screen** — add a file-local helper above `OLEDDisplay::update` (near `paramName()`):

```cpp
// Short label for the parameter the magnetic encoder currently controls.
static const char *encoderParamName(EncoderParameterMode mode)
{
  switch (mode)
  {
  case EncoderParameterMode::Velocity:      return "Velocity";
  case EncoderParameterMode::Filter:        return "Filter";
  case EncoderParameterMode::Attack:        return "Attack";
  case EncoderParameterMode::Decay:         return "Decay";
  case EncoderParameterMode::Note:          return "Note";
  case EncoderParameterMode::DelayTime:     return "DelayTime";
  case EncoderParameterMode::DelayFeedback: return "DelayFdbk";
  case EncoderParameterMode::SlideTime:     return "SlideTime";
  default:                                  return "-";
  }
}
extern float getEncoderParameterValue(); // defined in src/sensors/EncoderManager.cpp
```

Then in the default-screen `else` branch (the one with scale/shuffle/Voice/`drawStepIndicators`), immediately before `drawStepIndicators(currentSequencerDefault, 63);` add:

```cpp
    // Encoder control line (replaces the old control-cluster value-fade LED):
    // shows which parameter the encoder drives and its live value.
    if (uiState.currentEncoderParameter != EncoderParameterMode::COUNT)
    {
      displayHardware.setTextSize(1);
      displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 53);
      displayHardware.print("ENC:");
      displayHardware.print(encoderParamName(uiState.currentEncoderParameter));
      displayHardware.print(" ");
      displayHardware.print(getEncoderParameterValue(), 2);
    }
```

- [ ] **Step 3: Review pass** — re-read the modified regions: notice block must `return` after `display()`; the encoder line must not shift the Voice/indicator drawing (it draws last, at y=53, size 1 — the voice number at size 3 ends by y≈49).

- [ ] **Step 4: Suite sanity + commit**

Run: `cmake --build build_test --parallel && ./build_test/tests/pico2seq_tests --reporter console`
Expected: PASS (oled.cpp is not host-compiled; this only guards accidental UIState breakage).
```bash
git add src/OLED/oled.cpp
git commit -m "feat(oled): transient delay/randomize notices + encoder parameter line"
```

---

### Task 5: Restore VOLCANIC / FOREST / NEON palettes

**Files:**
- Modify: `src/LEDMatrix/LEDMatrixFeedback.cpp` (`ALL_THEMES[]`, insert three entries between the OCEANIC entry and the DARK_NOCTIS entry)

**Interfaces:**
- Consumes: `LEDTheme` enum order (DEFAULT, OCEANIC, VOLCANIC, FOREST, NEON, MODERN, DARK_NOCTIS, DARK_EMBER, BLUE, GREEN) — unchanged.
- Produces: `ALL_THEMES` with exactly 10 entries, index-aligned with the enum.

- [ ] **Step 1: Insert the three palettes** after the OCEANIC entry's closing brace (field order: gateOnV1, gateOffV1, gateOnV2, gateOffV2, playheadAccent, idleBreathingBlue, editModeDimBlueV1, editModeDimBlueV2, modNoteActive, modNoteInactive, modVelocityActive, modVelocityInactive, modFilterActive, modFilterInactive, modDecayActive, modDecayInactive, modAttackActive, modAttackInactive, modOctaveActive, modOctaveInactive, modSlideActive, modSlideInactive, defaultActive, defaultInactive, modParamModeActive, modParamModeInactive, modGateModeActive, modGateModeInactive, randomizeFlash, randomizeIdle):

```cpp
    {
        // VOLCANIC theme - red/orange fire on near-black
        CRGB(220, 80, 20),   // gateOnV1 - ember red-orange
        CRGB(10, 4, 2),      // gateOffV1 - near-black
        CRGB(255, 140, 40),  // gateOnV2 - bright flame orange
        CRGB(8, 3, 2),       // gateOffV2 - deep dark
        CRGB(60, 18, 4),     // playheadAccent - dark lava accent
        CRGB(50, 20, 8),     // idleBreathingBlue - warm ember glow
        CRGB(12, 6, 4),      // editModeDimBlueV1 - very dark warm slate
        CRGB(14, 8, 5),      // editModeDimBlueV2
        CRGB(230, 150, 90),  // modNoteActive - warm beige-orange
        CRGB(30, 18, 12),    // modNoteInactive
        CRGB(240, 180, 120), // modVelocityActive - pale amber
        CRGB(32, 22, 16),    // modVelocityInactive
        CRGB(200, 90, 60),   // modFilterActive - muted terracotta red
        CRGB(28, 12, 8),     // modFilterInactive
        CRGB(255, 200, 120), // modDecayActive - bright amber
        CRGB(32, 24, 14),    // modDecayInactive
        CRGB(190, 120, 70),  // modAttackActive - muted copper
        CRGB(26, 16, 10),    // modAttackInactive
        CRGB(255, 120, 60),  // modOctaveActive - hot orange accent
        CRGB(30, 12, 6),     // modOctaveInactive
        CRGB(255, 160, 90),  // modSlideActive - warm slide accent
        CRGB(30, 18, 12),    // modSlideInactive
        CRGB(240, 220, 200), // defaultActive - warm light gray
        CRGB(16, 10, 8),     // defaultInactive - near-black
        CRGB(255, 170, 100), // modParamModeActive - warm pale
        CRGB(28, 18, 12),    // modParamModeInactive
        CRGB(255, 140, 60),  // modGateModeActive - bright ember highlight
        CRGB(26, 14, 8),     // modGateModeInactive
        CRGB(255, 220, 150), // randomizeFlash - bright warm flash
        CRGB(24, 14, 10)     // randomizeIdle - dark subtle tone
    },
    {
        // FOREST theme - greens and warm browns on dark moss
        CRGB(30, 160, 60),   // gateOnV1 - vivid leaf green
        CRGB(3, 10, 4),      // gateOffV1 - near-black green tint
        CRGB(120, 200, 80),  // gateOnV2 - pale moss accent
        CRGB(4, 10, 3),      // gateOffV2 - deep dark
        CRGB(10, 40, 14),    // playheadAccent - deep forest accent
        CRGB(16, 36, 18),    // idleBreathingBlue - deep moss breathing
        CRGB(6, 12, 7),      // editModeDimBlueV1 - dark green slate
        CRGB(8, 14, 9),      // editModeDimBlueV2
        CRGB(160, 220, 140), // modNoteActive - pale green
        CRGB(20, 30, 18),    // modNoteInactive
        CRGB(190, 230, 160), // modVelocityActive - soft mint
        CRGB(24, 32, 22),    // modVelocityInactive
        CRGB(110, 180, 120), // modFilterActive - muted green-teal
        CRGB(14, 24, 16),    // modFilterInactive
        CRGB(210, 190, 120), // modDecayActive - dry-grass warm contrast
        CRGB(28, 26, 16),    // modDecayInactive
        CRGB(140, 190, 110), // modAttackActive - sage
        CRGB(18, 26, 14),    // modAttackInactive
        CRGB(200, 150, 90),  // modOctaveActive - warm bark accent
        CRGB(28, 20, 12),    // modOctaveInactive
        CRGB(150, 220, 170), // modSlideActive - minty slide accent
        CRGB(18, 28, 22),    // modSlideInactive
        CRGB(220, 240, 210), // defaultActive - off-white green tint
        CRGB(10, 14, 10),    // defaultInactive - near-black
        CRGB(170, 230, 150), // modParamModeActive - bright leaf
        CRGB(20, 30, 20),    // modParamModeInactive
        CRGB(190, 210, 120), // modGateModeActive - lichen highlight
        CRGB(24, 28, 14),    // modGateModeInactive
        CRGB(230, 250, 180), // randomizeFlash - pale flash
        CRGB(14, 20, 12)     // randomizeIdle - dark subtle tone
    },
    {
        // NEON theme - bright cyan/magenta on dark
        CRGB(0, 230, 230),   // gateOnV1 - electric cyan
        CRGB(0, 12, 14),     // gateOffV1 - near-black cyan tint
        CRGB(255, 0, 180),   // gateOnV2 - hot magenta accent
        CRGB(14, 0, 10),     // gateOffV2 - deep dark
        CRGB(0, 60, 80),     // playheadAccent - deep cyan accent
        CRGB(0, 30, 60),     // idleBreathingBlue - neon blue breathing
        CRGB(0, 10, 16),     // editModeDimBlueV1 - dark cyan slate
        CRGB(10, 0, 12),     // editModeDimBlueV2
        CRGB(120, 255, 255), // modNoteActive - pale cyan
        CRGB(16, 30, 30),    // modNoteInactive
        CRGB(180, 255, 255), // modVelocityActive - ice cyan
        CRGB(20, 32, 32),    // modVelocityInactive
        CRGB(90, 120, 255),  // modFilterActive - electric indigo
        CRGB(12, 16, 34),    // modFilterInactive
        CRGB(255, 220, 60),  // modDecayActive - neon yellow contrast
        CRGB(32, 28, 8),     // modDecayInactive
        CRGB(140, 255, 120), // modAttackActive - neon green
        CRGB(18, 32, 16),    // modAttackInactive
        CRGB(255, 60, 255),  // modOctaveActive - magenta accent
        CRGB(32, 8, 32),     // modOctaveInactive
        CRGB(0, 255, 200),   // modSlideActive - spring neon slide
        CRGB(10, 30, 24),    // modSlideInactive
        CRGB(230, 230, 255), // defaultActive - pale violet-white
        CRGB(12, 12, 18),    // defaultInactive - near-black
        CRGB(80, 255, 180),  // modParamModeActive - neon mint
        CRGB(10, 30, 22),    // modParamModeInactive
        CRGB(255, 120, 220), // modGateModeActive - pink neon highlight
        CRGB(30, 12, 26),    // modGateModeInactive
        CRGB(255, 255, 255), // randomizeFlash - white flash
        CRGB(14, 14, 20)     // randomizeIdle - dark subtle tone
    },
```

- [ ] **Step 2: Verify count and cycler** — count entries in `ALL_THEMES`: must be exactly 10, and `static_assert`-style check by eye that the 3rd–5th entries are VOLCANIC/FOREST/NEON (matching enum indices 2–4). The `BUTTON_CHANGE_THEME` cycler (`ButtonHandlers.cpp` ~line 231) needs no change — `% LEDTheme::COUNT` now stays in bounds.

- [ ] **Step 3: Suite sanity + commit**

Run: `cmake --build build_test --parallel && ./build_test/tests/pico2seq_tests --reporter console`
Expected: PASS.
```bash
git add src/LEDMatrix/LEDMatrixFeedback.cpp
git commit -m "feat(led): restore VOLCANIC/FOREST/NEON palettes, fixing theme table OOB"
```

---

### Task 6: Docs pass + final verification

**Files:**
- Modify: `docs/LEDMatrix.md` (geometry, band map, control cluster → OLED, themes)
- Modify: `docs/architecture.md`, `docs/matrix.md`, `docs/oled.md` (only lines mentioning the LED matrix size/layout)
- Modify: `README.md` (only lines mentioning 8×8/64 LEDs — **check `git diff README.md` first; a concurrent session is editing it; skip if the section already changed**)
- Modify: `.agents/skills/pico2seq-codebase/references/ui-input.md` (file-map line: "FastLED WS2812B 8×8; 11 themes")

- [ ] **Step 1: `docs/LEDMatrix.md`** — update: dimensions line ("8 columns × 4 rows (32 total LEDs)"), `MATRIX_*` constant listing (HEIGHT 4, TOTAL 32, BOTTOM_HALF_OFFSET 16, VOICE_PAIR_SEPARATION 1), the grid diagram, the class snippet (`HEIGHT = 4`, `NUM_LEDS = 32`), the step-visualization sentence (rows 0–1 / rows 2–3 bands), the themes table (add VOLCANIC/FOREST/NEON rows, correct count to 10), and replace any control-cluster (`updateControlLEDs`) sections with a short "Control indicators moved to OLED" paragraph describing the transient notices and the encoder line.

- [ ] **Step 2: Sweep other docs** — `grep -rn "8x8\|8×8\|64 LED\|64 total" docs README.md .agents/skills/pico2seq-codebase` and fix only LED-matrix-related hits (docs/matrix.md may refer to the *touch* matrix being 4×8 — leave those; docs/oled.md/architecture.md hits about the LED matrix get the 8×4 wording). Then:

Run: `python tests/verify_docs_links.py`
Expected: OK (no broken links).

- [ ] **Step 3: Full verification**

Run: `cmake --build build_test --parallel && ./build_test/tests/pico2seq_tests --reporter console`
Expected: full suite PASS including the new LedLayout cases.
Run: `git status --short` — confirm only intended files are dirty.

- [ ] **Step 4: Commit**

```bash
git add docs/LEDMatrix.md docs/architecture.md docs/matrix.md docs/oled.md .agents/skills/pico2seq-codebase/references/ui-input.md
git add README.md   # only if Step 2 edited it
git commit -m "docs: LED matrix is 8x4 pad-mirror; control indicators on OLED; 10 themes"
```

---

## Bench items (cannot be verified headlessly)

- Band alignment: touch a pad in rows 2–3 → the matching LED in rows 2–3 lights/updates for both voice pairs.
- Notice duration ~800 ms and priority under the PARAM/UTIL banner.
- Theme cycling through all 10 themes without artifacts.
- FastLED output is now clocked for 32 LEDs — confirm no leftovers light on panels wired with more.
