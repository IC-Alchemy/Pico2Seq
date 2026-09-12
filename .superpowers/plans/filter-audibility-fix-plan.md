# Plan: Restore Filter-parameter audibility (regression from 51ced80)

## Context

User report: "the Filter [track] doesn't seem to change the way it sounds."
Code-explorer root cause: the Filter→DSP chain is fully wired; the regression is
numeric. Commit 51ced80 narrowed the Filter random subrange
`kFilterMin/kFilterMax` from `[0.2, 0.95]` to `[0.1, 0.6]`
(src/pico2seq-core/sequencer/ParameterManager.cpp, randomizeParameters).
Firmware playback composes track values as ±0.5 modifiers around a per-preset
base (`VoiceEdit::composeLane`, standard-voice `filterCutoffBase = 0.37`), so
any track value < 0.5 − 0.37 = 0.13 clamps to an identical effective value
(dead zone), and the [0.1, 0.6] subrange maps to ~132–1318 Hz — mostly under
800 Hz on the boot presets. Result: filter changes sound like nothing.

## Global Constraints

- The user has UNCOMMITTED in-flight edits in ParameterManager.cpp,
  SequencerDefs.h, Sequencer.cpp, VoiceSetup.cpp, AudioEngine.*, StepPlayback.cpp,
  UIEventHandler.* (a value-conversion refactor, `parameterValueAsFloat`).
  Do NOT revert, stash, or commit their edits. Do NOT edit their refactor lines.
  The tree stays uncommitted at the end of the task (controller ruling: live-edited
  tree, commits deferred to the user).
- pico2seq-core stays portable (no Arduino.h).
- Host test suite must pass. Configure fresh with Ninja+clang (stale MSVC
  caches fake failures) — exact commands in
  `.agents/skills/pico2seq-codebase/references/testing-and-build.md`.
- noexcept must match exactly between declaration and definition.
- Establish a BASELINE test run BEFORE making changes; report pre-existing
  failures separately from new ones.

## Task 1: Re-center Filter random subrange + restore neutral step default + pin with tests (TDD)

1. (test first) Add regression tests that FAIL before the constants change:
   - In the existing randomize test file (grep tests/unit for randomizeParameters
     coverage): Filter draws observed across repeated randomizeParameters() calls
     must stay within [0.2, 0.8] (± small epsilon) and must NOT enter the
     standard-voice dead zone (every legal draw, composed through
     laneBase = 0.37, must produce a strictly positive, distinct effective value;
     effective sweep width across the subrange ≥ 0.5).
   - Assert Step::filterCutoff default == 0.5f (the neutral modifier value:
     composes to exactly the preset base).
2. Apply the fix:
   - ParameterManager.cpp randomizeParameters: `kFilterMin = 0.2f`,
     `kFilterMax = 0.8f` (modifier-symmetric around 0.5, dead-zone free for any
     preset anchor ≤ 0.8; restores an audible ~4-octave sweep on standard voices:
     effective 0.07–0.67 → ~158–1900 Hz at env peak).
   - SequencerDefs.h `Step::filterCutoff` default: 0.35f → 0.5f (LED-matrix
     preview only; 0.5 composes to the neutral preset base).
3. Run the full host suite; all green (pre-existing failures aside, reported).
4. Do NOT commit. Do NOT touch any other file.

## Task 2 (review gate only): scoped task review of the fix diff

Review package = hand-scoped diff of the two constants, the step default, and
the test additions only (working tree also carries the user's unrelated
refactor — reviewer must not comment on it).
