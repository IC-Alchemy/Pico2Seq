# Migration Plan: MPR121 → AlchemyUI, AS5600 → TMAG5273

Status: **plan only, nothing implemented yet.** This document was produced by reading the
current source tree, the two vendored-but-unwired libraries, git history/status, and two
`.zcode/plans/*.md` notes left in the repo from a related planning session. Every claim below
is marked as confirmed (read directly in this repo's code) or unconfirmed (inferred, or true
as of a note written for a different project on a different date).

## 1. Assumptions

**{confirmed}** The touch input is `Adafruit_MPR121` driving a custom 32-button scanner
(`src/matrix/Matrix.cpp/h`, `MATRIX_BUTTON_COUNT = 32`), constructed in `Pico2Seq.ino` as
`Adafruit_MPR121 touchSensor`, initialized in `setup1()` via `touchSensor.begin(0x5A)` +
`Matrix_init(&touchSensor)`, polled every 1ms via `Matrix_scan()`, and dispatched through one
callback registered with `Matrix_setEventHandler()` that forwards `MatrixButtonEvent{buttonIndex,
type}` into `matrixEventHandler()`.

**{confirmed}** The 32 button indices are heavily overloaded by UI mode, not one-function-per-button:
- 0–15: step pads — also reinterpreted as voice-select (settings mode), preset picker (settings),
  voice-parameter toggles (voice-parameter sub-mode), and gate-track-length picker (while the
  AS5600-control button is long-held).
- 16–21: parameter "hold to record" buttons (Note/Velocity/Filter/Attack/Decay/Octave) — holding
  one also auto-selects that parameter as the magnetic encoder's live target.
- 22: dual-purpose — slide-mode toggle *and* a 7th parameter-record button ("Slide").
- 23: delay on/off toggle.
- 24: voice switch (cycles voice 0→1→2→3).
- 25: encoder-target cycle button; short-press cycles, long-press enters "gate sequence length"
  mode, and inside Settings it toggles the settings sub-mode instead.
- 26: play/stop; stopping also opens Settings.
- 27: scale change. 28: LED theme change. 29: shuffle/swing pattern change.
- 30–31: randomize, each covering two voices depending on a "page" flag (`resolveRandomizeVoiceIndex`).

**{confirmed}** The magnetic encoder (`src/sensors/as5600.cpp/h`, `AS5600Sensor`) is not just a
sensor driver — `AS5600ParameterMode` (an enum) and `AS5600BaseValues`/`AS5600BaseValuesVoice1`
(structs) are defined in `src/sequencer/SequencerDefs.h` itself, and `AS5600Manager.cpp/h`
(~40 functions) implements the "shift and scale" mapping between encoder offsets and sequencer
values, bounds/clamping, and OLED-string formatting. `UIState.h`, `ButtonHandlers.cpp`,
`UIEventHandler.cpp`, `oled.cpp`, and `LEDMatrixFeedback.cpp` all consume these types directly.
This is a data-model dependency, not just a driver call.

**{confirmed}** `src/AlchemyUI/` and `lib/VelocityEncoder/` (submodule) already exist in the repo, fully
written and documented, but `git status` shows both as untracked (`??`), and a repo-wide grep
found **zero** references to either outside their own folders — not in `includes.h`, not in
`Pico2Seq.ino`, nowhere. They are vendored and ready, but completely unwired.

**{confirmed}** `VelocityEncoder`'s `MagEncoder` class is a single driver that wraps *either*
an AS5600 *or* a TMAG5273 behind one identical API (`begin/update/isConnected/getRawAngle/
getParameterIncrement/...`). Its default tuning constants
(`minVelDps=90, maxVelDps=2400, minScale=0.008, maxScale=3.2, curveExponent=1.8,
velocitySmoothing=0.08`) are byte-for-byte identical to the constants hard-coded in the current
`as5600.h` — strong evidence it was built as a faithful port, not a fresh design. The four calls
`AS5600Manager.cpp` actually makes on `as5600Sensor` — `begin()`, `update()`, `isConnected()`,
`getParameterIncrement(min, max, rotations)` — all exist on `MagEncoder` with the same signature.

**{confirmed}** `AlchemyUI`'s `ButtonMap.h` defines exactly **20** logical buttons (`enum class
Btn`: Step1–8, Note/Velocity/Filter/Attack/Decay/Octave, Play/Voice/Scale/Shuffle/Random/Page)
across a hardware ceiling of 5 I2C tiles × 4 buttons each (`AlchemyTiles::kMaxTiles = 5`, one
slider+button tile plus four button-only tiles). This is fewer than the current 32 slots, and
the code has no compiled-in path to more than 20.

**{confirmed}** Neither `matrix/`, `sensors/`, nor `ui/` are compiled into the host test suite
(`tests/CMakeLists.txt` only builds `dsp/`, `scales/`, `sequencer/{ParameterManager,Sequencer}`,
`voice/`). This migration has no CI regression coverage in either direction — nothing will fail
to compile in `ctest`, but nothing will catch a UI logic mistake either.

**{unconfirmed, dated evidence}** Two `.zcode/plans/*.md` notes in this repo (dated Aug 24,
targeting a *different*, related project `MoogFilter_Updated`, treating this repo as
reference-only) record: "Button-4× tile firmware doesn't exist yet (spec: 'defined, not
built')... no master-side driver exists anywhere" as of that date — only the Slider+Button tile
firmware was confirmed working. If still true, only 1 of the 5 physical tiles AlchemyUI's
`ButtonMap.h` assumes (4 faders + 4 buttons) can actually be tested right now, regardless of how
correct the software migration is. **This needs a bench check before Phase 2, not an assumption.**

**{confirmed}** I2C addresses post-migration don't collide: OLED `0x3C`, VL53L1X `0x29`
(untouched, unrelated to this migration), TMAG5273 `0x22` (the B-part default, matching the
Velocity Encoder board), AlchemyUI tiles `0x08–0x0D`. MPR121 (`0x5A`) goes away.
`AlchemyTiles.h`'s own header comment requires `Wire.setClock(400000)` before `begin()` — that
call does not exist anywhere in the current codebase today and needs to be added.

**{unconfirmed, inferred}** The TMAG5273 likely needs the same on-axis diametric magnet mounting
as the AS5600 (its CORDIC angle engine is described as working "on an on-axis rotary setup" where
`getMagnitude()` stays constant through a turn) — worth confirming against the Velocity Encoder
board's own documentation/silkscreen rather than assuming.

## 2. Decided: 32 buttons → 20 buttons via consolidation into existing modes

The current UI has 32 distinct button *functions*. AlchemyUI's hardware ceiling is 20 logical
buttons. **Decision (confirmed with the user): consolidate the five orphaned functions into
existing modes rather than hunting for new physical slots or gestures.** A straight 1:1 port was
never possible; this is the chosen resolution.

Direct matches (unchanged from mpr121 today):

| Current (mpr121, index) | AlchemyUI (`Btn`) |
|---|---|
| Note/Velocity/Filter/Attack/Decay/Octave (16–21) | `Note`/`Velocity`/`Filter`/`Attack`/`Decay`/`Octave` |
| Play/Stop (26) | `Play` |
| Voice switch (24) | `Voice` |
| Scale change (27) | `Scale` |
| Swing/shuffle pattern (29) | `Shuffle` |
| Step pads (0–15, 16 of them) | `Step1`–`Step8` (8 of them), short-press `Page` flips the bank |

The five functions with no direct slot, and where each lands:

| Orphaned function (old index) | New home |
|---|---|
| Slide-mode toggle (22) | New Settings-mode menu item (Settings is already a stopped-state, step-pad-navigated menu — see Phase 2 notes below) |
| Delay on/off (23) | New Settings-mode menu item |
| LED theme change (28) | New Settings-mode menu item |
| Dedicated encoder-target-cycle button (25) | Dropped as a dedicated slot. `autoSelectEncoderParameter` (renamed from `autoSelectAS5600Parameter`, §5) already auto-selects the encoder's target whenever a Note/Velocity/Filter/Attack/Decay button is held — that mechanism becomes the *only* way to change the target, which covers every case except Octave/Slide/DelayTime. Flagging this as the one real behavior loss: today's dedicated cycle button can reach all 7 targets (incl. Octave, DelayTime, SlideTime); auto-select alone can only reach 5. Worth a bench sanity-check once Phase 2 lands — if Octave/DelayTime/SlideTime turn out to matter in practice, `Btn::Page` long-press is the natural fallback slot. |
| Gate-sequence-length mode entry (was: long-press on old button 25) | Proposed: long-press `Btn::Page` (otherwise only used for the short-press step-bank flip, so a long-press is free) |
| Randomize, paged across 2 buttons for 4 voices (30/31) | Single `Btn::Random`: short press randomizes the *currently selected* voice (`uiState.selectedVoiceIndex`), long press resets it — same short/long behavior as today, just retargeted at whichever voice is selected instead of a fixed pair |

The exact step-pad layout of the new Settings-mode menu items (slide/delay/theme) is an
implementation-time detail, not a blocking design question — Settings mode already has an
8-item menu (`settingsMenuIndex`) and a sub-mode split (`PRESET_SELECTION` / `VOICE_PARAMETER`),
so these three just need slots in that existing structure.

## 3. Second design decision: event model (bigger effort lever than it looks)

`Matrix_setEventHandler()` is a **push/callback** model: one press or release produces exactly one
`MatrixButtonEvent{buttonIndex, type}` call into `matrixEventHandler()`. `AlchemyPanel`/`TileButton`
is a **pull/poll** model: each `panel.update(now)` call refreshes internal state, and code asks
`panel.button(Btn::X).pressEdge()/.releaseTap()/.longPress()/.held()`. These are architecturally
different, and `ButtonHandlers.cpp`, `UIEventHandler.cpp` (935 lines), and `ButtonManager.cpp`
are all written entirely in terms of `evt.buttonIndex` integers and `MATRIX_BUTTON_PRESSED/RELEASED`
— including arithmetic on the index (`evt.buttonIndex + 1`, `evt.buttonIndex < NUMBER_OF_STEP_BUTTONS`)
and mode-dependent reinterpretation of the same index (§1). There are two honest ways to do this:

- **A — native rewrite.** Convert the UI layer to poll `AlchemyPanel` directly, using
  `TileButton`'s own press/hold/tap vocabulary instead of the existing timestamp-based long-press
  detection in `ButtonManager`/`UIEventHandler`. Cleaner end state, touches the most files, largest
  chance of introducing a behavior regression in logic nobody has re-tested (no test coverage here,
  see §1) since so much of it is being rewritten at once.
- **B — adapter shim. Decided: adopting this for the first pass.** Each `loop1()` tick, diff every
  `AlchemyPanel` button's `pressEdge()`/`releaseTap()` against its previous state and synthesize the
  *existing* `MatrixButtonEvent{buttonIndex, type}` calls into the *unchanged*
  `matrixEventHandler()`. This confines the new-hardware integration to one small new file,
  `src/ui/AlchemyInputAdapter.cpp`, that maps each `Btn` to a chosen synthetic 0–31 index per the
  §2 table, and leaves ~1500 lines of already-working, mode-aware logic untouched. The cost: it's
  a slightly impedance-mismatched design (AlchemyUI's native long-press support goes unused, since
  the existing timestamp-based detection already does that job) — accepted as worthwhile for a
  lower-risk first cut. Option A stays on the table as a deliberate follow-up cleanup once the
  hardware path is proven on the bench (Phase 0), not part of this pass.

## 4. Phase 0 — bench checks (do before writing Phase 2 code)

1. Build a minimal standalone sketch (just `AlchemyTiles` + `Serial`, no audio/sequencer) that
   scans both Wire banks and reports which of the 5 tile slots actually answer with a valid
   identity block. This resolves the "{unconfirmed, dated evidence}" item in §1 directly instead
   of assuming it — closely matches what the `.zcode` note called `AlchemyPanelTest`.
2. Confirm a TMAG5273 answers at `0x22` on the same Wire bus as the OLED and VL53L1X, and that
   adding `Wire.setClock(400000)` (required by `AlchemyTiles.h`'s own header contract) doesn't
   destabilize the existing `Adafruit_SH110X` OLED or `Melopero_VL53L1X` reads — neither of those
   two currently sets a bus clock either, so this is new territory for the whole bus, not just the
   new devices.

## 5. Phase 1 — sensor swap (AS5600 → TMAG5273), lower risk, do first

This is the safer half: `MagEncoder`'s call surface is a near-exact match for what
`AS5600Manager.cpp` already calls (§1), so the change is mostly renaming an object, not
rewriting logic.

- `includes.h`: drop `#include "src/sensors/as5600.h"`, add
  `#include "lib/VelocityEncoder/src/MagEncoder.h"`.
- `Pico2Seq.ino`: replace the global `AS5600Sensor as5600Sensor;` with
  `MagEncoder magEncoder(MagEncoder::Sensor::TMAG5273);` (default `i2cAddress = 0` already
  resolves to `TMAG5273::ADDRESS_B = 0x22`, matching the Velocity Encoder board). In `setup1()`,
  replace the `as5600Sensor.begin()` block with `magEncoder.begin()`, keeping the existing
  success/failure `Serial.println` pair.
- `AS5600Manager.cpp`: the four call sites (`begin/update/isConnected/getParameterIncrement`)
  swap the receiver object; method names and argument shapes are unchanged.
- `UIState.h` and `src/sequencer/ParameterManager.h/.cpp`: both currently
  `#include "../sensors/as5600.h"` with a comment claiming it's "for `AS5600ParameterMode`" — that
  enum actually already lives in `SequencerDefs.h` (which both files already include separately),
  so this is a stale/redundant include that can simply be dropped once `as5600.h` is deleted.
- Delete `src/sensors/as5600.cpp/h` once the above leaves zero includes (confirm with a repo grep
  before deleting, not just from this document).
- `docs/sensors.md`, `README.md` (hardware list + wiring table), `AGENTS.md`/`CLAUDE.md`: update
  AS5600 → TMAG5273 (address 0x36 → 0x22, note the shared-bus clock requirement from §4.2).

**Naming decision: rename now, as part of this same change.** Every `AS5600`-prefixed identifier
gets renamed to a sensor-neutral equivalent so the code doesn't keep saying "AS5600" once the
sensor underneath is a TMAG5273. `Encoder` is the chosen replacement prefix (matches `MagEncoder`'s
own vocabulary). This is a straightforward, mechanical, but wide-reaching rename — do it as one
dedicated commit, separate from the functional sensor-swap commit, so a regression is easy to
bisect. Full table:

| Old identifier | New identifier | Where |
|---|---|---|
| `AS5600ParameterMode` (enum) | `EncoderParameterMode` | `SequencerDefs.h` |
| `AS5600BaseValues` / `AS5600BaseValuesVoice1` (structs) | `EncoderBaseValues` / `EncoderBaseValuesVoice1` | `SequencerDefs.h` |
| `src/sensors/AS5600Manager.cpp/h` | `src/sensors/EncoderManager.cpp/h` | file rename |
| `as5600Sensor` (global `AS5600Sensor`) | `magEncoder` (global `MagEncoder`, per Phase 1 above) | `Pico2Seq.ino` |
| `as5600BaseValuesVoice1` / `as5600BaseValuesVoice2` | `encoderBaseValuesVoice1` / `encoderBaseValuesVoice2` | `EncoderManager.cpp` |
| `applyIncrementToParameter` | unchanged (already sensor-neutral) | `EncoderManager.cpp/h` |
| `updateAS5600BaseValues` / `updateAS5600StepParameterValues` | `updateEncoderBaseValues` / `updateEncoderStepParameterValues` | `EncoderManager.cpp/h` |
| `applyAS5600BaseValues` / `applyAS5600DelayValues` / `applyAS5600SlideTimeValues` | `applyEncoderBaseValues` / `applyEncoderDelayValues` / `applyEncoderSlideTimeValues` | `EncoderManager.cpp/h` |
| `getAS5600BaseValueRange` / `clampAS5600BaseValue` | `getEncoderBaseValueRange` / `clampEncoderBaseValue` | `EncoderManager.cpp/h` |
| `convertAS5600ParameterToParamId` | `convertEncoderParameterToParamId` | `EncoderManager.cpp/h` |
| `resetAS5600BaseValues` / `initAS5600BaseValues` | `resetEncoderBaseValues` / `initEncoderBaseValues` | `EncoderManager.cpp/h` |
| `getAS5600ParameterValue` | `getEncoderParameterValue` | `EncoderManager.cpp` |
| `BUTTON_AS5600_CONTROL` | `BUTTON_ENCODER_CONTROL` | `UIConstants.h` |
| `uiState.currentAS5600Parameter` | `uiState.currentEncoderParameter` | `UIState.h` |
| `uiState.lastAS5600ButtonPressTime` | `uiState.lastEncoderButtonPressTime` | `UIState.h` |
| `uiState.as5600ControlPressTime` / `as5600ControlWasPressed` | `uiState.encoderControlPressTime` / `encoderControlWasPressed` | `UIState.h` |
| `handleAS5600ControlButton` / `cycleAS5600Parameter` / `autoSelectAS5600Parameter` / `handleAS5600ParameterControl` | `handleEncoderControlButton` / `cycleEncoderParameter` / `autoSelectEncoderParameter` / `handleEncoderParameterControl` | `ButtonHandlers.cpp`, `UIEventHandler.cpp` |

`SensorConstants::MagneticEncoder` (the namespace of range/threshold constants in
`SensorConstants.h`) is left as-is — it's already sensor-neutral and doesn't mention AS5600.

## 6. Phase 2 — button matrix swap (mpr121 → AlchemyUI), higher risk, do second

Gated only on Phase 0.1 now (which tiles actually respond on the bench) — the design decisions
that used to block this (§2, §3) are settled. Sketch of the work:

- Remove `Adafruit_MPR121 touchSensor`, its `#include <Adafruit_MPR121.h>`, `touchSensor.begin(0x5A)`
  + threshold config, `Matrix_init`/`Matrix_scan`/`Matrix_setEventHandler` calls from `Pico2Seq.ino`.
- Add `AlchemyPanel panel;`, `panel.begin(Wire, /*bankB=*/nullptr or &Wire1, millis())` in `setup1()`
  (bank B only needed if the rig actually uses two Qwiic buses — confirm during Phase 0.1),
  `panel.update(millis())` replacing `Matrix_scan()` in `loop1()`. Also add the
  `Wire.setClock(400000)` call from §4.2, ahead of `panel.begin()`.
- Write `src/ui/AlchemyInputAdapter.cpp` (§3, Option B): each `loop1()` tick, for every `Btn` in
  the §2 mapping table, compare `panel.button(Btn::X).pressEdge()`/`.releaseTap()` against last
  tick's state and call the existing `matrixEventHandler()` with a synthesized
  `MatrixButtonEvent{syntheticIndex, PRESSED/RELEASED}` — `ButtonHandlers.cpp`,
  `UIEventHandler.cpp`, and `ButtonManager.cpp` stay untouched apart from the renames in §5 and
  the retargeted `Random`/gate-seq-length/Settings-menu logic from §2.
- Wire the three new Settings-mode menu items (slide/delay/theme, per §2) and the `Btn::Page`
  long-press → gate-seq-length-mode entry into the existing Settings/`ButtonHandlers` logic.
- `LEDMatrixFeedback.cpp`/`UIState.h`: fields like `flash23Until`/`flash25Until`/`flash31Until` are
  named after old button indices but drive on-screen indicators on the *separate* 8×8 WS2812 LED
  grid (confirmed via README: touch matrix and LED matrix are listed as two distinct pieces of
  hardware, not 1:1 co-located pads) — so this is lower risk than it first looks, but the *trigger
  condition* for each flash (which button press causes it) still needs to be re-pointed at the new
  input source, whichever of §3's options is chosen.
- Delete `src/matrix/` (`Matrix.cpp/h`, its `README.md`) once a repo grep confirms zero references.
- Remove `Adafruit_MPR121` (and `OneButton` if it turns out unused elsewhere) from `README.md`'s
  library list and wiring table; add the AlchemyUI tile address table instead.
- Update `docs/matrix.md` (replace or retire), `docs/ButtonHandlers.md`, `docs/architecture.md`'s
  data-flow section (currently says "Matrix/AS5600/VL53L1X/MIDI input (Core 1)").

## 7. Phase 3 — verification

- `cmake -B build_test && cmake --build build_test --parallel && ./build_test/tests/pico2seq_tests`
  — should stay green throughout, since `matrix/`, `sensors/`, `ui/` aren't compiled into the
  suite (§1); this only catches an accidental stray include breaking `dsp/sequencer/voice`
  compilation, not a UI logic mistake.
- Repo-wide grep for `mpr121`/`MPR121`/`AS5600Sensor`/`as5600\.h` returning nothing outside git
  history.
- No CLI firmware build exists (per `CLAUDE.md`) — actual functional verification is Arduino IDE
  compile + flash + hands-on test on the real hardware. I can't do that step; the bench checks in
  §4 are the closest substitute available before that point.

## 8. Decisions log

| Decision | Outcome |
|---|---|
| Button consolidation (§2) | Consolidate into existing modes: slide/delay/theme → new Settings-mode menu items; encoder-cycle → drops to auto-select-only (one known capability gap: Octave/DelayTime/SlideTime targets become unreachable without a dedicated button — flagged in §2 for a bench sanity-check); randomize → single button, retargeted at the currently-selected voice |
| Event-model approach (§3) | Adapter shim (Option B) for this pass — `AlchemyInputAdapter.cpp` synthesizes the existing `MatrixButtonEvent` calls; native rewrite (Option A) deferred to a later cleanup |
| Naming scope (§5) | Rename now, in the same change — full `AS5600` → `Encoder` identifier table in §5 |

## 9. Still open — resolve on the bench, not on paper

**Phase 0 bench results.** How many of the 5 AlchemyUI tiles are physically present and running
confirmed-working firmware today. This is the one item in this whole plan I can't resolve by
reading source — it needs the standalone `AlchemyPanelTest`-style sketch from §4.1 run against the
real hardware. If it turns out only the slider+button tile answers right now, Phase 2's `Step1-8`/
`Note..Octave` pad-side buttons won't be testable yet even though the software side is done —
worth running this check *before* writing Phase 2 code, not after, so the mapping in §2 can be
scoped down deliberately if needed rather than discovered broken during bring-up.
