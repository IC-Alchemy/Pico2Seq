# Plan: Sync all .md documentation with the current project state (2026-09-30)

## Context

Branch `DeCluttered` @ fd79900. Between 2026-09-24 and 2026-09-30 the firmware gained: Arpeggiator mode on the shared performance surface (5598d0d), tempo-synced master delay divisions (265c72a), a stereo master bus with `DarkReverb` after the delay (dc321d3), persistence format 3 with a fixed 48-byte effect record + v1/v2 migration (1777aec), half-reverb-tank-by-default with engine in SRAM + memory diagnostics (de6a260), a live Reverb page (4cbe874) and live voice ADSR slider pages (9b05a69), gate sequence-length editing via voice-button holds (1dd1f1b), a 225 MHz CPU baseline (fc13927), raised Decay/Release minimums (8823787), Core 1 stall diagnostics (5c60417), and dead-code removals (e2c2c1c, 3b70269, fb5b8a7). Two read-only audits on 2026-09-30 produced per-file drift findings with code references. This plan turns those findings into documentation edits. **No code changes.**

Ground truth highlights (verified against code by the audits):
- Master chain: 4 voices (mono) → `MasterDelay` (10–750 ms, default feedback **75%** `kDefaultFeedback=0.75f`, MasterDelay.h:34; 19 tempo-sync divisions in DelayTiming.h) → `MasterReverb` = `rpdsp::DarkReverb<16384, Half>` (mono in / stereo out) → master volume/macro → **linked stereo compressor** → `interleavePcm16` → I2S 48 kHz. (src/voice/VoiceManager.cpp:310–401)
- UI: live voice ADSR page (Shift+btn6+voice), live Reverb page (Shift+6+press-2; MAIN Mix/Decay/Damping, TONE LowCut/Diffusion/ModDepth/Width), ARP play page; save = Utility **button 2** tap (1-based, bit 1; Shift+bit-1 held ≥400 ms = load), AlchemyControlBridge.cpp:561–601. Param button bit 4 records **Release** (silkscreened "Decay") — ControlSurfaceLogic.h:56–62.
- Numbers: 29 presets; Note 0–36; Attack 1 ms–2 s; Decay min 0.1; GateLength 0.1–1.0 default 0.8; Release 10 ms–8 s; Octave ±2 via `VoiceEdit::mapOctave` (runtime zones ≈136/297/458/619 mm over 55–700 mm); randomize depth 35%, never touches Gate/Slide; lidar 55–700 mm; MPR121 thresholds **45/14**, 10 ms read interval, 30 ms I2C stabilization; LED `DEFAULT_BRIGHTNESS` 222 (startup 150), LED slice 13 ms, OLED slice 40 ms; 6 test targets + opt-in benchmark, 692 tests recorded 2026-09-30 with 33 known failures; CPU baseline 225 MHz.

## Global Constraints

1. **Docs only.** No changes to code, build config, or `.vscode/arduino.json` (its `freq=150` is out of scope; docs state the 225 policy truthfully).
2. **Out of scope / do not touch:** `vendor/**`, all `.html` files, `.superpowers/plans/filter-audibility-fix-plan.md` (dated historical record — ruling), files already current (`docs/persistence.md`, `docs/arpeggiator.md`, `docs/midi.md`, `docs/recipe-performance.md`, `src/midi/README.md`).
3. **Preserve historical records.** Lines explicitly labeled as dated measurements (e.g. audio-performance.md's historical 150 MHz builds, recipe-performance.md's 300 MHz run, testing.md's dated baseline "692 on 2026-09-30") stay as-is; only current-tense claims change.
4. **Naming convention ruling:** utility tile buttons are named 1-based ("button 8" = Shift), matching existing usage. Save = Utility button 2 tap; load = hold ≥400 ms; Play/Stop = button 1. Apply consistently wherever touched.
5. **Octave-zone ruling:** docs describe runtime behavior (`VoiceEdit::mapOctave`, five zones ≈136/297/458/619 mm across the 55–700 mm window, ±2 octaves). Never present the dead `OCTAVE_ZONE_*` / `SequencerDefs.h:36–39` tables as live behavior.
6. **Truth standard:** every changed claim must match code; the implementer report lists each change with a code `file:line` citation. Verify doc line anchors with grep before editing (HEAD fd79900; lines may shift).
7. **docs/manual.md encoding:** the Read tool may reject it ("Non-ISO extended-ASCII"); use bash (`sed -n`, `grep -a`, python) to read/patch. Preserve encoding and line endings; do not rewrite the whole file.
8. **Broken doc links:** fix to existing files or remove the entry; repo has `tests/verify_docs_links.py` (run `python tests/verify_docs_links.py`; target = no findings for repo files; noise from Catch2 `_deps`/worktrees is acceptable).
9. **Git:** commits go directly on `DeCluttered` (ruling — docs-only sync, user's working branch; no push). Commit style follows repo convention: `docs: ...`.
10. CLAUDE.md is **gitignored/local-only**: edit it, but it will not appear in commits; do not `git add -f`.

## Task 1: README.md (major drift)

Findings (line anchors @ fd79900, verify by grep):
- :11 — features bullet says master chain is "delay and compressor"; add reverb: delay → DarkReverb stereo reverb → gain → linked stereo compressor (VoiceManager.cpp:310–340).
- :32 — "software gates and duration timers across all 4 voices" in VoiceSystem description → VoiceSystem holds `voiceIds[]`/`voiceStates[]`; gate truth is `VoiceState::isGateHigh`, `Sequencer::tickNoteDuration()` is the duration authority (VoiceSystem.h:17–57).
- :35,:368 — "4 test executables (315 tests total)" → **six** test targets plus opt-in `pico2seq_recipe_benchmark` (tests/CMakeLists.txt:116–219); testing.md:308 records 692 tests on 2026-09-30.
- :115,:130,:134 — 150 MHz build guidance → 225 MHz baseline (`scripts/build_pico2seq.ps1:5` defaults 225). Keep any explicitly-labeled historical measurements if present.
- :197 — `freq=150` in documented arduino-cli FQBN → `freq=225` (policy FQBN per CLAUDE.md; do NOT edit arduino.json itself).
- :239 — "400 kHz bus; SliderModule & ButtonModule8" → tile bus (Wire1) is **100 kHz** with comment "400 kHz stalls them" (ControlIO.cpp:24, HardwarePins.h:11); 400 kHz is the main Wire bus.
- :403–409 — broken links: `docs/alchemyui-tmag5273-migration.md`, `docs/superpowers/specs/*.md`, and four `docs/*.html` files do not exist → point to real existing docs or drop the rows (verify with `python tests/verify_docs_links.py`).

Verify: link checker clean for repo files; grep for "150 MHz"/"freq=150"/"315" returns nothing current-tense.

## Task 2: docs/manual.md (major drift, 951 lines)

Encoding caveat per Global Constraint 7. Findings (verify by grep; section-by-section, the file mixes current and stale blocks):
- :251–270 — "five-tier hierarchy" §1.8 omits live voice ADSR page, Reverb page, ARP play page → full priority chain per oled.cpp:213–457 (see oled.md fix in Task 5 for the tier list).
- :295,:909 — "mono mix duplicated to both channels" → stereo master bus; reverb returns stereo wet L/R (VoiceManager.cpp:310–401).
- :356 — Note "0–21 scale steps" → 0–36 (SequencerDefs.h:22–23).
- :359 — Attack "0–1 s" → 1 ms–2 s (MusicalValues.h:34–37, VoiceEditParameters.h:142).
- :227,:366 — Release "1 ms–10 s"/"up to 10 s" → 10 ms–8 s (VoiceEditParameters.h:143–144, SequencerDefs.h:178–181).
- :380 — gate-track length "Hold the Utility encoder button and tap a pad" → hold a **voice button** 400 ms and tap a pad (UITransitions.h:38–60; manual §1.2 :102 and §5 :772 already say this).
- :390 — randomize recipe (75%/33% gate chance, ~8% slides) → randomize never rewrites Gate/Slide; Note gets random 0–12; octave/gate-length neutral; other lanes triangular spread at depth 35% around the patch values (ParameterManager.h:32 `kDefaultRandomizeDepth=35`, ParameterManager.cpp:157–193).
- :425–428 — octave hand zones table (-1: 91–280 mm …) → replace with runtime mapping story per Global Constraint 5.
- :470,:476 — delay feedback "defaults to 85%"/"reboot restores … feedback 85%" → 75% (MasterDelay.h:34).
- :473 — "free-running delay, not a freeze or tempo-sync mode" → tempo-synced divisions exist: Shift + Utility button 2 toggles synced/ms; Shift + fader 2 then selects one of 19 divisions (AlchemyControlBridge.cpp:578–601, 971–976; DelayTiming.h:18–31; OLED notices DELAY SYNC/DELAY MS, oled.cpp:270–271).
- :686 — Param row "Note / Velocity / Filter / Attack / Decay / Octave" → bit 4 records **Release** (ControlSurfaceLogic.h:56–62).
- :742–745 — ARP faders "Fader 1 Octave range 1–4 / 2 Gate / 3 Swing / 4 Filter" as default layer → unshifted = Hits/Length/Rotate/Accent; Shift = Octaves/Gate/Swing/Filter (ControlSurfaceLogic.cpp:200–215, AlchemyControlBridge.cpp:871–905). Fix the inverse row at :756 too.
- :771 — encoder target order "…Attack → Decay → Note…" → order has **Release** where Decay was (`EncoderParameterMode`, SequencerDefs.h:90–102; cycle code ButtonHandlers.cpp:75–82).
- :912 — "caps brightness at 120/255" → DEFAULT_BRIGHTNESS 222, startup 150 (LEDConstants.h:15, ControlIO.cpp:26,94).
- §9 (lidar/touch section) — "50 ms stabilization" → 30 ms (SensorConstants.h:18); also verify MPR thresholds 45/14 if mentioned.
- :939 — glossary "One of 15 factory voice configurations" → 29 (PresetBank.h; §4.2 already correct).
- Header date note still says "updated 2026-09-16" → update to 2026-09-30.

## Task 3: docs/LEDMatrix.md + docs/ButtonHandlers.md (major drift)

LEDMatrix.md:
- :20 — DEFAULT_BRIGHTNESS 120 → **222** (LEDConstants.h:15); add startup brightness 150 (ControlIO.cpp:26,94) if the text describes boot state.
- :145–156 — Display Modes 1–5 missing the **Arpeggiator chord-map mode** and pitch-class note colorization → add: `renderArpPanel()` paints per-pad chord/held/latched/sounding/free states with `ArpLedPalette` hue rotation around the voice hue (LEDMatrixFeedback.cpp:1043–1123, ArpLedPalette.h; commits 5598d0d, 7eba9f0, f665d53).
- :219 — "Core 0 … ~50 Hz (20 ms)" → LED slice 13 ms ≈ 77 fps (ControlIO.cpp:23).

ButtonHandlers.md:
- :70 — Param bit 4 "Decay" → records **Release** (ControlSurfaceLogic.h:56–62, bridge comment AlchemyControlBridge.cpp:495–499).
- :96–101 — no-step fader table "Master Tempo / Swing Amount / Unassigned / Gate Length" → Tempo / **Delay mix** / **Master volume** / Gate Length (ControlSurfaceLogic.cpp:187–189; fixes internal contradiction with :114).
- :132 — "Shift + Voice 4: Enter Voice Editing mode" → same gesture held ~400 ms with Shift toggles **Arpeggiator mode** (tap-release goes to voice editor; AlchemyControlBridge.cpp:361–405). Add the ARP entry gesture.
- :86 (Session row) — add: Shift + Utility button 2 (after tap/hold save-load context) toggles master-delay ms / tempo-sync (AlchemyControlBridge.cpp:577–601).
- :325 — `OledNoticeKind { None, Randomized }` → 15 values incl. Macro, DelayMix/Time/Feedback/Sync/MsMode, ArpOn/ArpOff, Saved/Loaded/LoadError, VoiceCleared/AllCleared (UIState.h:72).

## Task 4: systems docs batch (architecture, firmware-structure, testing, audio-performance, next-steps) + local CLAUDE.md

docs/architecture.md:
- :119,:465 — "20 ms (50 Hz) Display Refresh" → OLED 40 ms (~25 fps), LED 13 ms (~77 fps) (ControlIO.cpp:18,23; :51 already correct).
- :357 — preset table "std::array<VoiceConfig, 15>" stopping at NoiseStorm → 29 presets, `constexpr Preset kPresets[]` from PresetBank.h with static_assert (VoicePresets.cpp:17–24, VoicePresets.h:21 `kPresetPadCount=31`).
- :510,:515 — "Utility button 1 tap / long-press" for save/load → **button 2** (1-based; AlchemyControlBridge.cpp:676 `case 1: // Session`, 561–575) per Global Constraint 4.

docs/firmware-structure.md:
- :56 — "every 20 ms (50 Hz)" → 13 ms LED / 40 ms OLED (ControlIO.cpp:18,23).
- :114 — "mono mix converted once, copied to left and right" → stereo after reverb; left/right convert separately (`interleavePcm16`, Pcm16.h:36–43; AudioEngine.cpp:44–45; :158–169 already correct).
- :144–145 — "48,000-float (~187.5 KiB) DelayLine" → `kCapacitySamples = 36004` (~140.6 KiB), `kMaxDelaySamples = 36000` (750 ms) decoupled (MasterDelay.h:23–27).
- Build command section: align `-B build_test` with the actual known-good dir name `build_test_ninja` (or note both).

docs/testing.md:
- :44 — "src/midi/ (TinyUSB stack)" → src/midi/ holds a removal notice only; USB is CDC-only.
- :224–251 — suite table missing `test_arpeggiator.cpp`, `test_sequencer_view.cpp`, `test_settings_pads.cpp`, `test_voice_envelope.cpp`, `test_voice_playback.cpp`, `test_lidar_recording.cpp` (all in tests/CMakeLists.txt:96–143) → add rows.
- :393 — broken link to `superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md` → remove/replace.
- Keep the dated "692 on 2026-09-30" record unchanged (Global Constraint 3).

docs/audio-performance.md:
- :32,:58,:480 — current-tense 150 MHz ("validate at the stable 150 MHz first", "the helper's default clock is 150 MHz", "Build with `-CpuMHz 150`") → 225 MHz (script default scripts/build_pico2seq.ps1:5). Do NOT touch lines 60–62/73–84/118–119 (labeled historical).

docs/next-steps.md:
- :19 — delay feedback 85% → 75% (MasterDelay.h:34).
- :66–71 — delay capacity 48,000 floats / 187.5 KiB / "47 KiB to recover" → capacity 36,004 samples, max 36,000 decoupled; the suggested optimization is DONE — mark item 6 done.
- :80–81 — "defaults to the stable 150 MHz baseline" → 225 (build_pico2seq.ps1:5); mark item 8's clock half done.
- :36–41 — test-count framing → reference current recorded baseline (692 checks, 33 known failures — testing.md:189–195,308) without inventing numbers.
- Keep genuinely open items open: item 1 (`macroAlpha_` ctor/init mismatch, VoiceManager.h:254 vs VoiceManager.cpp:249), item 3 (no on-board timing measurement), item 5 (delay mix/time/feedback + compressor macro not persisted — only the 48-byte reverb EffectsSnapshot is, ProjectSnapshot.h:154–155), item 7 (master-control definitions scattered). Refresh the header date/snapshot framing to 2026-09-30.

CLAUDE.md (local-only, gitignored — edit, never commit):
- :22 — `advanceSequencerStep()` lives in `src/app/StepPlayback.cpp:17` / StepPlayback.h:10, not UIEventHandler.
- :99 — ".vscode/arduino.json uses freq=225" → state the truth: arduino.json currently carries `freq=150` and must be aligned when next touched; CLI FQBN policy is `freq=225`.
- :111 — drop/replace the stale "150 MHz hardware A/B result" phrasing.
- :169 — remove StepPlayback from the untested list (compiled into pico2seq_tests via test_lidar_recording.cpp, tests/CMakeLists.txt:129–132).
- :176 — "Add the source file to add_executable(pico2seq_tests …)" → after 8df780d, portable sources go into the shared OBJECT libs (`pico2seq_ui_code`/`pico2seq_voice_code`/`pico2seq_persist_code`) or shared `PICO2SEQ_UI_TEST_SOURCES`/`PICO2SEQ_VOICE_TEST_SOURCES` lists (tests/CMakeLists.txt:61–114); only suite-unique files go in `add_executable`.

## Task 5: user-facing docs batch (sequencer, voice, voice-edit, scales, oled, matrix, sensors) + src READMEs

docs/sequencer.md:
- :135 — Decay min 0.0 → 0.1 (SequencerDefs.h:171). :137 — GateLength 0.001..1.0 default 0.5 → 0.1..1.0 default 0.8 (SequencerDefs.h:173). :141 — Release min 0.0 → 0.01 (SequencerDefs.h:181).
- :176–181 — randomize description → same recipe as manual.md Task 2 finding (ParameterManager.cpp:157–193).
- :243 — `advanceStep(... is_decay_button_held ...)` → `is_release_button_held` (Sequencer.h:193–197).

docs/voice.md:
- :18 — VoiceManagerBuilder/VoiceFactory deleted (e2c2c1c) → remove from Supporting Classes.
- :218–229 — VoiceState block: noteIndex 0–21 → 0–36; decayTimeSeconds 0.01 → 0.2; octaveOffset float "0.0=C2, 0.5=C3" → int8_t semitone offset default 0; add sustainLevel/releaseTimeSeconds fields (SequencerDefs.h:236–244).
- :448 — T60 "0.05–7 s at runtime" → binding range 0.05–10 s everywhere (VoiceParameters.h:78–79,109–110; Voice.cpp:783–784).

docs/voice-edit.md:
- :6 — save "Utility button 1" → button 2 (AlchemyControlBridge.cpp:565–583) per Global Constraint 4.

docs/scales.md:
- :20–21 + §6 (:176–264) — remove the removed "Precomputed Unique-Degree Rank Cache" (scaleUniqueCounts/scaleIndexToRank/scaleUniqueIndexList no longer exist; write-only caches removed 2026-09-05 per voice.md:553).
- :144–165 — `mapFloatToOctaveOffset` thresholds 0.15/0.40 → actual `1.0f/3.0f` and `2.0f/3.0f` (Sequencer.cpp:14–15); note firmware path uses injected `VoiceEdit::mapOctave` (five zones, ±2 oct; VoiceEditParameters.cpp:1066–1069) per Global Constraint 5.
- Add one line: `scaleNotesPerOctave()` and the ARP octave-row layout (scales.h:18–40).

docs/oled.md:
- :206 — Octave "`-1`, `0`, `+1`" → `%+d oct` over −2..+2 (MusicalValues.h:118, VoiceEditParameters.cpp:1066–1069).
- :44 + diagram :63–122 — 7-tier hierarchy omits the Arpeggiator play page (renders between Settings and the gate-length gauge, oled.cpp:396–401) → integrate.

docs/matrix.md:
- :73 — Param buttons list: 5th button records Release (ControlSurfaceLogic.h:56–62).
- :21 — "Polled … every 1 ms with automatic debouncing" → scanning runs in the 1 ms slice but is IRQ-gated (no I2C until the GP8 flag), no separate debounce (Matrix.h:12–13,39–40).
- :72 — drop "motorized" (passive analog tiles).

docs/sensors.md:
- :95,:312,:366 — MPR121 `setThresholds(55,22)` → **45/14** (ControlIO.cpp:28–29,139).
- :167 — "(23ms interval)" comment → `READ_INTERVAL_MS = 10` (SensorConstants.h:23; table :209 already says 10).
- :359 — "50 ms stabilization" → 30 ms (SensorConstants.h:18; table :208 already says 30).
- :74 — the "5% per encoder unit" continuous-lane claim is not backed by a constant → rephrase to the real tunables: `STEPPED_VALUE_DETENT = 0.03`, `SLOW_TURN_SCALE = 0.2` (SensorConstants.h:65,70).

src/voice/README.md:
- :84 — "Only Velocity, Filter, Attack and Decay support float-member remapping" → clarify that waveguide layouts additionally remap Sustain/Release (wgPickPosition, wgStiffness; VoiceParameters.h:113–115); the legacy `paramSet` path is the four-member one.

src/matrix/README.md:
- Fix the "Debounced button state tracking" wording to IRQ-gated state-change scanning (Matrix.h:12–13,39–40).

## Task 6: Final whole-branch review

Dispatch final reviewer over `git diff fd79900..HEAD` (docs-only) plus the ledger's deferred minors. Verify: no code files touched, claims consistent across docs (75% feedback, 0–36, button 2, 45/14, 222/150, 13/40 ms, 29 presets, ±2 oct), link checker clean for repo files, manual.md encoding intact.
