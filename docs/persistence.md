# Persistence — User Manual

Everything Pico2Seq remembers across power cycles and watchdog freezes lives in
`src/pico2seq-core/persistence/`. This manual covers both sides:

- **Players** (§1): what gets saved, which buttons save/load, autosave, and what
  happens after a crash.
- **Developers** (§2+): how to capture, store, validate, and restore a project
  snapshot without breaking the locked on-disk layout.

The core layer is **portable C++** (no `Arduino.h`, no UI types, no heap) so it
compiles identically in firmware and in the host test suite. Flash I/O
(LittleFS), retained-RAM placement (`__uninitialized_ram`), and the
save/load/apply orchestration live outside it, in `src/app/`
(`Session`, `SessionStorage`, `RetainedSession`, `Application`).

## 1. Player's guide (what the box does)

### 1.1 What is saved

One **project** = everything needed to resume exactly where you left off:

| Area | Contents |
|---|---|
| 4 patterns | All 11 parameter lanes (Note, Velocity, Filter, Attack, Decay, Octave, GateLength, Gate, Slide, Sustain, Release) per voice, **including** each lane's independent step length (polymeter survives save/load) and the full 64-step tail behind a shortened lane |
| 4 patches | Each voice's preset + every Voice Edit tweak (oscillators, filter, envelope, overdrive, waveguide/hypersaw/noise params, flags). The preset's *sound descriptors* are re-derived from flash at load, not stored |
| Settings | Tempo (45–200 BPM), master volume, scale (0–12), shuffle template (0–15), LED theme (0–9), selected voice (0–3), per-voice preset indices, per-voice editor cursor + edited flags, slide mode |

### 1.2 Save / load / autosave

| Action | How | Feedback |
|---|---|---|
| **Save now** | Utility button 2, **tap** (also `Session::requestSave()` from Alchemy bridge) | Transport pauses ~0.5 s for the flash write, then resumes. OLED `SAVED`, serial `[STORAGE] saved` |
| **Reload saved** | Utility button 2, **long-press ≥ 0.4 s** (or `Session::requestLoad()`) | OLED `LOADED` / `LOAD ERR`, serial `[STORAGE] loaded` / `load FAILED` |
| **Autosave on stop** | Automatic, ~1 s after the transport stops, **only if something changed** (CRC differs from last save) | Serial `[STORAGE] autosaved on stop` |
| **Boot restore** | Automatic: flash file first, factory defaults if none/invalid | OLED `LOADED` notice; serial `[STORAGE] session loaded` vs `no valid session; factory defaults` |

Technical notes players rarely need but should know:

- The save is **atomic**: firmware writes `/session.tmp`, then renames it over
  `/session.p2s`. A power cut mid-write leaves the old *or* the new file, never
  a half-written one.
- First boot formats the LittleFS partition (takes seconds, happens once).
  If mount *and* format both fail, persistence is disabled for that boot and
  the serial log says so.
- Flash writes stop the sequencer clock first and restart it after, so a save
  never tears a playing pattern.

### 1.3 Crash recovery (watchdog resume)

A second, independent copy of the project is mirrored into **retained RAM once
per second** (zero flash wear). After a watchdog freeze:

1. The board reboots and resumes the **live** session from retained RAM —
   you lose at most ~1 s of edits. Serial: `[RECOVERY] watchdog reset;
   resuming live session from retained RAM`.
2. If the retained copy is corrupt **or** the board freezes 3 times in a row
   without ever running healthy for 15 s, it **parks** instead: peripherals
   and voices stay untouched, the watchdog is disarmed, and the serial log
   says `[RECOVERY] session resume unavailable/exhausted. Power-cycle to
   retry.` Your flash save is still intact — a power cycle restores it.
3. Bench aid: typing `W` over the USB serial console deliberately freezes
   Core 0 so you can prove the resume path end-to-end.

## 2. Concepts and file map

```
src/pico2seq-core/persistence/        portable, tested, no Arduino
  ProjectSnapshot.h / .cpp            the data: Track/Pattern/Patch/Settings/ProjectSnapshotV1 + validate
  PatternCodec.h / .cpp               Sequencer  <->  PatternSnapshot  (capturePattern / applyPattern)
  SnapshotFormat.h / .cpp             the frame: magic + version + size + CRC-32  (crc32, write/readFrameHeader)
  RetainedSessionLogic.h              retained-RAM policy: RetainedStore, retainedValid/Refresh, decideResume

src/voice/PatchCodec.h / .cpp        VoiceConfig  <->  PatchSnapshot  (voicecodec::capturePatch / applyPatch)

src/app/                              firmware glue (hardware-bound, NOT in pico2seq-core)
  Session.h / .cpp                    whole-project capture + 3-phase apply + deferred save/load requests
  SessionStorage.h / .cpp             LittleFS file  (/session.p2s via /session.tmp)  +  static ~10 KB buffers
  RetainedSession.h / .cpp            NOLOAD retained-RAM store + 1 Hz refresh + boot-completed accounting
  Application.cpp                     boot order, 1 Hz mirror, deferred flash I/O, stop-autosave, recovery park
```

Design rules (do not break these):

- **No heap, no Arduino, no UI types in `pico2seq-core/persistence/`.**
  Everything is trivially copyable, `noexcept`, fixed-size.
- **On-disk layout is locked** by `static_assert`s. Never reorder, resize, or
  repurpose a field without bumping `SNAPSHOT_FORMAT_VERSION` and adding a
  migration — old files must keep loading or be cleanly rejected.
- **`reserved` bytes must stay zero.** They exist so `memcmp`/CRC are
  deterministic across compilers.

## 3. The snapshot structs

All in `persistence::` (`ProjectSnapshot.h`). Sizes are part of the format:

| Struct | Size | Layout |
|---|---|---|
| `TrackSnapshot` | 260 B | 64 floats `values[64]` (256 B) + `stepCount` u8 + 3 `reserved` bytes |
| `PatternSnapshot` | 2,340 B | 9 tracks (`kPatternTrackCount`), `ParamId::Note`..`Slide` |
| `EnvelopeTracksSnapshot` | 520 B | 2 tracks: `ParamId::Sustain`, `ParamId::Release` (format 2) |
| `PatchSnapshot` | 232 B | 55 × 4-byte value words (220 B) + 10 u8 (counts, engine/paramSet/filter/preset/waveforms/flags) + 2 `reserved` tail bytes, packed so there is **no** compiler-dependent padding |
| `SettingsSnapshot` | 24 B | tempo, master volume, theme, scale, shuffle, selected voice, 4 preset indices, 4 editor cursors, `changedFlags` |
| `ProjectSnapshotV1` | 10,312 B | Format 1: 4 patterns (9,360 B) + 4 patches (928 B) + settings (24 B). Only used to size old files |
| **`ProjectSnapshot`** | **12,400 B** | Format 2: the format-1 layout unchanged, then 4 × `EnvelopeTracksSnapshot` (2,080 B), `laneModel` (u32) and `reserved` (u32) |
| Flash file | 12,412 B | 12-byte frame header + 12,400-byte payload (format-1 files: 10,324 B) |

**Format 2 (2026-09-19).** A format-1 payload is byte-for-byte the prefix of
format 2 (`static_assert(offsetof(ProjectSnapshot, envelopes) == sizeof(ProjectSnapshotV1))`).
`SessionStorage::load` reads the header's version first (`frameVersion()`), reads
10,312 or 12,400 payload bytes, checks the frame against that version, and for a
format-1 file calls `upgradeFromV1()`: Sustain/Release tracks follow the patch on
16 steps and `laneModel = LANE_MODEL_OFFSETS`. `Session::applyAfterVoices()` then
converts the Velocity/Filter/Attack/Decay values in the snapshot, voice by voice,
from offsets around the loaded patch to the absolute values they played
(`VoiceEdit::convertOffsetValues()`; neutral 0.5 becomes `LANE_FOLLOWS_PATCH`), before
`applyPattern()`, and marks the snapshot `LANE_MODEL_ABSOLUTE`. The conversion works on
snapshot data because `Sequencer::setRawStepValue()` wraps at a lane's active length.
The next save writes format 2. The retained-RAM store moved to `RETAINED_VERSION = 2`,
so a watchdog resume across the firmware update falls back to the flash file.

Field notes:

- `values` always holds **all 64 steps**, even when `stepCount` is smaller.
  Shrinking a lane keeps its tail in raw storage; growing re-fills from the
  old length with the lane default (lossy by design — same as the live
  `ParameterTrack`).
- `defaultValue` is **not** persisted — it comes from the compile-time
  `CORE_PARAMETERS` table at every boot.
- `PatchSnapshot` stores **values only**. The `parameters`/`recipe` pointers
  (flash-resident descriptors) are re-derived at load by `voicecodec::applyPatch`.
- `SettingsSnapshot::changedFlags`: bit N = `voiceEditor.changed[N]` for
  N = 0..3, bit 4 = `slideMode`.
- `SettingsSnapshot::editorCursor[]` stores `VoiceEdit::Id` stable IDs per voice.
- `PatchSnapshot::presetIndex` is **stamped by the caller** (`Session`), not by
  `capturePatch` — a `VoiceConfig` doesn't know which factory preset it came from.
- `PatchSnapshot::flags` bit packing: bit0 `usePatchBases`, bit1 `baseGate`,
  bit2 `baseSlide`, bit3 `recipeRetrigger`, bit4 `hasOverdrive`, bit5
  `hasEnvelope`, bit6 `hasFilter`, bit7 `enabled`.

## 4. Developer cookbook

### 4.1 Save the whole project (firmware)

```cpp
#include "app/Session.h"
#include "app/SessionStorage.h"

// Core 0 only, never from an ISR / uClock callback:
persistence::ProjectSnapshotV1 snap;
Session::captureSession(snap);
if (SessionStorage::save(snap))
    Session::setLastSavedCrc(persistence::crc32(
        reinterpret_cast<const uint8_t*>(&snap), sizeof(snap)));
```

From UI code, don't touch flash directly — request, and let
`Application::update()` do the I/O (it stops the clock for the erase window):

```cpp
Session::requestSave();   // deferred, click-safe
Session::requestLoad();
```

### 4.2 Load and apply (order matters)

```cpp
persistence::ProjectSnapshotV1 loaded{};
if (SessionStorage::load(loaded) == SessionStorage::LoadResult::Ok) {
    Session::applyBeforeVoices(loaded);  // preset indices only — initializeVoices() consumes them
    // ... build voices ...
    Session::applyAfterVoices(loaded);   // patterns + patch values + editor cursors + volume
    // ... start clock ...
    Session::applyAfterClock(loaded);    // tempo + shuffle template + scale + theme
}
```

Why three phases: voices must exist before patterns/patches can land on them,
and the clock must exist before tempo/shuffle can be set. Boot follows exactly
this order (`Application::begin`); runtime reload calls all three back-to-back
with the clock stopped.

### 4.3 One pattern only (tests, tools, per-voice copy/paste)

```cpp
#include "persistence/PatternCodec.h"

persistence::PatternSnapshot snap;
persistence::capturePattern(seq, snap);      // Sequencer -> snapshot (raw values + lengths)

Sequencer restored(1);
restored.initializeParameters();
persistence::applyPattern(snap, restored);   // snapshot -> Sequencer
```

`applyPattern` grows each lane to 64 **first**, writes all 64 raw values, then
restores the saved length. (Writing through the length-aware setter on a short
lane would wrap tail values into the head and corrupt it.) An optional third
argument caps the restored length; the firmware (`Session::applyAfterVoices`)
passes 16, the steps a voice can show and edit, and logs
`[STORAGE] capped N saved track lengths to 16 steps` when a session held longer
lanes. Stored values past the cap stay in the snapshot.

### 4.4 One patch only

```cpp
#include "voice/PatchCodec.h"

persistence::PatchSnapshot snap;
voicecodec::capturePatch(config, snap);
snap.presetIndex = myPreset;                 // caller stamps this!

VoiceConfig out;
if (voicecodec::applyPatch(myPreset, snap, out)) {
    VoiceEdit::enablePatch(out);
    voiceManager->setVoiceConfig(voiceId, out);
}
// else: applyPatch already reset `out` to the factory preset — use it as-is.
```

`applyPatch` starts from the **factory preset** (correct descriptor pointers),
overlays every saved value, then fixes up the layout: if the saved
`engine`/`paramSet` differs from the preset's own, `out.parameters` is set to
`nullptr` so `VoiceParameters::layout()` derives it from `paramSet`. If the
saved engine is `ENGINE_RECIPE` but the preset carries no recipe, it returns
`false` (unreconstructable) with `out` reset to factory. An out-of-range
`presetIndex` also returns `false`.

### 4.5 Retained-RAM mirror (crash resume)

```cpp
#include "app/RetainedSession.h"

RetainedSession::bootInit();                 // once, at boot: validate magic + CRC
if (RetainedSession::resumeAllowed()) {      // once per boot; escalates attempt counter
    persistence::ProjectSnapshotV1 snap;
    if (RetainedSession::takeResumeSnapshot(snap)) { /* apply like a load */ }
}
// in the healthy control loop, ~1 Hz:
Session::captureSession(snap);
RetainedSession::refresh(snap);              // bumps generation, preserves attempt counter
// after the loop has run healthy 15 s (NOT at end of begin()):
RetainedSession::markBootCompleted();        // clears attempt counter -> 3 fresh resumes
```

Policy lives in pure logic (`RetainedSessionLogic.h`) so tests cover it
without hardware: `retainedValid()` (magic + version + CRC over the snapshot
only), `retainedRefresh()` (sets magic/version, `generation += 1`, CRC),
`decideResume(watchdogReset, valid, attempts)`:

| watchdog reset? | retained valid? | attempts | decision |
|---|---|---|---|
| no | — | — | `NormalBoot` |
| yes | no | — | `HaltRecovery` (park) |
| yes | yes | 0–2 | `ResumeRetained` |
| yes | yes | ≥ 3 | `HaltRecovery` (park) |

Constants: `RETAINED_MAGIC 'RET1'`, `RETAINED_VERSION 1`,
`MAX_RESUME_ATTEMPTS 3`, `RETAINED_FLAG_BOOT_COMPLETED`. `refresh()` never
resets the attempt counter; only `markBootCompleted()` does — resetting it at
the end of `begin()` would let a freeze that recurs right after every boot
resume forever.

## 5. On-disk / on-wire frame format

`SnapshotFormat.h`: 12-byte little-endian header + payload. Header and payload
may live in **separate** buffers (the loader reads them separately); the CRC
is computed over the payload buffer directly, never over bytes past the header.

```
offset  size  field
0       4     magic   0x50325331 ('P2S1', LE)
4       2     version SNAPSHOT_FORMAT_VERSION = 2 (1 still loads, see §3)
6       2     payloadSize (u16; sizeof(ProjectSnapshot) = 12400, format 1: 10312)
8       4     crc32   CRC-32/ISO-HDLC over payload (poly 0xEDB88320, init/xor 0xFFFFFFFF;
              check vector: "123456789" -> 0xCBF43926)
```

API:

```cpp
uint32_t persistence::crc32(const uint8_t* data, size_t length) noexcept;  // nullptr/0 -> 0
void persistence::writeFrameHeader(uint8_t out[12], uint32_t payloadSize, uint32_t payloadCrc) noexcept;
uint16_t persistence::frameVersion(const uint8_t header[12]) noexcept;
FrameStatus persistence::readFrameHeader(const uint8_t header[12], const uint8_t* payload,
                                         size_t payloadCapacity, uint16_t expectedPayloadSize,
                                         uint16_t expectedVersion = SNAPSHOT_FORMAT_VERSION) noexcept;
// FrameStatus: Ok | TooShort | BadMagic | BadVersion | BadSize | BadCrc
```

`payloadCapacity` must be ≥ declared size; the caller checks `TooShort` before
trusting anything. `SessionStorage::load` maps every non-`Ok` status plus a
failed `validateProjectSnapshot` to `LoadResult::BadFrame` (truncated reads
too); `NoFile` / `IoError` are distinct so boot can tell "first run" from
"broken".

## 6. Validation

```cpp
bool persistence::validateProjectSnapshot(const ProjectSnapshotV1& s) noexcept;
```

Range checks only (structural validity, not musical sense). Bounds mirror the UI:

- every lane `stepCount` in 1..64 (note: a **zero-initialized** snapshot is
  *invalid* — seed lengths or `captureSession` first),
- `presetIndices[v]` ≤ 63 (true bound re-checked against the preset count by `applyPatch`),
- tempo 45..200 BPM, scale 0..12 (13 scales), shuffle 0..15 (16 templates),
  theme 0..9 (10 themes), selected voice 0..3.

Call it on **every** load path before applying. `SessionStorage::load` already
does; the retained path relies on CRC + the same check at apply time.

## 7. Gotchas (read before changing anything)

1. **10 KB buffers are static, never stack.** `SessionStorage` keeps
   `g_saveBuffer`/`g_loadBuffer` (~10.1 KB each) as file-static — Core 0's
   Arduino loop stack cannot hold them. `Application` keeps one more
   `g_pendingBootSnapshot` for the deferred 3-phase boot apply.
2. **Never do flash I/O in ISR / uClock callback / input-scan context.**
   Use `Session::requestSave/requestLoad` → `consumePendingAction` in
   `Application::update()`. `g_pending` is a Core-0 single-writer flag.
3. **Stop the clock around saves.** `Application::update` stops the transport
   for the erase window (`stopClockForEditor`, drains pending steps) and
   restarts it after; then re-feeds the watchdog (flash ops exceed the 2 s budget).
4. **`SessionStorage::begin()` runs before the watchdog is armed.**
   First-boot LittleFS format takes seconds and must not reboot mid-format.
   It sets `autoFormat(false)` and formats explicitly with logging.
5. **`captureSession` starts from `{}`** — `changedFlags` is built with
   read-modify-write ORs, so a fresh zero base is required.
6. **Note programming is gate-controlled.** `setStepParameterValue(Note)` on a
   gate-off step is rejected — tests and tools must raise the gate first (see
   `test_persistence.cpp`).
7. **Dirty tracking is CRC-based.** Autosave compares
   `crc32(snapshot)` against `Session::lastSavedCrc()`; boot and every
   successful save/load refresh it. If you add a field to the snapshot, dirty
   detection follows automatically — but so does CRC churn from uninitialized
   padding, hence rule 8.
8. **Zero your structs.** Always start from `ProjectSnapshotV1{}` /
   `PatternSnapshot{}` so `reserved`/padding CRC deterministically. `capturePattern`
   explicitly zeroes `track.reserved`.
9. **Don't "fix" the `applyPattern` grow-first sequence.** It looks redundant;
   it prevents modulo-wrap corruption (see §4.3).
10. **Recipe patches can't cross presets.** Saving `ENGINE_RECIPE` values onto
    a non-recipe preset slot fails closed to the factory preset by design.

## 8. Testing

Host tests need no hardware (`tests/stubs/` shims the Arduino includes):

```bash
cmake -B build_test -DCMAKE_BUILD_TYPE=Debug
cmake --build build_test --parallel
./build_test/tests/pico2seq_tests "[persistence]" --reporter console
```

`tests/unit/test_persistence.cpp` covers: CRC check vector, locked sizes +
trivially-copyable, frame round-trip + each damage mode (`BadMagic`,
`BadVersion`, `BadSize`, `BadCrc`, `TooShort`), validation rejects each
out-of-range setting, pattern round-trip (values, lengths, shrink/grow tail
semantics, full random-pattern fidelity across all 9 lanes × 64 steps), patch
round-trip (untouched preset, edited patch, `paramSet`-change clears layout
pointer, recipe-without-source rejected, bad preset index rejected), golden
full-project frame round-trip, resume decision table, retained
validity/refresh/generation/CRC-damage/version-damage.

When adding a field: update the struct, the `static_assert`, `capture`/`apply`,
`validate` bounds if any, the size assertion in the test, and this manual —
then bump `SNAPSHOT_FORMAT_VERSION` if old files must be rejected, or add a
migration if they must keep loading.

## 9. Quick reference

| You want to… | Call |
|---|---|
| Save whole project | `Session::captureSession` → `SessionStorage::save` (or `Session::requestSave()`) |
| Load whole project | `SessionStorage::load` → `applyBeforeVoices` → `applyAfterVoices` → `applyAfterClock` (or `Session::requestLoad()`) |
| Copy one pattern | `persistence::capturePattern` / `persistence::applyPattern` |
| Copy one patch | `voicecodec::capturePatch` / `voicecodec::applyPatch` |
| Frame a payload | `persistence::crc32` + `writeFrameHeader` / `readFrameHeader` |
| Sanity-check a snapshot | `persistence::validateProjectSnapshot` |
| Mirror for crash resume | `RetainedSession::refresh` @ ~1 Hz; `resumeAllowed` + `takeResumeSnapshot` at boot |
| Know if boot restored | `Session::g_bootLoadedOk` |
| Know if user edited since save | `crc32(snapshot) != Session::lastSavedCrc()` |
