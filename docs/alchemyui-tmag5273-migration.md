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

**{confirmed}** `src/AlchemyUI/` and `src/VelocityEncoder/` already exist in the repo, fully
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

## 2. The one design decision everything else hangs on: 32 buttons → 20 buttons

The current UI has 32 distinct button *functions*. AlchemyUI's hardware ceiling is 20 logical
buttons. This is not a driver-swap problem — it's a UI redesign that has to happen before or
during Phase 2, and the answer changes how much of `ButtonHandlers.cpp` / `UIEventHandler.cpp`
gets rewritten. A straight 1:1 port is not possible.

Direct matches (no decision needed):

| Current (mpr121, index) | AlchemyUI (`Btn`) |
|---|---|
| Note/Velocity/Filter/Attack/Decay/Octave (16–21) | `Note`/`Velocity`/`Filter`/`Attack`/`Decay`/`Octave` |
| Play/Stop (26) | `Play` |
| Voice switch (24) | `Voice` |
| Scale change (27) | `Scale` |
| Swing/shuffle pattern (29) | `Shuffle` |
| Step pads (0–15, 16 of them) | `Step1`–`Step8` (8 of them) + `Page` to flip banks |

No slot exists for these five, and each needs a decision: slide-mode toggle (22), delay on/off
(23), the dedicated encoder-target-cycle button (25 — note `autoSelectAS5600Parameter` already
auto-selects the target whenever a param button is held, so the *dedicated* cycle button may be
closer to a convenience than a hard requirement), LED theme change (28), and the second randomize
button (30/31 currently cover 4 voices two-at-a-time via a "page" flag; with only one `Random`
slot, "randomize the currently-selected voice" is both simpler and arguably better than the
current scheme). §5 below lays out a proposed consolidation; it is a proposal, not a decision —
see §6.

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
- **B — adapter shim (recommended for the first pass).** Each `loop1()` tick, diff every
  `AlchemyPanel` button's `pressEdge()`/`releaseTap()` against its previous state and synthesize the
  *existing* `MatrixButtonEvent{buttonIndex, type}` calls into the *unchanged*
  `matrixEventHandler()`. This confines the new-hardware integration to one small new file
  (e.g. `src/ui/AlchemyInputAdapter.cpp`) that maps each `Btn` to a chosen synthetic 0–31 index,
  and leaves ~1500 lines of already-working, mode-aware logic untouched. The cost: it's a slightly
  impedance-mismatched design (AlchemyUI's native long-press support goes unused, since the
  existing timestamp-based detection already does that job), and the 32→20 mapping decision (§2)
  still has to be made — the adapter just makes it "which `Btn` produces which synthetic index"
  instead of "rewrite three files."

Recommendation: **B for the initial migration**, A as a deliberate follow-up cleanup once the
hardware path is proven on the bench. This is a recommendation, not something I've assumed — it's
one of the questions below.

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
  `#include "src/VelocityEncoder/src/MagEncoder.h"`.
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

**Naming decision (see §6):** every identifier above keeps the literal string "AS5600"
(`AS5600ParameterMode`, `AS5600BaseValues`, `AS5600Manager`, `applyAS5600BaseValues`,
`as5600ControlPressTime`, ...) even after the sensor underneath is a TMAG5273. That's not a bug —
it's a scope choice: renaming ~15 files' worth of identifiers to something sensor-neutral is a
larger, purely-cosmetic diff on top of the functional swap above, and it's your call whether to
do it now or later.

## 6. Phase 2 — button matrix swap (mpr121 → AlchemyUI), higher risk, do second

Gated on Phase 0.1 (which tiles actually respond) and on the two decisions in §2/§3 being made.
Sketch of the work once those are decided:

- Remove `Adafruit_MPR121 touchSensor`, its `#include <Adafruit_MPR121.h>`, `touchSensor.begin(0x5A)`
  + threshold config, `Matrix_init`/`Matrix_scan`/`Matrix_setEventHandler` calls from `Pico2Seq.ino`.
- Add `AlchemyPanel panel;`, `panel.begin(Wire, /*bankB=*/nullptr or &Wire1, millis())` in `setup1()`
  (bank B only needed if the rig actually uses two Qwiic buses — confirm during Phase 0.1),
  `panel.update(millis())` replacing `Matrix_scan()` in `loop1()`.
- Implement whichever of §3's Option A or B was chosen. If B: write the small adapter that
  produces `MatrixButtonEvent`s from `TileButton` edges using the §2 mapping table.
- If A: rewrite `ButtonHandlers.cpp`/`UIEventHandler.cpp`/`ButtonManager.cpp` against `Btn` and
  `TileButton` directly, including re-deriving the mode-reuse behavior enumerated in §1 (settings
  mode, voice-parameter mode, slide mode, gate-seq-length mode all currently reinterpret the same
  raw indices contextually — that behavior needs to be preserved deliberately, not lost by omission).
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

## 8. Open decisions before implementation starts

1. **Button consolidation (§2).** Confirm or redirect the proposed mapping — specifically what
   happens to slide-toggle, delay-toggle, the dedicated encoder-cycle button, theme-change, and
   the voice-paged randomize scheme.
2. **Event-model approach (§3).** Adapter shim (B, recommended for pass one) vs. native rewrite (A).
3. **Naming scope (§5).** Keep `AS5600`-prefixed identifiers as-is (minimal diff) or rename to
   sensor-neutral names as part of this same change.
4. **Phase 0 bench results.** How many of the 5 AlchemyUI tiles are physically present and running
   confirmed-working firmware today — this may itself force a scoped-down first cut of §2's mapping
   if, e.g., only the slider+button tile answers right now.
