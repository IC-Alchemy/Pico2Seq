# Modifier-layer restoration — bringing the pre-tile control vocabulary back

Status: **plan / not yet implemented**
Supersedes nothing; extends `2026-09-01-alchemy-tile-control-surface-design.md`.

## Problem

The Alchemy tile migration moved all 32 MPR121 pads to step/gate duty and put
the parameter and utility function sets on one 8-button tile, selected by the
GP7 toggle. Every old function still exists in code, but reachability regressed:

1. **Theme (LED palette), swing template, scale and encoder-target cycling are
   only reachable when the GP7 toggle is in the Utility position.** In Param
   mode — the mode you perform in — they cannot be touched.
2. **The deep tier requires stopping the transport.** Envelope, overdrive,
   filter mode, filter resonance, delay-time = dotted quarter, tempo −5, tempo
   +5 (old matrix pads 8–15) live only inside Settings mode, which is entered
   with a long-press of Play *while stopped*, then a sub-mode toggle on the
   encoder button (`UIEventHandler.cpp:529-657`).
3. **Per-voice randomize is gone.** Old pads 30/31 randomized a named
   sequencer; the tile Randomize only ever hits `selectedVoiceIndex`.
4. **No cycler runs backwards.** 10 themes, 13 scales, 16 swing templates,
   forward-only — a mis-press costs a full lap.

## Assumptions

| # | Assumption | Status | Evidence |
|---|---|---|---|
| A1 | GP7 grounded = Param, open (pull-up) = Utility | **confirmed** | `kModeParamLevel = false` (`ControlSurfaceLogic.h:37`), `ModeStabilizer::update` (`ControlSurfaceLogic.cpp:29`), `pinMode(PIN_ALCHEMY_MODE_SWITCH, INPUT_PULLUP)` (`Pico2Seq.ino:841`); user confirmed the toggle switches GP7 to ground |
| A2 | Shift is unread in Utility mode — 7 chords are free, no behavior change to reclaim them | **confirmed** | `AlchemyControlBridge::handleUtilityButtons` (`AlchemyControlBridge.cpp:222-311`) never consults `uiState.shiftHeld` |
| A3 | Voice buttons act on the press edge, so using them as held modifiers requires moving select to release | **confirmed** | `handleVoiceButtons` acts on `edges.pressEdge` (`AlchemyControlBridge.cpp:140-150`) |
| A4 | Tile buttons can be chorded reliably | **confirmed** | tiles poll round-robin every 4 ms (`AlchemyTiles.h:42`), bridge runs on the 1 ms control slice; a deliberate hold is orders of magnitude longer |
| A5 | Two **pads** must never form a chord: the MPR121 is a 4×8 row/column-coincidence matrix, so two pads in different rows *and* columns raise two phantom presses | **confirmed** | `scanMatrixButton()` ANDs the row and column electrode bits (`Matrix.cpp:43-47`) |
| A6 | The deep-tier voice actions can be re-hosted without rewriting them | **confirmed** | `handleVoiceParameterButton(voiceIndex, paramIndex, state)` already takes an explicit voice index (`ButtonHandlers.cpp:97`) |
| A7 | ButtonModule8 reports all 8 bits truly simultaneously (3+ keys down) | **unconfirmed** | inferred from the 8-bit bitmap in `AlchemyProto.h:130`; needs one bench check before Milestone 2 |
| A8 | LED themes = 10, scales = 13, swing templates = `NUM_SHUFFLE_TEMPLATES` | **confirmed** | `LEDMatrixFeedback.h:31-45`, `currentScale = (currentScale + 1) % 13` (`ButtonHandlers.cpp:223`), `ShuffleTemplates.h` |

## Design: two modifiers, four layers

Two modifier keys, each with one meaning, applied to the same seven tile
buttons. No new hardware, no pad chords (A5).

- **Shift** (tile bit 7) = *"the other direction / the deeper twin of this
  same button."*
- **Fn** = hold any slider Voice button (V1–V4) = *"give me the utility set,
  scoped to this voice."*

| Modifier | GP7 = Param | GP7 = Utility |
|---|---|---|
| — | Param set (Note…Octave, Slide) — unchanged | Utility set (Play…Randomize) — unchanged |
| Shift | latch parameter — unchanged | **new:** reverse / alternate twin |
| Fn (hold V*n*) | **new:** the Utility set, voice-scoped | **new:** per-voice synth set for voice *n* |
| Shift + Fn | reserved (no action) | reserved (no action) |

### Layer A — Shift + Utility set (GP7 = Utility)

Reclaims the seven chords proven free by A2.

| Bit | Plain | **Shift** |
|---|---|---|
| 0 | Play / Stop | **Tempo −5** (floor 45) |
| 1 | Delay on/off | **Delay time = dotted quarter** |
| 2 | Scale cycle → | **Scale cycle ←** |
| 3 | Swing cycle → | **Swing cycle ←** |
| 4 | Theme cycle → | **Theme cycle ←** |
| 5 | Encoder target → | **Encoder target ←** |
| 6 | Randomize voice | **Tempo +5** (ceiling 200) |

Tempo −5 on the leftmost button and +5 on the rightmost is the physical
mnemonic; both tempo bounds match the existing Settings-mode code
(`UIEventHandler.cpp:625-645`).

### Layer B — Fn + tile set (GP7 = Param)

Holding V*n* temporarily presents the **Utility** set while the toggle stays in
Param. This is the fix for problem 1: LED palettes, swing and scale become
reachable mid-performance without touching the toggle.

Voice scoping applies where it is meaningful: `Fn(V2) + Randomize` randomizes
**voice 2**, restoring the old per-voice randomize pads (problem 3) and
extending them from 2 voices to 4. Play, Delay, Scale, Swing, Theme and Encoder
target stay global.

### Layer C — Fn + tile set (GP7 = Utility): per-voice synth tier

This is the fix for problem 2 — the whole deep tier, reachable while running.

| Bit | Action on voice *n* | Existing implementation |
|---|---|---|
| 0 | Envelope on/off | `handleVoiceParameterButton(n, 8, …)` |
| 1 | Overdrive on/off | `handleVoiceParameterButton(n, 9, …)` |
| 2 | Filter mode cycle | `handleVoiceParameterButton(n, 11, …)` |
| 3 | Filter resonance step | `handleVoiceParameterButton(n, 12, …)` |
| 4 | Preset next | `applyVoicePreset(n, …)` |
| 5 | Preset previous | `applyVoicePreset(n, …)` |
| 6 | Randomize voice *n* (long-press = reset) | `beginRandomizePress` / `handleRandomizeButton` |

Settings mode stays exactly as it is — this adds a live path to the same
actions, it does not remove the stopped one.

### Voice-button tap vs. hold

`Fn` and voice select share the same four buttons, so:

- Press V*n* with **Shift held** → today's Shift chord, on the press edge,
  unchanged (`AlchemyControlBridge.cpp:152-176`).
- Press V*n* alone → arm Fn. If any tile button fires while it is held, mark
  the press **consumed**.
- Release V*n* **unconsumed** → select voice *n*.
- Release V*n* **consumed** → nothing; the chords already did the work.

Cost: voice select lands on release rather than press. At a 4 ms poll that is
imperceptible, and it is the standard Fn-key idiom.

## Software design

Everything decidable stays in pure, host-testable C++; the bridge only
translates.

### 1. `src/ui/ControlSurfaceLogic.h/.cpp` — new pure policies

```cpp
enum class UiAction : uint8_t {
  None,
  // transport / global
  PlayStop, DelayToggle, DelayDottedQuarter, TempoDown5, TempoUp5,
  ScaleNext, ScalePrev, SwingNext, SwingPrev, ThemeNext, ThemePrev,
  EncoderTargetNext, EncoderTargetPrev, EncoderTargetHold,
  // param set
  ParamHold, SlideToggle,
  // voice-scoped
  RandomizeVoice, VoiceEnvelopeToggle, VoiceOverdriveToggle,
  VoiceFilterModeCycle, VoiceResonanceStep, VoicePresetNext, VoicePresetPrev,
};

struct ActionBinding {
  UiAction action = UiAction::None;
  uint8_t  paramId = 0xFF;   // valid for ParamHold
  bool     voiceScoped = false;
};

/** The whole control vocabulary as one table: (mode, shift, fn, bit) -> action. */
class ActionMap {
 public:
  static ActionBinding resolve(Mode mode, bool shiftHeld, bool fnHeld, uint8_t bit);
};

/** Voice-button tap/hold arbitration (Fn). Pure; the bridge feeds edges. */
class FnModifier {
 public:
  void onVoicePress(uint8_t voice, bool shiftHeld);
  void onVoiceRelease(uint8_t voice);      // -> selectPending() if unconsumed
  void markConsumed();                      // any tile action fired while held
  bool  held() const;
  uint8_t heldVoice() const;                // 0xFF when none
  int8_t selectPending();                   // voice to select, or -1; clears
};
```

`ActionMap::resolve` is a flat table — a rewired panel or a reshuffled layer is
a table edit, matching the intent already stated in `ButtonMap.h`.

### 2. `src/ui/AlchemyControlBridge.cpp/.h`

- `handleParamButtons()` + `handleUtilityButtons()` collapse into one
  `handleTileButtons()` that reads `ActionMap::resolve(mode, shift, fn, bit)`
  and dispatches. Press/release/long-press edge handling for Play, Encoder-hold
  and Randomize is preserved verbatim, keyed off the resolved action rather
  than off the raw bit.
- `handleVoiceButtons()` drives `FnModifier` instead of calling `selectVoice`
  on the press edge.
- `onModeFlip()` additionally clears `FnModifier`, so a toggle flip mid-chord
  cannot strand a modifier.

### 3. `src/ui/ButtonHandlers.h/.cpp`

Add one dispatcher plus the genuinely new actions:

```cpp
void dispatchUiAction(const ControlSurface::ActionBinding &binding,
                      uint8_t voiceScope, UIState &state);
```

- Reverse cyclers: `scale`, `theme`, `swing`, `encoder target` gain a `−1`
  direction (modular decrement of the existing `+1` code in
  `handleControlButton`).
- `TempoDown5` / `TempoUp5` / `DelayDottedQuarter`: lift the bodies out of
  `handleVoiceParameter` cases 13/14/15 (`UIEventHandler.cpp:614-645`) into
  shared helpers so Settings mode and the new chords call the same code.
- Voice-scoped synth actions forward to `handleVoiceParameterButton(voice, …)`,
  which already accepts a voice index (A6).
- Preset next/prev wrap `applyVoicePreset` and keep
  `uiState.voicePresetIndices[]` in step.

### 4. `src/ui/UIState.h`

Per the repo rule that UIState is the single source of UI truth, add:

```cpp
uint8_t fnVoiceHeld = 0xFF;                  // active Fn modifier, 0xFF = none
bool    fnConsumed  = false;
volatile unsigned long actionBannerUntil = 0; // OLED confirmation window
const char *lastActionName = nullptr;         // static string, no allocation
```

No new free-floating globals.

### 5. Feedback (`src/OLED/`, `src/LEDMatrix/`)

A cycler you cannot see is a cycler you cannot use. On every dispatched action,
set `lastActionName` + `actionBannerUntil` and render a short banner (scale
name, theme name, swing template name, tempo value). While Fn is held, the
OLED shows the live layer legend for the held voice. This is the difference
between "the function exists" and "the function is usable on stage".

### 6. Tests — `tests/unit/test_control_surface_logic.cpp`

- `ActionMap` across all four layers × 7 bits × 2 modes: exhaustive expected
  table, and an assertion that **no two (mode, shift, fn, bit) tuples collide**.
- A coverage test that enumerates the pre-migration function list and asserts
  each one resolves from some tuple — the regression guard for "all the prior
  functionality back".
- `FnModifier`: tap selects; hold + tile action suppresses select; Shift+voice
  routes to the chord path and never arms Fn; mode flip clears state.
- Reverse cyclers wrap correctly at 0 (scale 0 → 12, theme 0 → 9).

No hardware stubs needed — all of it is pure C++ already linked into
`pico2seq_tests`.

## Milestones

| # | Deliverable | Verification |
|---|---|---|
| 1 | `UiAction` + `ActionMap` + `FnModifier` in ControlSurfaceLogic, with tests | `./build_test/tests/pico2seq_tests "[control_surface]"` green |
| 2 | Bridge rewired to `ActionMap`; Layer A live (Shift+utility) | bench: reverse scale/theme/swing, tempo ±5, dotted-¼ delay |
| 3 | `FnModifier` wired; Layers B and C live | bench: theme cycle in Param mode; `Fn(V3)` + bit 2 changes voice 3 filter mode while running |
| 4 | OLED/LED feedback + Fn legend | bench: every chord names itself on the display |
| 5 | Docs: `docs/ButtonHandlers.md` layer tables, this spec marked implemented | `scripts/verify_docs*` link check |

Milestone 2 is gated on the A7 bench check (3 simultaneous tile keys).

## Explicitly out of scope

- Any two-pad chord on the MPR121 (A5 — it ghosts).
- Removing Settings mode; it stays as the stopped-state path to the same tier.
- Restoring `BUTTON_VOICE_SWITCH`'s cycle-through-voices behavior: four direct
  selects supersede it.
