# Pico2Seq Functionality Improvements — Top 8 + Implementation Plan

Date: 2026-09-12 · Anchored to `simp1` @ 51ced80 · Source: five parallel read-only subsystem
surveys (sequencer core, voice/DSP, UI/control surface, sensors, persistence/infra), with
spot-checks of the load-bearing claims against the live tree.

Selection criteria: user value on this instrument, feasibility under the dual-core
architecture, risk, and leverage (prerequisites unlocked for later items). Everything here
is in-repo work except where noted; `src/rpdsp/` submodule changes are explicitly avoided.

## Global constraints that apply to every item

- Core 1 (`AudioEngine` / `Voice::process`) is real-time: no blocking, allocation, or
  `Serial.print`. Parameter changes cross cores only via per-voice
  `SpscQueue<ControlUpdate, 8>`.
- `src/pico2seq-core/` stays portable (no Arduino/UI includes). Sequencer→UI bridging via
  `src/ui/UIEventHandler.h/.cpp`.
- uClock ISR callbacks stay stage-only; all work drains in `Application::update()`.
- Testable logic goes in `src/ui/ControlSurfaceLogic.cpp` or `src/pico2seq-core/`;
  verify with the host suite (CMake + Ninja/clang; stale MSVC caches produce fake
  compile failures — configure fresh).
- `UIState` is the single source of UI truth — extend the struct, no new globals.
- `noexcept` must match exactly between declaration and definition (host GCC rejects
  mismatches).

---

## The 8 improvements

### 1. Pattern & patch persistence (save/recall across power cycles)

**Why:** The single biggest gap — twice over. Nothing survives power-off (no
EEPROM/LittleFS/`flash_range_program` anywhere in `src/`; presets are compile-time
`constexpr` tables in `src/voice/presets/`), and a watchdog recovery *also* loses the
session because recovery mode demands a power-cycle. Users cannot keep anything they
program.

**Scope:** Serialize per voice: 9 `ParameterTrack`s (steps + lengths), `VoiceConfig`
edits (as preset index + parameter values — `VoiceConfig.parameters`/`.recipe` are raw
pointers and must never be serialized as pointers, `src/voice/VoiceConfig.h:97-98`),
patch bases, plus globals (`currentScale`, shuffle/theme indices, tempo, swing, master
volume). Max raw snapshot ≈ 10.5 KB (4 × 9 × 64 floats); firmware uses only ~249 KB of
4 MB flash. Earlephilhower core 6.0.0 ships flash-backed `EEPROM.h`/LittleFS.

**Implementation sketch:**
1. Add `FIRMWARE_VERSION` + save-format magic/version/checksum (precedent:
   `src/AlchemyUI/src/AlchemyProto.h:33` frame checksum).
2. Add `serialize()`/`deserialize()` to `ParameterManager` and a flat `PatchSnapshot`
   struct in `src/app/` (Core 0).
3. Write path: stop transport (reuse `stopClockForEditor()`), drain
   `voiceManager->flushControlUpdates()`, snapshot, write flash. Flash erase/program
   stalls XIP — Core 0 only, Core 1 parks or runs from RAM; explicit user saves make
   wear a non-issue.
4. Restore hook in `src/app/VoiceSetup.cpp:14-25` (already builds voices from
   `uiState.voicePresetIndices`) and `Application::begin()`.
5. Phase 2 (optional): auto-written "last session" slot on transport-stop with dirty-flag
   debounce, offered for restore after a watchdog reset — turns recovery mode from
   session-destroying into session-saving.

**Tests:** Golden round-trip tests in `tests/unit/` (mutate → serialize → deserialize →
compare), checksum-corruption rejection. Flash I/O itself is hardware-bound; keep the
format logic portable.

**Risk:** Highest of the list (cross-core quiescence, XIP stalls, format migration), which
is why it's sequenced last — items 5 and the versioning work build its groundwork.

### 2. Per-voice mute/solo performance layer

**Why:** `VoiceManager::enableVoice/disableVoice/isVoiceEnabled` and `setVoiceMix` exist
with **zero call sites** (verified). There is no performance mode: the only live "mute"
is the master-volume fader. A 4-way mute/solo grid is the cheapest big win for live use.

**Implementation sketch:**
1. Extend `UIState` with `voiceMuteMask` / `voiceSoloMask` (`src/ui/UIState.h:96-101`,
   next to the Alchemy fields).
2. Pure policy `effectiveMuteMask(mute, solo)` in `src/ui/ControlSurfaceLogic.cpp` —
   host-testable.
3. Wire chords in `src/ui/AlchemyControlBridge.cpp:256-357`; Utility button bit 1 is
   unassigned since the delay removal (`AlchemyControlBridge.cpp:267` falls to
   `default:`), and the not-yet-implemented modifier-layer spec
   (`docs/superpowers/specs/2026-09-02-modifier-layer-restoration.md`) already designs
   the vocabulary.
4. Gate at the `StepPlayback` call site (`src/app/StepPlayback.cpp:235-250`) — this also
   suppresses gates via `midiNoteManager.setGateState`, avoiding stuck notes. Route
   through `enableVoice()` (control-thread status, voice queues audio enable) — no new
   cross-core primitives.
5. LED dim of the muted band (`src/LEDMatrix/LEDMatrixFeedback.cpp:935-990`) + OLED
   indicator (`src/OLED/oled.cpp:435-479`).

**Risk:** Add mute/solo to every "clear conflicting modes" list
(`UIEventHandler.cpp:648-655, 682-689`; `VoiceEditor.cpp:9-29`) or editor entry leaks
mutes. Muted voices must still receive note-offs (mirror `ClockService.cpp:68-75`).

### 3. Engine-aware randomizer (per-engine sweet-spot subranges) + tests

**Why:** The pending known task, confirmed with numbers. `randomizeParameters`
(`src/pico2seq-core/sequencer/ParameterManager.cpp:176-232`) draws Filter/Attack/Decay in
fixed "standard voice" subranges, but on non-standard layouts the raw value feeds
`VoiceParameterBinding::map()` unnormalized (`src/voice/VoiceParameters.cpp:98-108`):
waveguide T60 reaches only **0.05–0.95 s of the 0.05–10 s** range (preset default
unreachable), FM Ratio **0.65–2.75 of 0.5–8** (FmGlass's seed 3.5 above the max),
Hypersaw detune capped below preset default. HEAD 51ced80 just retuned this code and it
has **zero tests** (verified) — pin it while fixing it.

**Implementation sketch:**
1. Add optional per-slot normalized sweet-spot fields to `VoiceParameterBinding`
   (`src/voice/VoiceParameters.h:14-27`); fill per layout (STANDARD keeps current
   subranges as the sweet spots).
2. Pass the voice's layout into `randomizeParameters` (call site
   `src/ui/ButtonHandlers.cpp:67` has Core-0 access to the config); draw normalized
   sweet-spot values so `map()` lands where the engine is musical.
3. Make the LCG seed injectable (`seed_lcg()` currently uses time,
   `ParameterManager.cpp:19-46`) so bounds are testable.
4. Add `tests/unit/test_parameter_randomize.cpp`: per-engine bounds, gate density,
   the Slide 1/16 rule.

**Risk:** Low — Core 0 only, preset IDs untouched. Lanes are per-voice sequencers, so
re-domaining one voice can't corrupt others.

### 4. Per-step probability & ratchets/retriggers

**Why:** The two most standard modern sequencer features are absent (no probability
field anywhere; `shouldRetrigger` fires exactly once per gated step,
`src/pico2seq-core/sequencer/SequencerDefs.h:229`, set at `Sequencer.cpp:317-341`).
Sub-step timing already exists: 480-PPQN ticks stream into `processPendingGateTicks`
(`src/app/ClockService.cpp:101-122`) and mid-gate retriggering already works for notes
(`src/app/StepPlayback.cpp:113-120`).

**Implementation sketch:**
1. Extend `ParameterTrack` (or add a parallel flag lane) in
   `src/rpdsp/src/rpdsp/parameter_track.h` — **note: this is the submodule**; prefer an
   in-repo parallel lane in `src/pico2seq-core/` if submodule churn is unacceptable.
2. Consume probability in `Sequencer::processStep` via the injectable RNG from item 3.
3. Ratchets: per-voice retrig scheduler on the Core-0 PPQN tick path — reuse the
   GateTimer machinery; keep all mutation in staged callbacks (ISR stays stage-only).
4. UI: probability/retrigger as encoder-editable step properties in step-edit mode
   (long-press pad, `UIEventHandler.cpp:358-401`), with LED brightness as probability
   preview.

**Risk:** Moderate — new state on the hot step path; polymetric `%` arithmetic must
respect the new lanes; LED/OLED previews for per-step props need care.

### 5. Pattern copy/paste + single-slot undo

**Why:** Only destructive ops today: single-step clear (`UIEventHandler.cpp:736-760`)
and per-voice reset (`:630-634`). No copy/paste, no whole-pattern clear, no undo —
every edit is immediately unrecoverable (randomize, reset, clears, length changes).
Also the serialization groundwork for item 1.

**Implementation sketch:**
1. `Sequencer::setStep()` to mirror `getStep()` (`Sequencer.cpp:441-456`).
2. Per-voice clipboard in `UIState`; copy/paste chords on the existing Shift+pad path
   (`UIEventHandler.cpp:300-309`). Whole-pattern clear + "copy all 4 voices" variants.
3. `ParameterManager::snapshot()`/`restore()` (pure `std::array` data, fully portable);
   ~2.3 KB/voice, ~9.2 KB all four — trivial RAM. Capture from UI entry points before
   each discrete operation; single-level undo (Shift+Randomize or a dedicated chord).
4. Natural extension: A/B pattern compare, standard on hardware sequencers.

**Tests:** Pure core functions in `tests/unit/test_sequencer.cpp`; policy tests for
clipboard semantics in `test_control_surface_logic.cpp`.

**Risk:** Minimal — Core 0 only, no ISR interaction.

### 6. Master bus glue: soft-clip + compressor + reverb send

**Why:** The master bus is a raw sum with a **hard clamp** (`src/app/Pcm16.h:16`,
`src/voice/VoiceManager.cpp:348-377`) — four voices at typical levels can exceed ±1.0
and hard-clip. The delay removed in 8961d07 left the master insert slot empty and
~338 KB RAM + two encoders freed. A glue stage restores headroom polish and the sense
of space without restoring the delay as-is.

**Implementation sketch (all from vendored rpdsp — no submodule changes):**
1. Master soft-clip (`softClip`/`fastTanh`, `src/rpdsp/src/rpdsp/algorithm.h`) +
   enable the already-constructed-but-bypassed `rpdsp::Compressor`
   (`VoiceManager.cpp:25-29, 322-326, 365-376`) at modest glue settings — the code's own
   comment prices one bus instance at ~two transcendentals/sample, affordable.
2. Phase 2: `StereoSchroederReverb` send (`src/rpdsp/src/rpdsp/effects.h`) with a
   wet-mix knob on a freed encoder/Delay-encoder slot.
3. Replace the hard `0.0f` transport-mute cut (`VoiceManager.cpp:365`) with a ~5 ms fade
   (pairs with item 7's dezipper work).
4. Measure against `renderOverBudget` (`src/app/AudioEngine.cpp:181`) on the bench
   before/after each stage.

**Tests:** `processAllVoices` is host-tested (`[voice_transfer]`) — dynamics and
soft-clip behavior assertable there. `AudioEngine`/`fill_audio_buffer` is
hardware-bound; bench-verify underrun counters.

**Risk:** Per-sample CPU on Core 1 — incremental, measured. Stereo (dormant
`outputChannel`/`setVoiceOutput`) only after this, since a stereo reverb wants L/R
separated.

### 7. Voice parity & click-free polish

**Why:** Three small audio-truth defects that make half the instrument second-class:
(a) **GateLength is ignored on voices 2–3** — `updateGate` runs only for
`voiceIndex < kGateVoiceCount` (`src/app/StepPlayback.cpp:17, 159-168`; gates/timers
sized 2 in `src/voice/VoiceSystem.h:28-31`), so staccato patterns sound different on
half the voices; (b) **waveguide pluck is always full level** —
`waveguide_.pluck(targetHz, 1.0f)` (`src/voice/Voice.cpp:640`) ignores velocity, which
is both musically weak and a click source on envelope-less string presets (instant
`velocityLevel` gain jump when re-plucking a ringing string; `PluckedStringVoice`
already accepts amplitude, `src/rpdsp/src/rpdsp/waveguide.h:190`); (c) **zipper noise**
— `globalVolume` and `transportMuted_` apply per-sample with no smoothing
(`VoiceManager.cpp:365-366`; fader at `AlchemyControlBridge.cpp:394`).

**Implementation sketch:**
1. Widen `gates[2]`/`gateTimers[2]` to 4, run `tickAllGateTimers()` for 4, relax the
   `kGateVoiceCount` branch — MIDI bookkeeping stays 0–1 only.
2. Feed velocity into pluck amplitude/pick hardness inside the layout-flag discipline;
   per-sample one-pole on applied `velocityLevel` (audio-thread-owned, ~3 lines).
3. Audio-thread-local one-pole on `globalVolume` + ~5 ms mute fade (rpdsp
   `onePoleSmooth` or cached-alpha like the existing `slideAlpha` pattern).

**Tests:** `[voice]` gate tests widen; `[waveguide]` determinism tests must keep
passing. `noexcept` signatures matched (host GCC invariant).

**Risk:** Low-moderate — touches Core-0 timing (gate timers stay tick-driven) and
per-sample math on Core 1 (multiply-add, negligible).

### 8. Voice Edit power features: A/B compare, voice-to-voice copy, patch restore

**Why:** The editor (PR #23) can nudge one parameter at a time and reset only the
cursor param (`src/app/VoiceEditor.cpp:81-93`). No way to hear the un-edited patch,
clone a good edit to another voice, or recover a whole patch after a bad detour. All
plain `VoiceConfig` struct copies; policy lives in host-tested `VoiceEdit::Controls`
(`src/ui/VoiceEditControls.h:15-87`, tested in `tests/unit/test_voice_edit.cpp`).

**Implementation sketch:**
1. Snapshot field(s) in `UIState`-borne editor state: A/B compare (toggle between
   working and snapshot config), voice-to-voice copy (slider-button chord: source +
   destination), full-patch restore-to-preset.
2. Snapshot the **requested** config via `voiceManager->getVoiceConfig` (same source
   `buttons()` uses) — never the audio-owned applied state.
3. Route chords in `VoiceEditor::buttons()`/`publish()` (`VoiceEditor.cpp:43-94`),
   which already isolates per-index publication.
4. Update help text (`src/OLED/oled.cpp:1053-1055`) and `docs/voice-edit.md:8-32`
   control table together — they duplicate content with no parity test; consider one
   shared table.
5. Audition (hear-while-editing via `Sequencer::playStepNow` + mute gating) is
   explicitly deferred (`docs/voice-edit.md:32`) — separate, riskier step touching the
   cross-core mute path; do after item 2.

**Risk:** Low — editor state is already UIState-borne and host-tested; OLED rendering
hardware-only.

---

## Build sequence

Ordered for risk isolation, prerequisite flow, and quick wins first:

| Phase | Items | Why this order |
|---|---|---|
| 1 — quick wins | 3 (randomizer + tests), 7 (parity/polish) | Lowest risk, immediate musical payoff; item 3 is already a pending task and pins its own tests |
| 2 — UI features | 2 (mute/solo), 8 (Voice Edit extras) | Dormant APIs + tested policy structs; no core changes; item 8's audition depends on item 2's mute path |
| 3 — core extensions | 5 (copy/paste + undo), 4 (probability/ratchets) | Pure portable logic, TDD-friendly; item 5's snapshot/serialize is groundwork for item 1 |
| 4 — big rock | 6 (master bus), 1 (persistence) | Item 6 needs bench render-budget measurement; item 1 is the largest and consumes items 5's serialization + versioning groundwork |

Each phase lands with the host suite green (`ctest` via fresh Ninja+clang configure) and
a bench flash/verify (arduino-cli, COM33, fqbn `rp2040:rp2040:rpipico2`) before moving
on. Item 6 additionally gates on underrun-counter comparison at 48 kHz.

## Runners-up (cut from the top 8, worth keeping warm)

- **Lidar live macro** — distance as continuous performance control (filter/master fade,
  distance-gated patterns) via the existing ControlUpdate queue; fix `observeDistance`
  snap-to-0 and the 1100 vs 700 mm divisor mismatch (`Sequencer.cpp:10,213`).
- **Core-1 watchdog coverage** — audio hang currently dies silently; monotonic
  buffer-progress deadline on Core 0, host-testable via `tests/watchdog_stubs/`.
- **Feedback gap fixes** — dead `mm` LED argument (`LEDMatrixFeedback.cpp:706-713`),
  single-kind OLED notice enum, swing-name desync after fader move
  (`AlchemyControlBridge.cpp:403-414` vs `oled.cpp:447`).
- **On-device legend + reverse cycling** — 11 memorized gestures, all cyclers
  forward-only; the modifier-layer spec already designs Shift-in-Utility reverse.
- **MIDI-in clock sync over UART** — dormant CC tables + stage-only event queue make
  input-only slave clock the cheapest musically-real re-add (GP0/GP1 free).
- **UIState dead-affordance cleanup** — six writer-only flags (`modGateParamSeqLengthsMode`,
  `settingsSubMenuIndex`, etc.) worth removing before new modes inherit them.
- **Unlock 17–64 step capacity** — storage already 64, UI clamps 16; needs paging UI
  (LED playhead + OLED assume 16) and phase-safe per-track counters
  (`Sequencer.cpp:191-205` recomputes `%` per step).
