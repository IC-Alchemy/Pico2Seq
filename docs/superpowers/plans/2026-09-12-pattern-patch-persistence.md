# Pattern & Patch Persistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Everything a user programs — 4×9 parameter tracks, 4 voice patches, and the global settings — survives power-off (LittleFS flash), and a watchdog reset resumes the live session from retained RAM instead of demanding a power-cycle.

**Architecture:** One deterministic POD snapshot type (`ProjectSnapshotV1`, ~10.2 KB) with three sinks: a retained-RAM mirror (zero-wear, refreshed 1 Hz from Core 0, survives watchdog soft reboots via `__uninitialized_ram`), a LittleFS file (`/session.p2s`, framed with magic+version+CRC, written atomically via tmp+rename), and the host test suite (round-trip + corruption). All byte logic lives in portable `src/pico2seq-core/persistence/` (+ one codec in `src/voice/`); `src/app/Session*` is the thin Core-0 orchestrator. Flash writes only ever run from `Application::update()` context with the transport stopped, because a 4 KB sector erase stalls XIP on **both** cores for 45–400 ms.

**Tech Stack:** earlephilhower arduino-pico core 6.x (board `rp2040:rp2040:rpipico2`), `LittleFS` (bundled `LittleFS_OnFlash`), pico-sdk `__uninitialized_ram` section, Catch2 v3.5.2 host suite (CMake + Ninja + clang on Windows).

**Spec:** `docs/superpowers/plans/2026-09-12-functionality-improvements.md`, item 1 "Pattern & patch persistence" (⚠️ that file is **not** in the current `DeCluttered` tree — it lives at commit `e1bae0e` / branch `simp1`; read it with `git show e1bae0e:docs/superpowers/plans/2026-09-12-functionality-improvements.md`). The item's verbatim requirements are folded into Global Constraints below.

## Global Constraints

- **Storage is LittleFS_OnFlash with a 64 KB partition** — board option `flash=4194304_65536`. The packed snapshot (10,416 B ≈ 10.2 KB) **exceeds the 4 KB EEPROM-emulation cap**, so EEPROM is not an option. Sketch max drops from 4,186,112 to 4,120,576 bytes (current sketch ~238 KB — 17× headroom).
- **Save file:** `/session.p2s`, framed `{magic u32 = 0x50325331 ('P2S1'), version u16 = 1, payloadSize u16, crc32 u32}` + raw `ProjectSnapshotV1` bytes. Atomic write: write `/session.tmp`, close, `LittleFS.rename("/session.tmp", "/session.p2s")` (lfs rename atomically replaces).
- **All snapshot/checksum/codec logic is portable** — `src/pico2seq-core/persistence/` gets no `Arduino.h`, no UI, no hardware includes (codebase invariant 5). The flash I/O wrapper itself is hardware-bound by design.
- **Flash writes happen only from Core 0 `Application::update()` context, transport stopped.** Never from a uClock callback (those run in the timer ISR on Core 0), never from Core 1, never from `onClockStop()` (ISR context). Erase stalls both cores 45–400 ms (Winbond W25Q32JV: 4 KB sector erase typ 45 ms / max 400 ms) — acceptable only while silent.
- **LittleFS mounts before the watchdog arms.** `freezeWatchdogArm()` is called inside `ControlIO::beginMainBusAndLeds()` (`src/app/ControlIO.cpp:69`); a first-boot format can take multiple seconds (16 blocks × 45–400 ms) and would trip the 2 s watchdog. `SessionStorage::begin()` therefore runs before `ControlIO::beginMainBusAndLeds()`.
- **`UIState` is the single source of UI truth** — extend the struct, no new globals (spec constraint).
- **Never serialize pointers.** `VoiceConfig::parameters` / `VoiceConfig::recipe` (`src/voice/VoiceConfig.h:97-98`) are flash-resident descriptors; persist `engine`/`paramSet`/preset index and re-derive (Task 4 rules).
- **Watchdog scratch phase IDs are append-only** (`src/utils/FreezeWatchdog.h:46` comment); `FW_FAULT` keeps its explicit `0xDEADF00D`.
- `noexcept` must match exactly between declaration and definition (host clang rejects mismatches).
- No heap allocation in the audio/sequencer hot path. LittleFS/file allocation during saves (Core 0 `update()` context, transport stopped) is acceptable; nothing new allocates on Core 1 or in ISRs.
- **Host test commands** (fresh configure — stale MSVC caches fake compile failures):
  ```bash
  cmake -B build_test -G Ninja -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang++.exe" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_USE_MATH_DEFINES"
  cmake --build build_test --parallel
  ./build_test/tests/pico2seq_tests "[persistence]"
  ctest --test-dir build_test --output-on-failure
  ```
- **Never claim the Arduino firmware built.** The Arduino IDE/arduino-cli build is bench-only; the plan's host tests are the automated verification. Do not `git add build*/` artifacts.
- Work on a feature branch (e.g. `persistence`) created per superpowers:using-git-worktrees at execution time; the user edits the repo live — re-check `git status` before each commit and never stage files you didn't touch.

### Persisted state (from the survey — sizes for ARM)

| Block | Contents | Size |
|---|---|---|
| Patterns | 4 voices × 9 tracks × (64 floats + length byte + reserved) | 9,360 B |
| Patches | 4 × `VoiceConfig` value fields (55 four-byte words + 10 u8 = 230 B), pointers excluded | 920 B |
| Settings | tempo, master volume, scale, shuffle idx, theme idx, selected voice, preset indices[4], encoder bases 4×7 floats, editor cursors/changed, slideMode | 136 B |
| **Total** | `ProjectSnapshotV1` | **10,416 B** |

Transient by design (never persisted): transport position (`currentStep`, `currentStepPerParam`), `Voice` DSP state (osc phases, filters, ADSR), SPSC queues, `voicesReady`, all debounce/timestamp UI fields, sensor readings, `VoiceSystem` gates. All persisted state is Core-0-owned (audio core only receives queued copies), so `captureSession()` needs no cross-core locking.

---

### Task 1: Reserve the flash partition (build config)

**Files:**
- Modify: `.vscode/arduino.json:3`
- Modify: `scripts/build_pico2seq.ps1:60`
- Modify: `scripts/build.ps1:43`
- Modify: `CLAUDE.md:63`

**Interfaces:**
- Produces: board option `flash=4194304_65536` everywhere the firmware build is configured. Nothing else in this plan can be bench-tested until this lands, but all host-testable tasks (2–5, 9) are independent of it.

- [ ] **Step 1: Change the flash option in all four files**

In each location replace `flash=4194304_0` with `flash=4194304_65536` (64 KB reserved for LittleFS at the top of flash, below the EEPROM sector):

`.vscode/arduino.json` line 3 — inside the `configuration` string. `scripts/build_pico2seq.ps1` line 60 and `scripts/build.ps1` line 43 — the option is a standalone quoted array element `'flash=4194304_65536'`. `CLAUDE.md` line 63 — update `PICO2_FQBN` to `'rp2040:rp2040:rpipico2:flash=4194304_65536,arch=arm,freq=225,...'` (rest unchanged).

- [ ] **Step 2: Sanity-check nothing else hardcodes the old option**

Run: `grep -rn "4194304_0" .vscode scripts CLAUDE.md README.md docs 2>/dev/null`
Expected: no hits (or only historical prose that doesn't affect builds; update prose hits too).

- [ ] **Step 3: Commit**

```bash
git add .vscode/arduino.json scripts/build_pico2seq.ps1 scripts/build.ps1 CLAUDE.md
git commit -m "build: reserve 64KB LittleFS partition for session persistence"
```

Note for the reviewer: firmware compile can only be confirmed on the bench (arduino-cli with the updated FQBN); sketch max size dropping to 4,120,576 bytes is expected and harmless.

---

### Task 2: Snapshot primitives — CRC32, frame header, `ProjectSnapshotV1`, validation

**Files:**
- Create: `src/pico2seq-core/persistence/SnapshotFormat.h`
- Create: `src/pico2seq-core/persistence/SnapshotFormat.cpp`
- Create: `src/pico2seq-core/persistence/ProjectSnapshot.h`
- Create: `src/pico2seq-core/persistence/ProjectSnapshot.cpp`
- Create: `tests/unit/test_persistence.cpp`
- Modify: `tests/CMakeLists.txt` (two places: test file + sources in `pico2seq_tests`; mirror the sources in `pico2seq_voice_tests` is NOT needed — the codecs only need the main suite)
- Test: `tests/unit/test_persistence.cpp`, tag `[persistence]`

**Interfaces:**
- Consumes: `SequencerDefs.h` (`MAX_STEPS_COUNT`, `PARAM_ID_COUNT`).
- Produces (used by Tasks 3–9):
  ```cpp
  namespace persistence {
  uint32_t crc32(const uint8_t *data, size_t length) noexcept;          // CRC-32/ISO-HDLC
  constexpr uint32_t SNAPSHOT_MAGIC = 0x50325331u;   // 'P2S1'
  constexpr uint16_t SNAPSHOT_FORMAT_VERSION = 1;
  // 12-byte little-endian on-flash frame: magic | version | payloadSize | crc32(payload)
  // PODs: TrackSnapshot, PatternSnapshot, EncoderBaseSnapshot, PatchSnapshot,
  // SettingsSnapshot, ProjectSnapshotV1 (see Step 1)
  bool validateProjectSnapshot(const ProjectSnapshotV1 &s) noexcept;
  void writeFrameHeader(uint8_t out[12], uint32_t payloadSize, uint32_t payloadCrc) noexcept;
  enum class FrameStatus { Ok, TooShort, BadMagic, BadVersion, BadSize, BadCrc };
  FrameStatus readFrameHeader(const uint8_t *data, size_t length, uint16_t expectedPayloadSize) noexcept;
  }
  ```

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/test_persistence.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include "persistence/SnapshotFormat.h"
#include "persistence/ProjectSnapshot.h"

using namespace persistence;

TEST_CASE("crc32 matches the ISO-HDLC check vector", "[persistence]")
{
    const char *v = "123456789";
    REQUIRE(crc32(reinterpret_cast<const uint8_t *>(v), 9) == 0xCBF43926u);
    REQUIRE(crc32(nullptr, 0) == 0u);
}

TEST_CASE("project snapshot size is locked", "[persistence]")
{
    STATIC_REQUIRE(sizeof(ProjectSnapshotV1) == 10416u);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ProjectSnapshotV1>);
}

namespace
{
// Zero-initialized stepCounts are INVALID (validate requires 1..64); every
// test that wants a range-valid snapshot seeds lengths first.
void seedValidStepCounts(ProjectSnapshotV1 &s)
{
    for (int v = 0; v < 4; ++v)
        for (int t = 0; t < PARAM_ID_COUNT; ++t)
            s.patterns[v].tracks[t].stepCount = 16;
}
} // namespace

TEST_CASE("frame header round-trips and rejects damage", "[persistence]")
{
    ProjectSnapshotV1 snap{};
    seedValidStepCounts(snap);
    snap.settings.tempoBpm = 120.0f;
    snap.settings.currentScale = 5;
    REQUIRE(validateProjectSnapshot(snap));

    uint8_t frame[12 + sizeof(ProjectSnapshotV1)];
    writeFrameHeader(frame, sizeof(snap),
                     crc32(reinterpret_cast<const uint8_t *>(&snap), sizeof(snap)));
    std::memcpy(frame + 12, &snap, sizeof(snap));

    REQUIRE(readFrameHeader(frame, sizeof(frame), sizeof(ProjectSnapshotV1)) ==
            FrameStatus::Ok);

    SECTION("too short") { REQUIRE(readFrameHeader(frame, 11, sizeof(ProjectSnapshotV1)) == FrameStatus::TooShort); }
    SECTION("bad magic")
    {
        frame[0] ^= 0xFF;
        REQUIRE(readFrameHeader(frame, sizeof(frame), sizeof(ProjectSnapshotV1)) == FrameStatus::BadMagic);
    }
    SECTION("bad version")
    {
        frame[4] = 0x63; frame[5] = 0x00; // version 99
        REQUIRE(readFrameHeader(frame, sizeof(frame), sizeof(ProjectSnapshotV1)) == FrameStatus::BadVersion);
    }
    SECTION("bad size")
    {
        frame[6] = 0x00; frame[7] = 0x00; // payloadSize 0
        REQUIRE(readFrameHeader(frame, sizeof(frame), sizeof(ProjectSnapshotV1)) == FrameStatus::BadSize);
    }
    SECTION("bad crc")
    {
        frame[12] ^= 0xA5; // corrupt payload
        REQUIRE(readFrameHeader(frame, sizeof(frame), sizeof(ProjectSnapshotV1)) == FrameStatus::BadCrc);
    }
}

TEST_CASE("project snapshot validation rejects out-of-range settings", "[persistence]")
{
    ProjectSnapshotV1 s{};
    seedValidStepCounts(s);
    REQUIRE(validateProjectSnapshot(s)); // seeded defaults in range
    s.settings.tempoBpm = 999.0f;
    REQUIRE_FALSE(validateProjectSnapshot(s));
    s.settings.tempoBpm = 120.0f;
    s.settings.currentScale = 13;
    REQUIRE_FALSE(validateProjectSnapshot(s));
    s.settings.currentScale = 0;
    s.settings.shuffleIndex = 16;
    REQUIRE_FALSE(validateProjectSnapshot(s));
    s.settings.shuffleIndex = 0;
    s.settings.themeIndex = -1;
    REQUIRE_FALSE(validateProjectSnapshot(s));
    s.settings.themeIndex = 10;
    REQUIRE_FALSE(validateProjectSnapshot(s));
}
```

- [ ] **Step 2: Register the test and sources in CMake**

In `tests/CMakeLists.txt`, add to `add_executable(pico2seq_tests ...)`:
- `unit/test_persistence.cpp` in the test-file group (after `unit/test_voice_edit.cpp`), and
- a new group after `# Sequencer logic`:
  ```cmake
  # Session persistence (portable snapshot + codecs)
  ${CORE_DIR}/persistence/SnapshotFormat.cpp
  ${CORE_DIR}/persistence/ProjectSnapshot.cpp
  ```

- [ ] **Step 3: Run the tests to verify they fail**

```bash
cmake --build build_test --parallel && ./build_test/tests/pico2seq_tests "[persistence]"
```
Expected: compile error — `SnapshotFormat.h` / `ProjectSnapshot.h` not found.

- [ ] **Step 4: Implement the format**

`src/pico2seq-core/persistence/SnapshotFormat.h`:

```cpp
#ifndef PICO2SEQ_SNAPSHOT_FORMAT_H
#define PICO2SEQ_SNAPSHOT_FORMAT_H

#include <cstddef>
#include <cstdint>

namespace persistence
{

constexpr uint32_t SNAPSHOT_MAGIC = 0x50325331u; // 'P2S1'
constexpr uint16_t SNAPSHOT_FORMAT_VERSION = 1;

// CRC-32/ISO-HDLC (the zlib/IEEE variant): poly 0xEDB88320, init/final 0xFFFFFFFF.
uint32_t crc32(const uint8_t *data, size_t length) noexcept;

struct FrameHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t payloadSize;
    uint32_t crc32;
};

void writeFrameHeader(uint8_t out[12], uint32_t payloadSize, uint32_t payloadCrc) noexcept;

enum class FrameStatus { Ok, TooShort, BadMagic, BadVersion, BadSize, BadCrc };
FrameStatus readFrameHeader(const uint8_t *data, size_t length, uint16_t expectedPayloadSize) noexcept;

} // namespace persistence

#endif
```

`src/pico2seq-core/persistence/SnapshotFormat.cpp`:

```cpp
#include "SnapshotFormat.h"

namespace persistence
{

uint32_t crc32(const uint8_t *data, size_t length) noexcept
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

void writeFrameHeader(uint8_t out[12], uint32_t payloadSize, uint32_t payloadCrc) noexcept
{
    out[0] = static_cast<uint8_t>(SNAPSHOT_MAGIC);
    out[1] = static_cast<uint8_t>(SNAPSHOT_MAGIC >> 8);
    out[2] = static_cast<uint8_t>(SNAPSHOT_MAGIC >> 16);
    out[3] = static_cast<uint8_t>(SNAPSHOT_MAGIC >> 24);
    out[4] = static_cast<uint8_t>(SNAPSHOT_FORMAT_VERSION);
    out[5] = static_cast<uint8_t>(SNAPSHOT_FORMAT_VERSION >> 8);
    out[6] = static_cast<uint8_t>(payloadSize);
    out[7] = static_cast<uint8_t>(payloadSize >> 8);
    for (int i = 0; i < 4; ++i)
        out[8 + i] = static_cast<uint8_t>(payloadCrc >> (8 * i));
}

FrameStatus readFrameHeader(const uint8_t *data, size_t length, uint16_t expectedPayloadSize) noexcept
{
    if (length < 12)
        return FrameStatus::TooShort;
    const uint32_t magic = data[0] | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) |
                           (uint32_t(data[3]) << 24);
    if (magic != SNAPSHOT_MAGIC)
        return FrameStatus::BadMagic;
    const uint16_t version = data[4] | (uint16_t(data[5]) << 8);
    if (version != SNAPSHOT_FORMAT_VERSION)
        return FrameStatus::BadVersion;
    const uint16_t size = data[6] | (uint16_t(data[7]) << 8);
    if (size != expectedPayloadSize)
        return FrameStatus::BadSize;
    if (length < 12u + size)
        return FrameStatus::TooShort;
    const uint32_t expected = data[8] | (uint32_t(data[9]) << 8) | (uint32_t(data[10]) << 16) |
                              (uint32_t(data[11]) << 24);
    if (crc32(data + 12, size) != expected)
        return FrameStatus::BadCrc;
    return FrameStatus::Ok;
}

} // namespace persistence
```

`src/pico2seq-core/persistence/ProjectSnapshot.h`:

```cpp
#ifndef PICO2SEQ_PROJECT_SNAPSHOT_H
#define PICO2SEQ_PROJECT_SNAPSHOT_H

#include "../sequencer/SequencerDefs.h"
#include <cstdint>
#include <type_traits>

namespace persistence
{

// One parameter track. Full 64-step storage so patterns keep their tail when
// lengths shrink and grow again; defaultValue is NOT persisted (it only ever
// comes from the compile-time CORE_PARAMETERS table).
struct TrackSnapshot
{
    float values[SequencerConstants::MAX_STEPS_COUNT]; // 64 floats = 256 B
    uint8_t stepCount;
    uint8_t reserved[3]; // deterministic size/alignment, must stay zero
};

struct PatternSnapshot
{
    TrackSnapshot tracks[PARAM_ID_COUNT]; // 9 tracks = 2,340 B
};

// Mirrors EncoderBaseValues (SequencerDefs.h) field-for-field, kept as plain
// floats so the core does not depend on the struct's future layout changes.
struct EncoderBaseSnapshot
{
    float note, velocity, filter, attack, decay, octave, slideTime;
};

// VoiceConfig value fields, pointers excluded (parameters/recipe are
// flash-resident descriptors re-derived at load, see PatchCodec).
struct PatchSnapshot
{
    // 53 floats / int32s
    float baseNote, baseVelocity, baseOctave, baseGateLength, slideSeconds;
    float oscAmplitudes[3];
    float oscDetuning[3];
    float oscPulseWidth[3];
    int32_t harmony[3];
    float macro1, macro2, macro3;
    float fmModFeedback, phaseTriangleFold, spectralSubRatio, spectralSubShape, prismDriftChaos;
    float noiseSourceLevel, noiseChaosRate, filterEnvelopeAmount, filterEnvelopeFloor;
    float wgT60, wgBrightness, wgPickPosition, wgPickHardness, wgStiffness, wgDetune;
    float hypersawDetune, hypersawMix;
    float noiseDiffuseSize, noiseDiffuseMix, noiseSwarmColor, noiseSwarmRegen, noiseChaosLevel;
    float filterRes, filterDrive, filterPassbandGain, filterCutoffBase;
    float highPassFreq, highPassRes;
    float overdriveGain, overdriveDrive;
    float defaultAttack, defaultDecay, defaultSustain, defaultRelease;
    float outputLevel;
    // small fields last -> no interior padding
    uint8_t oscillatorCount, engine, paramSet, filterType, filterMode, presetIndex;
    uint8_t oscWaveforms[3];
    uint8_t flags; // bit0 usePatchBases, bit1 baseGate, bit2 baseSlide, bit3 recipeRetrigger,
                   // bit4 hasOverdrive, bit5 hasEnvelope, bit6 hasFilter, bit7 enabled
};

struct SettingsSnapshot
{
    float tempoBpm;
    float masterVolume;
    EncoderBaseSnapshot encoderBases[4]; // 112 B
    int32_t themeIndex;
    uint8_t currentScale;
    uint8_t shuffleIndex;
    uint8_t selectedVoice;
    uint8_t presetIndices[4];
    uint8_t editorCursor[4]; // VoiceEdit::Id per voice (stable IDs)
    uint8_t changedFlags;    // bit N = voiceEditor.changed[N]; bit4 = slideMode
};

struct ProjectSnapshotV1
{
    PatternSnapshot patterns[4]; // 9,360 B
    PatchSnapshot patches[4];    // 936 B
    SettingsSnapshot settings;   // 138 B
};
static_assert(sizeof(TrackSnapshot) == 260, "locked layout");
static_assert(sizeof(PatternSnapshot) == 2340, "locked layout");
static_assert(sizeof(PatchSnapshot) == 230, "locked layout"); // 55 4-byte words + 10 u8
static_assert(sizeof(SettingsSnapshot) == 136, "locked layout");
static_assert(sizeof(ProjectSnapshotV1) == 10416, "locked layout");

// Range checks only — structural validity, not musical sense. Bounds mirror
// the UI: tempo 45..200 BPM (UIEventHandler clamps at 45, fader tops at 200),
// 13 scales, NUM_SHUFFLE_TEMPLATES=16, 10 LED themes.
bool validateProjectSnapshot(const ProjectSnapshotV1 &s) noexcept;

} // namespace persistence

#endif
```

`src/pico2seq-core/persistence/ProjectSnapshot.cpp`:

```cpp
#include "ProjectSnapshot.h"

namespace persistence
{

bool validateProjectSnapshot(const ProjectSnapshotV1 &s) noexcept
{
    for (uint8_t voice = 0; voice < 4; ++voice)
    {
        for (uint8_t track = 0; track < PARAM_ID_COUNT; ++track)
        {
            const uint8_t count = s.patterns[voice].tracks[track].stepCount;
            if (count == 0 || count > SequencerConstants::MAX_STEPS_COUNT)
                return false;
        }
        if (s.settings.presetIndices[voice] > 63) // true bound checked by PatchCodec (preset count)
            return false;
    }
    const auto &set = s.settings;
    if (set.tempoBpm < 45.0f || set.tempoBpm > 200.0f)
        return false;
    if (set.currentScale > 12)
        return false;
    if (set.shuffleIndex > 15)
        return false;
    if (set.themeIndex < 0 || set.themeIndex > 9)
        return false;
    if (set.selectedVoice > 3)
        return false;
    return true;
}

} // namespace persistence
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build_test --parallel && ./build_test/tests/pico2seq_tests "[persistence]"
```
Expected: 4 passing test cases.

- [ ] **Step 6: Run the whole suite (no regressions)**

```bash
ctest --test-dir build_test --output-on-failure
```
Expected: all green (186+4 main, 62 voice, 4 watchdog, 1 audio).

- [ ] **Step 7: Commit**

```bash
git add src/pico2seq-core/persistence tests/unit/test_persistence.cpp tests/CMakeLists.txt
git commit -m "feat(persistence): versioned snapshot frame, CRC32, ProjectSnapshotV1 POD"
```

---

### Task 3: Pattern codec + raw track accessors

**Files:**
- Modify: `src/pico2seq-core/sequencer/ParameterManager.h` (add 2 methods)
- Modify: `src/pico2seq-core/sequencer/ParameterManager.cpp` (implement)
- Modify: `src/pico2seq-core/sequencer/Sequencer.h` (add 2 forwarding methods)
- Modify: `src/pico2seq-core/sequencer/Sequencer.cpp` (implement)
- Create: `src/pico2seq-core/persistence/PatternCodec.h`
- Create: `src/pico2seq-core/persistence/PatternCodec.cpp`
- Modify: `tests/CMakeLists.txt`, `tests/unit/test_persistence.cpp`

**Interfaces:**
- Consumes: `ProjectSnapshot.h` (Task 2), `Sequencer`, `ParameterManager`.
- Produces:
  ```cpp
  namespace persistence {
  void capturePattern(const Sequencer &sequencer, PatternSnapshot &out) noexcept;
  void applyPattern(const PatternSnapshot &in, Sequencer &sequencer) noexcept;
  }
  // New on Sequencer (forwards to ParameterManager):
  float getRawStepValue(ParamId id, uint8_t stepIdx) const;   // no modulo wrap
  void setRawStepValue(ParamId id, uint8_t stepIdx, float value); // no clamp/round
  ```

Why raw accessors: `getValue`/`setValue` wrap modulo `stepCount` (`rpdsp::parameter_track.h:44-50`), so steps beyond the current polymetric length are unreachable — but the tail holds programmed values that must survive a shrink→regrow cycle (`resize()` only fills grown steps with the default). Saved values were already clamp/round-normalized when they were written by the UI, so re-applying them raw is idempotent.

- [ ] **Step 1: Write the failing tests** (append to `tests/unit/test_persistence.cpp`)

```cpp
#include "persistence/PatternCodec.h"
#include "sequencer/Sequencer.h"

TEST_CASE("pattern codec round-trips values, lengths, and shrink-grown tails", "[persistence]")
{
    Sequencer seq(1);
    seq.initializeParameters(); // uses CORE_PARAMETERS defaults
    seq.setStepParameterValue(ParamId::Note, 0, 12.0f);
    seq.setStepParameterValue(ParamId::Note, 1, 19.0f);
    seq.setParameterStepCount(ParamId::Note, 16);

    // Shrink the Gate track, program the tail while long, then shrink: the
    // tail must come back when the pattern is restored at the longer length.
    seq.setParameterStepCount(ParamId::Gate, 32);
    seq.setStepParameterValue(ParamId::Gate, 20, 1.0f);
    seq.setParameterStepCount(ParamId::Gate, 16);

    persistence::PatternSnapshot snap;
    persistence::capturePattern(seq, snap);

    Sequencer restored(1);
    restored.initializeParameters();
    persistence::applyPattern(snap, restored);

    REQUIRE(restored.getStepParameterValue(ParamId::Note, 0) == 12.0f);
    REQUIRE(restored.getStepParameterValue(ParamId::Note, 1) == 19.0f);
    REQUIRE(restored.getParameterStepCount(ParamId::Note) == 16);
    REQUIRE(restored.getParameterStepCount(ParamId::Gate) == 16);
    // Tail beyond the Gate length survived the round-trip.
    restored.setParameterStepCount(ParamId::Gate, 32);
    REQUIRE(restored.getStepParameterValue(ParamId::Gate, 20) == 1.0f);
}

TEST_CASE("pattern codec preserves every track of a random pattern", "[persistence]")
{
    Sequencer seq(2);
    seq.initializeParameters();
    seq.randomizeParameters();
    persistence::PatternSnapshot snap;
    persistence::capturePattern(seq, snap);

    Sequencer restored(2);
    restored.initializeParameters();
    persistence::applyPattern(snap, restored);
    for (uint8_t t = 0; t < PARAM_ID_COUNT; ++t)
    {
        const ParamId id = static_cast<ParamId>(t);
        REQUIRE(restored.getParameterStepCount(id) == seq.getParameterStepCount(id));
        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
            REQUIRE(restored.getRawStepValue(id, step) == seq.getRawStepValue(id, step));
    }
}
```

(If `Sequencer`'s public API differs — e.g. `initializeParameters` is named differently — match the real names from `Sequencer.h:100-120`; the codebase survey confirmed `getStepParameterValue`/`setStepParameterValue`/`getParameterStepCount`/`setParameterStepCount` exist at `Sequencer.h:108-111`.)

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build_test --parallel && ./build_test/tests/pico2seq_tests "[persistence]"
```
Expected: compile error — `capturePattern` / `getRawStepValue` not defined.

- [ ] **Step 3: Add raw accessors to `ParameterManager` and `Sequencer`**

`ParameterManager.h` (after `setValue`, public):
```cpp
    // Direct (non-wrapping, non-clamping) access for persistence. Steps beyond
    // the current stepCount keep programmed tail values across save/restore;
    // callers must only feed back values that previously passed setValue.
    float getRawValue(ParamId id, uint8_t stepIdx) const;
    void setRawValue(ParamId id, uint8_t stepIdx, float value);
```

`ParameterManager.cpp`:
```cpp
float ParameterManager::getRawValue(ParamId id, uint8_t stepIdx) const
{
  if (stepIdx >= SequencerConstants::MAX_STEPS_COUNT)
    return 0.0f;
  return _tracks[static_cast<size_t>(id)].rawValue(stepIdx);
}

void ParameterManager::setRawValue(ParamId id, uint8_t stepIdx, float value)
{
  if (stepIdx >= SequencerConstants::MAX_STEPS_COUNT)
    return;
  _tracks[static_cast<size_t>(id)].setValue(stepIdx, value);
}
```

Note: `ParameterTrack::setValue` is direct storage (no modulo — the modulo is in `getValue`/the public `ParameterManager::setValue` wrapper path); verify against `src/rpdsp/src/rpdsp/parameter_track.h:40-55` when implementing and adjust if the rpdsp version wraps internally (use `values_[i]`-level access via a new rpdsp raw setter only if forced — prefer not touching the submodule).

`Sequencer.h` (next to the existing parameter forwarders at `:108-111`):
```cpp
    float getRawStepValue(ParamId id, uint8_t stepIdx) const;
    void setRawStepValue(ParamId id, uint8_t stepIdx, float value);
```
`Sequencer.cpp`: one-line forwards to `parameterManager.getRawValue` / `setRawValue`.

- [ ] **Step 4: Implement the codec**

`src/pico2seq-core/persistence/PatternCodec.h`:
```cpp
#ifndef PICO2SEQ_PATTERN_CODEC_H
#define PICO2SEQ_PATTERN_CODEC_H

#include "ProjectSnapshot.h"

class Sequencer;

namespace persistence
{
void capturePattern(const Sequencer &sequencer, PatternSnapshot &out) noexcept;
void applyPattern(const PatternSnapshot &in, Sequencer &sequencer) noexcept;
} // namespace persistence

#endif
```

`src/pico2seq-core/persistence/PatternCodec.cpp`:
```cpp
#include "PatternCodec.h"
#include "../sequencer/Sequencer.h"

namespace persistence
{

void capturePattern(const Sequencer &sequencer, PatternSnapshot &out) noexcept
{
    for (uint8_t t = 0; t < PARAM_ID_COUNT; ++t)
    {
        const ParamId id = static_cast<ParamId>(t);
        TrackSnapshot &track = out.tracks[t];
        track.reserved = {0, 0, 0};
        track.stepCount = sequencer.getParameterStepCount(id);
        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
            track.values[step] = sequencer.getRawStepValue(id, step);
    }
}

void applyPattern(const PatternSnapshot &in, Sequencer &sequencer) noexcept
{
    for (uint8_t t = 0; t < PARAM_ID_COUNT; ++t)
    {
        const ParamId id = static_cast<ParamId>(t);
        const TrackSnapshot &track = in.tracks[t];
        const uint8_t count = (track.stepCount >= 1 && track.stepCount <= SequencerConstants::MAX_STEPS_COUNT)
                                  ? track.stepCount : SequencerConstants::DEFAULT_STEPS_COUNT;
        sequencer.setParameterStepCount(id, count);
        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
            sequencer.setRawStepValue(id, step, track.values[step]);
    }
}

} // namespace persistence
```

Add both files to the `pico2seq_tests` CMake source list under the persistence group.

- [ ] **Step 5: Run tests — pass** (same command; 6 test cases now)

- [ ] **Step 6: Full suite + commit**

```bash
ctest --test-dir build_test --output-on-failure
git add src/pico2seq-core/sequencer/ParameterManager.h src/pico2seq-core/sequencer/ParameterManager.cpp src/pico2seq-core/sequencer/Sequencer.h src/pico2seq-core/sequencer/Sequencer.cpp src/pico2seq-core/persistence/PatternCodec.h src/pico2seq-core/persistence/PatternCodec.cpp tests/unit/test_persistence.cpp tests/CMakeLists.txt
git commit -m "feat(persistence): pattern capture/apply with raw tail preservation"
```

---

### Task 4: Patch codec — `VoiceConfig` ⇄ `PatchSnapshot` with pointer re-derivation

**Files:**
- Create: `src/voice/PatchCodec.h`
- Create: `src/voice/PatchCodec.cpp`
- Modify: `tests/CMakeLists.txt`, `tests/unit/test_persistence.cpp`

**Interfaces:**
- Consumes: `VoiceConfig` (`src/voice/VoiceConfig.h`), `VoicePresets::getPresetConfig` (`src/voice/VoicePresets.h`), `ProjectSnapshot.h`.
- Produces:
  ```cpp
  namespace voicecodec {
  void capturePatch(const VoiceConfig &config, persistence::PatchSnapshot &out) noexcept;
  // Rebuilds a full config: starts from the factory preset (correct flash-resident
  // descriptor pointers), overlays all saved value fields, then re-derives pointers.
  // Returns false if the saved engine is ENGINE_RECIPE but the preset carries no
  // recipe (cannot reconstruct) — *out is then left as the unmodified preset config.
  bool applyPatch(uint8_t presetIndex, const persistence::PatchSnapshot &in, VoiceConfig &out) noexcept;
  }
  ```
  Pointer rules (host-tested):
  1. `out` starts as `VoicePresets::getPresetConfig(presetIndex)` — preset pointers are correct for that preset (recipe presets assign `recipe` + `parameters`, `presets/RecipePresets.h:43-44`).
  2. Overlay every value field from `in`. Do **not** copy `parameters`/`recipe` (they don't exist in `PatchSnapshot`).
  3. If the overlay changed `engine` or `paramSet` away from the preset's values, set `out.parameters = nullptr` — `VoiceParameters::layout()` then derives from `paramSet` (`src/voice/VoiceParameters.cpp:89-94`; `VoiceConfig.h:97` documents "null selects legacy paramSet").
  4. If `out.engine == ENGINE_RECIPE && out.recipe == nullptr` → return false (caller keeps the factory preset).
  5. `presetIndex >= VoicePresets::getPresetCount()` → return false.

- [ ] **Step 1: Write the failing tests** (append)

```cpp
#include "voice/PatchCodec.h"
#include "voice/VoicePresets.h"

using namespace persistence;

TEST_CASE("patch codec round-trips a preset untouched", "[persistence]")
{
    const VoiceConfig original = VoicePresets::getPresetConfig(4); // default voice-0 preset
    PatchSnapshot snap;
    voicecodec::capturePatch(original, snap);
    REQUIRE(snap.presetIndex == 4);

    VoiceConfig restored;
    REQUIRE(voicecodec::applyPatch(4, snap, restored));
    REQUIRE(restored.engine == original.engine);
    REQUIRE(restored.paramSet == original.paramSet);
    REQUIRE(restored.baseNote == original.baseNote);
    REQUIRE(restored.oscWaveforms[0] == original.oscWaveforms[0]);
    REQUIRE(restored.filterRes == original.filterRes);
    REQUIRE(restored.defaultAttack == original.defaultAttack);
    REQUIRE(restored.enabled == original.enabled);
}

TEST_CASE("patch codec round-trips an edited patch", "[persistence]")
{
    VoiceConfig original = VoicePresets::getPresetConfig(2);
    original.baseNote = 7.0f;
    original.oscDetuning[1] = -5.5f;
    original.harmony[2] = 7;
    original.wgT60 = 4.25f;
    original.filterMode = VoiceFilterMode::BP12;
    original.hasOverdrive = true;
    original.overdriveDrive = 0.9f;
    original.outputLevel = 0.31f;

    PatchSnapshot snap;
    voicecodec::capturePatch(original, snap);
    VoiceConfig restored;
    REQUIRE(voicecodec::applyPatch(2, snap, restored));
    REQUIRE(restored.baseNote == 7.0f);
    REQUIRE(restored.oscDetuning[1] == -5.5f);
    REQUIRE(restored.harmony[2] == 7);
    REQUIRE(restored.wgT60 == 4.25f);
    REQUIRE(restored.filterMode == VoiceFilterMode::BP12);
    REQUIRE(restored.hasOverdrive);
    REQUIRE(restored.overdriveDrive == 0.9f);
    REQUIRE(restored.outputLevel == 0.31f);
}

TEST_CASE("paramSet change clears the preset layout pointer", "[persistence]")
{
    const VoiceConfig preset = VoicePresets::getPresetConfig(2);
    VoiceConfig edited = preset;
    edited.paramSet = PARAMSET_WAVEGUIDE; // user re-purposed the slots
    PatchSnapshot snap;
    voicecodec::capturePatch(edited, snap);
    VoiceConfig restored;
    REQUIRE(voicecodec::applyPatch(2, snap, restored));
    REQUIRE(restored.paramSet == PARAMSET_WAVEGUIDE);
    REQUIRE(restored.parameters == nullptr); // layout() now derives from paramSet
}

TEST_CASE("recipe engine without a recipe source is rejected", "[persistence]")
{
    const VoiceConfig preset = VoicePresets::getPresetConfig(2); // non-recipe preset
    VoiceConfig edited = preset;
    edited.engine = ENGINE_RECIPE;
    PatchSnapshot snap;
    voicecodec::capturePatch(edited, snap);
    VoiceConfig restored;
    REQUIRE_FALSE(voicecodec::applyPatch(2, snap, restored));
    REQUIRE(restored.engine == preset.engine); // fell back to factory preset
}

TEST_CASE("out-of-range preset index is rejected", "[persistence]")
{
    PatchSnapshot snap;
    VoiceConfig restored;
    REQUIRE_FALSE(voicecodec::applyPatch(VoicePresets::getPresetCount(), snap, restored));
}
```

- [ ] **Step 2: Run to verify failure** — compile error, `voice/PatchCodec.h` missing.

- [ ] **Step 3: Implement**

`src/voice/PatchCodec.h`:
```cpp
#ifndef PICO2SEQ_PATCH_CODEC_H
#define PICO2SEQ_PATCH_CODEC_H

#include "../pico2seq-core/persistence/ProjectSnapshot.h"

struct VoiceConfig;

namespace voicecodec
{
void capturePatch(const VoiceConfig &config, persistence::PatchSnapshot &out) noexcept;
bool applyPatch(uint8_t presetIndex, const persistence::PatchSnapshot &in,
                VoiceConfig &out) noexcept;
} // namespace voicecodec

#endif
```

`src/voice/PatchCodec.cpp` — full field list, no shortcuts:

```cpp
#include "PatchCodec.h"
#include "VoiceConfig.h"
#include "VoicePresets.h"

namespace voicecodec
{
namespace
{
constexpr uint8_t kUsePatchBases = 1u << 0;
constexpr uint8_t kBaseGate = 1u << 1;
constexpr uint8_t kBaseSlide = 1u << 2;
constexpr uint8_t kRecipeRetrigger = 1u << 3;
constexpr uint8_t kHasOverdrive = 1u << 4;
constexpr uint8_t kHasEnvelope = 1u << 5;
constexpr uint8_t kHasFilter = 1u << 6;
constexpr uint8_t kEnabled = 1u << 7;
} // namespace

void capturePatch(const VoiceConfig &c, persistence::PatchSnapshot &o) noexcept
{
    o.baseNote = c.baseNote; o.baseVelocity = c.baseVelocity; o.baseOctave = c.baseOctave;
    o.baseGateLength = c.baseGateLength; o.slideSeconds = c.slideSeconds;
    for (int i = 0; i < 3; ++i)
    {
        o.oscAmplitudes[i] = c.oscAmplitudes[i];
        o.oscDetuning[i] = c.oscDetuning[i];
        o.oscPulseWidth[i] = c.oscPulseWidth[i];
        o.harmony[i] = c.harmony[i];
        o.oscWaveforms[i] = c.oscWaveforms[i];
    }
    o.macro1 = c.macro1; o.macro2 = c.macro2; o.macro3 = c.macro3;
    o.fmModFeedback = c.fmModFeedback; o.phaseTriangleFold = c.phaseTriangleFold;
    o.spectralSubRatio = c.spectralSubRatio; o.spectralSubShape = c.spectralSubShape;
    o.prismDriftChaos = c.prismDriftChaos;
    o.noiseSourceLevel = c.noiseSourceLevel; o.noiseChaosRate = c.noiseChaosRate;
    o.filterEnvelopeAmount = c.filterEnvelopeAmount; o.filterEnvelopeFloor = c.filterEnvelopeFloor;
    o.wgT60 = c.wgT60; o.wgBrightness = c.wgBrightness; o.wgPickPosition = c.wgPickPosition;
    o.wgPickHardness = c.wgPickHardness; o.wgStiffness = c.wgStiffness; o.wgDetune = c.wgDetune;
    o.hypersawDetune = c.hypersawDetune; o.hypersawMix = c.hypersawMix;
    o.noiseDiffuseSize = c.noiseDiffuseSize; o.noiseDiffuseMix = c.noiseDiffuseMix;
    o.noiseSwarmColor = c.noiseSwarmColor; o.noiseSwarmRegen = c.noiseSwarmRegen;
    o.noiseChaosLevel = c.noiseChaosLevel;
    o.filterRes = c.filterRes; o.filterDrive = c.filterDrive;
    o.filterPassbandGain = c.filterPassbandGain; o.filterCutoffBase = c.filterCutoffBase;
    o.highPassFreq = c.highPassFreq; o.highPassRes = c.highPassRes;
    o.overdriveGain = c.overdriveGain; o.overdriveDrive = c.overdriveDrive;
    o.defaultAttack = c.defaultAttack; o.defaultDecay = c.defaultDecay;
    o.defaultSustain = c.defaultSustain; o.defaultRelease = c.defaultRelease;
    o.outputLevel = c.outputLevel;
    o.oscillatorCount = c.oscillatorCount;
    o.engine = c.engine; o.paramSet = c.paramSet;
    o.filterType = c.filterType; o.filterMode = static_cast<uint8_t>(c.filterMode);
    o.flags = 0;
    if (c.usePatchBases) o.flags |= kUsePatchBases;
    if (c.baseGate) o.flags |= kBaseGate;
    if (c.baseSlide) o.flags |= kBaseSlide;
    if (c.recipeRetrigger) o.flags |= kRecipeRetrigger;
    if (c.hasOverdrive) o.flags |= kHasOverdrive;
    if (c.hasEnvelope) o.flags |= kHasEnvelope;
    if (c.hasFilter) o.flags |= kHasFilter;
    if (c.enabled) o.flags |= kEnabled;
}

bool applyPatch(uint8_t presetIndex, const persistence::PatchSnapshot &in, VoiceConfig &out) noexcept
{
    if (presetIndex >= VoicePresets::getPresetCount())
        return false;
    out = VoicePresets::getPresetConfig(presetIndex);

    const uint8_t presetEngine = out.engine;
    const uint8_t presetParamSet = out.paramSet;
    const VoiceParameterLayout *const presetParameters = out.parameters;

    out.baseNote = in.baseNote; out.baseVelocity = in.baseVelocity; out.baseOctave = in.baseOctave;
    out.baseGateLength = in.baseGateLength; out.slideSeconds = in.slideSeconds;
    for (int i = 0; i < 3; ++i)
    {
        out.oscAmplitudes[i] = in.oscAmplitudes[i];
        out.oscDetuning[i] = in.oscDetuning[i];
        out.oscPulseWidth[i] = in.oscPulseWidth[i];
        out.harmony[i] = in.harmony[i];
        out.oscWaveforms[i] = in.oscWaveforms[i];
    }
    out.macro1 = in.macro1; out.macro2 = in.macro2; out.macro3 = in.macro3;
    out.fmModFeedback = in.fmModFeedback; out.phaseTriangleFold = in.phaseTriangleFold;
    out.spectralSubRatio = in.spectralSubRatio; out.spectralSubShape = in.spectralSubShape;
    out.prismDriftChaos = in.prismDriftChaos;
    out.noiseSourceLevel = in.noiseSourceLevel; out.noiseChaosRate = in.noiseChaosRate;
    out.filterEnvelopeAmount = in.filterEnvelopeAmount; out.filterEnvelopeFloor = in.filterEnvelopeFloor;
    out.wgT60 = in.wgT60; out.wgBrightness = in.wgBrightness; out.wgPickPosition = in.wgPickPosition;
    out.wgPickHardness = in.wgPickHardness; out.wgStiffness = in.wgStiffness; out.wgDetune = in.wgDetune;
    out.hypersawDetune = in.hypersawDetune; out.hypersawMix = in.hypersawMix;
    out.noiseDiffuseSize = in.noiseDiffuseSize; out.noiseDiffuseMix = in.noiseDiffuseMix;
    out.noiseSwarmColor = in.noiseSwarmColor; out.noiseSwarmRegen = in.noiseSwarmRegen;
    out.noiseChaosLevel = in.noiseChaosLevel;
    out.filterRes = in.filterRes; out.filterDrive = in.filterDrive;
    out.filterPassbandGain = in.filterPassbandGain; out.filterCutoffBase = in.filterCutoffBase;
    out.highPassFreq = in.highPassFreq; out.highPassRes = in.highPassRes;
    out.overdriveGain = in.overdriveGain; out.overdriveDrive = in.overdriveDrive;
    out.defaultAttack = in.defaultAttack; out.defaultDecay = in.defaultDecay;
    out.defaultSustain = in.defaultSustain; out.defaultRelease = in.defaultRelease;
    out.outputLevel = in.outputLevel;
    out.oscillatorCount = in.oscillatorCount;
    out.engine = in.engine; out.paramSet = in.paramSet;
    out.filterType = in.filterType; out.filterMode = static_cast<VoiceFilterMode>(in.filterMode);
    out.usePatchBases = in.flags & kUsePatchBases;
    out.baseGate = in.flags & kBaseGate;
    out.baseSlide = in.flags & kBaseSlide;
    out.recipeRetrigger = in.flags & kRecipeRetrigger;
    out.hasOverdrive = in.flags & kHasOverdrive;
    out.hasEnvelope = in.flags & kHasEnvelope;
    out.hasFilter = in.flags & kHasFilter;
    out.enabled = in.flags & kEnabled;

    if (out.engine != presetEngine || out.paramSet != presetParamSet)
        out.parameters = nullptr; // layout() derives from paramSet instead
    else
        out.parameters = presetParameters;

    if (out.engine == ENGINE_RECIPE && out.recipe == nullptr)
    {
        out = VoicePresets::getPresetConfig(presetIndex); // safe fallback: factory preset
        return false;
    }
    return true;
}

} // namespace voicecodec
```

Convention: **`capturePatch` never writes `presetIndex`** (a `VoiceConfig` doesn't know which factory preset it came from) — **the caller stamps it**, right after calling `capturePatch` (Task 6's `captureSession` does `out.patches[v].presetIndex = uiState.voicePresetIndices[v];`). The Step 1 test mirrors that:

```cpp
    snap.presetIndex = 4; // stamped by the caller (Session), not by capturePatch
```

Register `PatchCodec.cpp` in the `pico2seq_tests` CMake list.

- [ ] **Step 4: Run tests — pass** (11 `[persistence]` cases)

- [ ] **Step 5: Full suite + commit**

```bash
ctest --test-dir build_test --output-on-failure
git add src/voice/PatchCodec.h src/voice/PatchCodec.cpp tests/unit/test_persistence.cpp tests/CMakeLists.txt
git commit -m "feat(persistence): VoiceConfig patch codec with descriptor re-derivation"
```

---

### Task 5: Golden full-project round-trip test

**Files:**
- Modify: `tests/unit/test_persistence.cpp`

This locks the whole pipeline (patterns + patches + settings in one `ProjectSnapshotV1`) exactly as the spec demands ("Golden round-trip tests: mutate → serialize → deserialize → compare, checksum-corruption rejection" — the frame-level corruption cases are Task 2's; this adds payload-level).

- [ ] **Step 1: Append the golden test**

```cpp
TEST_CASE("golden full-project round-trip through frame bytes", "[persistence]")
{
    Sequencer seqs[4] = {Sequencer(1), Sequencer(2), Sequencer(3), Sequencer(4)};
    ProjectSnapshotV1 snap{};
    for (int v = 0; v < 4; ++v)
    {
        seqs[v].initializeParameters();
        seqs[v].setStepParameterValue(ParamId::Note, v, 3.0f * v + 1.0f);
        seqs[v].setParameterStepCount(ParamId::Gate, 12 + v);
        capturePattern(seqs[v], snap.patterns[v]);
        snap.settings.presetIndices[v] = static_cast<uint8_t>(v);
    }
    snap.settings.tempoBpm = 137.0f;
    snap.settings.masterVolume = 0.66f;
    snap.settings.currentScale = 7;
    snap.settings.shuffleIndex = 3;
    snap.settings.themeIndex = 4;
    snap.settings.selectedVoice = 2;
    snap.settings.encoderBases[1].filter = 0.25f;
    snap.settings.editorCursor[0] = 11; // VoiceEdit::Id::T60
    snap.settings.changedFlags = 0x05;
    REQUIRE(validateProjectSnapshot(snap));

    // Frame to bytes and back, like the flash file does.
    uint8_t frame[12 + sizeof(ProjectSnapshotV1)];
    writeFrameHeader(frame, sizeof(snap), crc32(reinterpret_cast<const uint8_t *>(&snap), sizeof(snap)));
    std::memcpy(frame + 12, &snap, sizeof(snap));
    REQUIRE(readFrameHeader(frame, sizeof(frame), sizeof(ProjectSnapshotV1)) == FrameStatus::Ok);

    ProjectSnapshotV1 loaded{};
    std::memcpy(&loaded, frame + 12, sizeof(loaded));
    Sequencer restoredSeqs[4] = {Sequencer(1), Sequencer(2), Sequencer(3), Sequencer(4)};
    for (int v = 0; v < 4; ++v)
    {
        restoredSeqs[v].initializeParameters();
        applyPattern(loaded.patterns[v], restoredSeqs[v]);
        REQUIRE(restoredSeqs[v].getStepParameterValue(ParamId::Note, v) == 3.0f * v + 1.0f);
        REQUIRE(restoredSeqs[v].getParameterStepCount(ParamId::Gate) == 12u + v);
    }
    REQUIRE(loaded.settings.tempoBpm == 137.0f);
    REQUIRE(loaded.settings.encoderBases[1].filter == 0.25f);
    REQUIRE(loaded.settings.changedFlags == 0x05);
}
```

Note: `Sequencer` may not be default-constructible/movable in an array like this — if the real ctor signature differs, allocate four locals `seq1..seq4` style as `test_helpers.cpp` does for the firmware globals (check `tests/unit/test_sequencer.cpp` for the established construction pattern and copy it).

- [ ] **Step 2: Run — pass. Full suite + commit**

```bash
ctest --test-dir build_test --output-on-failure
git add tests/unit/test_persistence.cpp
git commit -m "test(persistence): golden full-project round-trip"
```

---

### Task 6: Session orchestration (capture/apply) + EncoderManager accessors

**Files:**
- Create: `src/app/Session.h`
- Create: `src/app/Session.cpp`
- Modify: `src/sensors/EncoderManager.h` (2 accessors)
- Modify: `src/sensors/EncoderManager.cpp` (implement)

**Interfaces:**
- Consumes: Tasks 2–4 codecs; `AppState.h` globals (`uiState`, `seq1..4`, `voiceManager`, `currentScale`, `isClockRunning`); `EncoderManager`; `ClockService`; `VoiceSetup::applyVoicePreset` pattern (`src/app/VoiceSetup.cpp:38-70`); `ButtonHandlers.cpp:248-261` shuffle-apply lines; `LEDMatrixFeedback::setLEDTheme`.
- Produces (Tasks 7–10):
  ```cpp
  // src/app/Session.h
  namespace Session
  {
  enum class Source { Defaults, Flash, RetainedRam };
  void captureSession(persistence::ProjectSnapshotV1 &out);       // Core 0 only
  void applyBeforeVoices(const persistence::ProjectSnapshotV1 &s); // preset indices only
  void applyAfterVoices(const persistence::ProjectSnapshotV1 &s);  // patterns, patches, uiState, encoder bases, volume
  void applyAfterClock(const persistence::ProjectSnapshotV1 &s);   // tempo, shuffle, theme
  }
  // EncoderManager.h:
  EncoderBaseValues getEncoderBaseValues(uint8_t voiceIndex);
  void setEncoderBaseValues(uint8_t voiceIndex, const EncoderBaseValues &values);
  ```

Hardware-bound by design (touches `Arduino.h`-adjacent globals); no new host tests — the codec logic underneath is already covered. Keep `Session.cpp` dumb plumbing only: if a function here grows logic, move that logic into the portable layer and test it there.

- [ ] **Step 1: Add the EncoderManager accessors**

`EncoderManager.h` (public, near `initEncoderBaseValues` at `:211`):
```cpp
// Persistence access to the per-voice recorded modifiers.
EncoderBaseValues getEncoderBaseValues(uint8_t voiceIndex);
void setEncoderBaseValues(uint8_t voiceIndex, const EncoderBaseValues &values);
```
`EncoderManager.cpp` (the array lives at `:43`; the file already has a clamped `baseValuesForVoice` helper at `:49-53`):
```cpp
EncoderBaseValues getEncoderBaseValues(uint8_t voiceIndex)
{
  return baseValuesForVoice(voiceIndex);
}

void setEncoderBaseValues(uint8_t voiceIndex, const EncoderBaseValues &values)
{
  baseValuesForVoice(voiceIndex) = values;
}
```

- [ ] **Step 2: Implement `Session`**

`src/app/Session.h`:
```cpp
#ifndef PICO2SEQ_SESSION_H
#define PICO2SEQ_SESSION_H

#include "../pico2seq-core/persistence/ProjectSnapshot.h"

namespace Session
{
enum class Source { Defaults, Flash, RetainedRam };

void captureSession(persistence::ProjectSnapshotV1 &out);
void applyBeforeVoices(const persistence::ProjectSnapshotV1 &s);
void applyAfterVoices(const persistence::ProjectSnapshotV1 &s);
void applyAfterClock(const persistence::ProjectSnapshotV1 &s);
} // namespace Session

#endif
```

`src/app/Session.cpp`:
```cpp
#include "Session.h"
#include "AppState.h"
#include "ClockService.h"
#include "../sensors/EncoderManager.h"
#include "../ui/ButtonHandlers.h"
#include "../ui/UIConstants.h"
#include "../ui/UIState.h"
#include "../voice/PatchCodec.h"
#include "../voice/VoiceEditParameters.h"
#include "../LEDMatrix/LEDMatrixFeedback.h"
#include "../pico2seq-core/persistence/PatternCodec.h"
#include <Arduino.h>
#include <uClock.h>

namespace
{
void captureOneEncoderBase(const EncoderBaseValues &in,
                           persistence::EncoderBaseSnapshot &out)
{
    out.note = in.note; out.velocity = in.velocity; out.filter = in.filter;
    out.attack = in.attack; out.decay = in.decay; out.octave = in.octave;
    out.slideTime = in.slideTime;
}

void applyOneEncoderBase(const persistence::EncoderBaseSnapshot &in, EncoderBaseValues &out)
{
    out.note = in.note; out.velocity = in.velocity; out.filter = in.filter;
    out.attack = in.attack; out.decay = in.decay; out.octave = in.octave;
    out.slideTime = in.slideTime;
}
} // namespace

void Session::captureSession(persistence::ProjectSnapshotV1 &out)
{
    for (uint8_t v = 0; v < VoiceSystem::MAX_VOICES; ++v)
    {
        persistence::capturePattern(*AppState::sequencers[v], out.patterns[v]);
        const VoiceConfig *config =
            voiceManager ? voiceManager->getVoiceConfig(voiceSystem.getVoiceId(v)) : nullptr;
        if (config)
            voicecodec::capturePatch(*config, out.patches[v]);
        out.patches[v].presetIndex = uiState.voicePresetIndices[v];

        const EncoderBaseValues base = getEncoderBaseValues(v);
        captureOneEncoderBase(base, out.settings.encoderBases[v]);
        out.settings.editorCursor[v] = static_cast<uint8_t>(uiState.voiceEditor.cursor[v]);
        if (uiState.voiceEditor.changed[v])
            out.settings.changedFlags |= static_cast<uint8_t>(1u << v);
        else
            out.settings.changedFlags &= static_cast<uint8_t>~(1u << v);
    }
    out.settings.tempoBpm = uClock.getTempo();
    out.settings.masterVolume = voiceManager ? voiceManager->getGlobalVolume() : 0.8f;
    out.settings.themeIndex = uiState.currentThemeIndex;
    out.settings.currentScale = currentScale;
    out.settings.shuffleIndex = uiState.currentShufflePatternIndex;
    out.settings.selectedVoice = uiState.selectedVoiceIndex;
    if (uiState.slideMode)
        out.settings.changedFlags |= 0x10u;
    else
        out.settings.changedFlags &= static_cast<uint8_t>(~0x10u);
}

void Session::applyBeforeVoices(const persistence::ProjectSnapshotV1 &s)
{
    // initializeVoices() (VoiceSetup.cpp:16) reads voicePresetIndices to build
    // the factory voices; everything else applies after those voices exist.
    const uint8_t presetCount = VoicePresets::getPresetCount();
    for (uint8_t v = 0; v < VoiceSystem::MAX_VOICES; ++v)
    {
        uiState.voicePresetIndices[v] = (s.settings.presetIndices[v] < presetCount)
                                            ? s.settings.presetIndices[v]
                                            : uiState.voicePresetIndices[v];
    }
}

void Session::applyAfterVoices(const persistence::ProjectSnapshotV1 &s)
{
    for (uint8_t v = 0; v < VoiceSystem::MAX_VOICES; ++v)
    {
        persistence::applyPattern(s.patterns[v], *AppState::sequencers[v]);

        VoiceConfig config;
        if (voicecodec::applyPatch(uiState.voicePresetIndices[v], s.patches[v], config))
        {
            VoiceEdit::enablePatch(config);
            const uint8_t voiceId = voiceSystem.getVoiceId(v);
            voiceManager->setVoiceConfig(voiceId, config);
            voiceManager->setVoiceSlide(voiceId, config.slideSeconds);
        }
        EncoderBaseValues base;
        applyOneEncoderBase(s.settings.encoderBases[v], base);
        setEncoderBaseValues(v, base);
        uiState.voiceEditor.cursor[v] = static_cast<VoiceEdit::Id>(s.settings.editorCursor[v]);
        uiState.voiceEditor.changed[v] = (s.settings.changedFlags & (1u << v)) != 0;
    }
    uiState.selectedVoiceIndex = s.settings.selectedVoice;
    uiState.slideMode = (s.settings.changedFlags & 0x10u) != 0;
    if (voiceManager)
        voiceManager->setGlobalVolume(s.settings.masterVolume);
}

void Session::applyAfterClock(const persistence::ProjectSnapshotV1 &s)
{
    uClock.setTempo(s.settings.tempoBpm);
    // Same three lines as BUTTON_CHANGE_SWING_PATTERN (ButtonHandlers.cpp:250-254).
    const ShuffleTemplate &tmpl = shuffleTemplates[s.settings.shuffleIndex];
    uClock.setShuffleTemplate(const_cast<int8_t *>(tmpl.ticks), SHUFFLE_TEMPLATE_SIZE);
    uClock.setShuffle(s.settings.shuffleIndex > 0);
    uiState.currentShufflePatternIndex = s.settings.shuffleIndex;
    currentScale = s.settings.currentScale;
    uiState.currentThemeIndex = s.settings.themeIndex;
    setLEDTheme(static_cast<LEDTheme>(s.settings.themeIndex));
}
```

Step 3 note — includes to resolve while implementing: `../voice/VoicePresets.h` (`getPresetCount`), `shuffleTemplates`/`ShuffleTemplate`/`SHUFFLE_TEMPLATE_SIZE` (from the header `ButtonHandlers.cpp` already includes for the swing handler — find it via the `ButtonHandlers.cpp:251` usage, likely `../ui/ShuffleTemplates.h`), `LEDTheme` (`../LEDMatrix/LEDMatrixFeedback.h`), and `VoiceSystem` via `AppState.h`. Also verify `uiState.voiceEditor.cursor[v]` is the real member name in `src/ui/VoiceEditControls.h:21` (`cursor[4]`) and `changed[4]` (`:20`); and that `VoiceManager::setVoiceSlide` exists (used at `VoiceSetup.cpp:59`).

- [ ] **Step 3: Compile check via the audio/watchdog-style suite**

`Session.cpp` is hardware-bound; it will not compile in the host suite and must NOT be added to it. Verify it compiles only as part of the Arduino sketch at bench time (Task 12). What you can still do on host: `clang++ -fsyntax-only` with the stub include paths if the includes resolve; otherwise rely on review + bench. Do not weaken includes to make a host build pass.

- [ ] **Step 4: Full host suite (unchanged green) + commit**

```bash
ctest --test-dir build_test --output-on-failure
git add src/app/Session.h src/app/Session.cpp src/sensors/EncoderManager.h src/sensors/EncoderManager.cpp
git commit -m "feat(persistence): session capture/apply orchestration on Core 0"
```

---

### Task 7: LittleFS storage adapter (atomic save/load)

**Files:**
- Create: `src/app/SessionStorage.h`
- Create: `src/app/SessionStorage.cpp`

**Interfaces:**
- Consumes: Task 2 frame helpers; `LittleFS.h` (bundled with the core — no library install needed).
- Produces (Tasks 8, 10):
  ```cpp
  // src/app/SessionStorage.h
  #include "../pico2seq-core/persistence/ProjectSnapshot.h"
  #include <cstdint>
  namespace SessionStorage
  {
  // Mount LittleFS. First boot formats the partition (multi-second: must run
  // BEFORE freezeWatchdogArm()). Returns false only if mount+format both fail.
  bool begin();
  enum class LoadResult { Ok, NoFile, IoError, BadFrame };
  LoadResult load(persistence::ProjectSnapshotV1 &out);
  bool save(const persistence::ProjectSnapshotV1 &snap); // tmp + atomic rename
  }
  ```

- [ ] **Step 1: Implement**

`src/app/SessionStorage.cpp`:
```cpp
#include "SessionStorage.h"
#include "../pico2seq-core/persistence/SnapshotFormat.h"
#include <Arduino.h>
#include <LittleFS.h>

namespace
{
constexpr const char *kSavePath = "/session.p2s";
constexpr const char *kTempPath = "/session.tmp";

// 12-byte frame + payload. Static, not stack: ~10.5 KB and this runs on the
// Core 0 Arduino loop stack during saves.
persistence::ProjectSnapshotV1 g_saveBuffer;
persistence::ProjectSnapshotV1 g_loadBuffer;
} // namespace

bool SessionStorage::begin()
{
    LittleFSConfig cfg;
    cfg.setAutoFormat(false); // format explicitly so we can log/measure it
    LittleFS.setConfig(cfg);
    if (LittleFS.begin())
        return true;
    Serial.println("[STORAGE] first boot: formatting LittleFS partition");
    if (!LittleFS.format())
    {
        Serial.println("[STORAGE] format FAILED; persistence disabled this boot");
        return false;
    }
    return LittleFS.begin();
}

SessionStorage::LoadResult SessionStorage::load(persistence::ProjectSnapshotV1 &out)
{
    if (!LittleFS.exists(kSavePath))
        return LoadResult::NoFile;
    File f = LittleFS.open(kSavePath, "r");
    if (!f)
        return LoadResult::IoError;
    uint8_t header[12];
    if (f.read(header, 12) != 12)
    {
        f.close();
        return LoadResult::BadFrame;
    }
    if (f.read(reinterpret_cast<uint8_t *>(&g_loadBuffer), sizeof(g_loadBuffer)) !=
        static_cast<int>(sizeof(g_loadBuffer)))
    {
        f.close();
        return LoadResult::BadFrame;
    }
    f.close();
    using persistence::FrameStatus;
    using persistence::readFrameHeader;
    const FrameStatus status =
        readFrameHeader(header, 12 + sizeof(g_loadBuffer), sizeof(persistence::ProjectSnapshotV1));
    if (status != FrameStatus::Ok)
        return LoadResult::BadFrame;
    if (!persistence::validateProjectSnapshot(g_loadBuffer))
        return LoadResult::BadFrame;
    out = g_loadBuffer;
    return LoadResult::Ok;
}

bool SessionStorage::save(const persistence::ProjectSnapshotV1 &snap)
{
    g_saveBuffer = snap;
    const uint32_t crc = persistence::crc32(
        reinterpret_cast<const uint8_t *>(&g_saveBuffer), sizeof(g_saveBuffer));
    uint8_t header[12];
    persistence::writeFrameHeader(header, sizeof(g_saveBuffer), crc);

    File f = LittleFS.open(kTempPath, "w");
    if (!f)
        return false;
    const bool okWrote = f.write(header, 12) == 12 &&
                         f.write(reinterpret_cast<const uint8_t *>(&g_saveBuffer),
                                 sizeof(g_saveBuffer)) == static_cast<size_t>(sizeof(g_saveBuffer));
    f.close();
    if (!okWrote)
        return false;
    // lfs rename atomically replaces the destination: a power cut mid-write
    // leaves either the old file or the new one, never a torn file.
    return LittleFS.rename(kTempPath, kSavePath);
}
```

Note the RAM cost: two ~10.2 KB static buffers (~20 KB) — build logs show 35–52 KB of 512 KB used, so this fits comfortably. If you prefer one buffer, make `g_loadBuffer`/`g_saveBuffer` a single shared static and route both functions through it (save copies in, load copies out) — allowed because both run only from `Application::update()`.

- [ ] **Step 2: Commit** (host suite untouched — hardware-bound file; full run anyway to prove no accidental breakage)

```bash
ctest --test-dir build_test --output-on-failure
git add src/app/SessionStorage.h src/app/SessionStorage.cpp
git commit -m "feat(persistence): LittleFS session storage with atomic save/load"
```

---

### Task 8: Boot integration — mount, load, apply, notices

**Files:**
- Modify: `src/app/Application.cpp` (`begin()` around lines 58–88)
- Modify: `src/app/Application.h` (if it declares per-module hooks; check current content)
- Modify: `src/utils/FreezeWatchdog.h:55` (append `FW_SETUP_STORAGE` after `FW_LOOP_DIAGNOSTICS`, before `FW_FAULT` — FW_FAULT's explicit value keeps scratch meaning stable)
- Modify: `src/utils/FreezeWatchdog.h` phase-name switch (`:86`) — add `case FW_SETUP_STORAGE: return "setup: session storage";`

**Interfaces:**
- Consumes: `SessionStorage::begin/load` (Task 7), `Session::apply*` (Task 6).
- Produces: `Application::sessionLoadSucceeded()` — `true` when a valid snapshot was applied at boot (used by Task 9's resume path and Task 10's `lastSavedCrc` seed). Expose via a small internal accessor or a file-scope flag polled by Session (decide at implementation: simplest is `namespace Session { extern bool g_bootLoadedOk; }` set in `begin()`; NOT a UIState field — it is not UI state).

- [ ] **Step 1: Rewire `Application::begin()`**

```cpp
void Application::begin()
{
    freezeWatchdogBootCheck();
    delay(kBootStabilizationMs);
    Serial.begin(kSerialBaud);
    recoveryMode = previousFreeze.watchdogReset;
    if (recoveryMode)
    {
        // Leave peripherals and voices untouched so the previous failure
        // cannot reset us again before the USB monitor has time to reconnect.
        watchdog_disable();
        return;
    }
    Serial.print("[CORE0] Setup starting... ");
    Serial.printf("clock=%lu MHz\n", (unsigned long)(F_CPU / 1000000));

    // Session storage mounts BEFORE anything arms the watchdog
    // (ControlIO::beginMainBusAndLeds -> freezeWatchdogArm): a first-boot
    // LittleFS format can take seconds and must not reboot us mid-format.
    freezeWatchdogMark(FW_SETUP_STORAGE); // mark only; watchdog not armed yet
    SessionStorage::begin();
    persistence::ProjectSnapshotV1 snapshot;
    const bool loaded =
        SessionStorage::load(snapshot) == SessionStorage::LoadResult::Ok;
    Session::g_bootLoadedOk = loaded;
    if (loaded)
    {
        Session::applyBeforeVoices(snapshot);
        g_pendingBootSnapshot = snapshot; // file-scope ProjectSnapshotV1; applied after voices exist
        Serial.println("[STORAGE] session loaded");
    }
    else
    {
        Serial.println("[STORAGE] no valid session; factory defaults");
    }

    ControlIO::beginMainBusAndLeds();   // arms the watchdog from here on
    ControlIO::beginPerformanceSensors();
    ControlIO::beginTouchPads();
    ControlIO::beginDisplay();
    freezeWatchdogFeed(FW_SETUP_VOICES);
    initializeVoices();                 // consumes uiState.voicePresetIndices
    if (loaded)
        Session::applyAfterVoices(g_pendingBootSnapshot);
    ControlIO::observeVoiceChanges();
    ControlIO::beginMatrixAndTiles();

    freezeWatchdogFeed(FW_SETUP_UCLOCK);
    initializeClock();
    if (loaded)
        Session::applyAfterClock(g_pendingBootSnapshot);
    Serial.println("[CORE0] Setup complete!");
    voicesReady.store(true, std::memory_order_release);
}
```

`g_pendingBootSnapshot` is a file-scope `persistence::ProjectSnapshotV1` in `Application.cpp` (another ~10.4 KB static — same budget note as Task 7). Include `SessionStorage.h`, `Session.h`, `../pico2seq-core/persistence/ProjectSnapshot.h`.

Wait — `FW_SETUP_STORAGE` mark before the watchdog is armed is breadcrumb-only (`freezeWatchdogMark` docs at `FreezeWatchdog.h:92` say exactly that). Correct as written.

- [ ] **Step 2: Watchdog phase enum append**

In `FreezeWatchdog.h`, after `FW_LOOP_DIAGNOSTICS` (line 55) add `FW_SETUP_STORAGE,` and in `freezeWatchdogPhaseName` add the matching case. Do not renumber anything (append-only rule).

- [ ] **Step 3: Host suite + commit**

```bash
ctest --test-dir build_test --output-on-failure
git add src/app/Application.cpp src/app/Application.h src/utils/FreezeWatchdog.h
git commit -m "feat(persistence): mount storage pre-watchdog, load session at boot"
```

(The watchdog suite must stay green — it asserts specific phase IDs; appending does not shift existing values. `tests/unit/test_freeze_watchdog.cpp:75-76` pins `FW_LOOP_CONTROL==14`/`FW_LOOP_DISPLAY==15` — confirm the append doesn't move those; it appends at the end so it cannot.)

---

### Task 9: Retained-RAM mirror + watchdog session resume

**Files:**
- Create: `src/pico2seq-core/persistence/RetainedSessionLogic.h` (pure, host-tested)
- Create: `src/app/RetainedSession.h`
- Create: `src/app/RetainedSession.cpp`
- Modify: `tests/unit/test_persistence.cpp`
- Modify: `tests/CMakeLists.txt` (only if a new .cpp is added — RetainedSessionLogic is header-only pure, so likely no CMake change)
- Modify: `src/app/Application.cpp` (recovery branch + 1 Hz refresh + attempts clear)

**Interfaces:**
- Consumes: Task 2 (`crc32`, `ProjectSnapshotV1`), Task 6 (`Session::captureSession`), `FreezeWatchdogReport previousFreeze`.
- Produces:
  ```cpp
  // RetainedSessionLogic.h (portable)
  namespace persistence {
  struct RetainedHeader {
      uint32_t magic;      // RETAINED_MAGIC = 0x52455431 ('RET1')
      uint16_t version;    // RETAINED_VERSION = 1
      uint16_t flags;      // bit0 = bootCompleted
      uint32_t generation; // ++ on every refresh
      uint8_t resumeAttempts; // consecutive watchdog resume attempts
      uint8_t reserved[3];
  };
  struct RetainedStore {
      RetainedHeader header;
      ProjectSnapshotV1 snapshot; // crc covers exactly these bytes
      uint32_t crc32;
  };
  constexpr uint32_t RETAINED_MAGIC = 0x52455431u;
  constexpr uint16_t RETAINED_VERSION = 1;
  constexpr uint8_t MAX_RESUME_ATTEMPTS = 3;
  bool retainedValid(const RetainedStore &store) noexcept;           // magic+version+crc
  enum class ResumeDecision { HaltRecovery, ResumeRetained, NormalBoot };
  ResumeDecision decideResume(bool watchdogReset, bool retainedValidFlag,
                              uint8_t resumeAttempts) noexcept;
  void retainedRefresh(RetainedStore &store, const ProjectSnapshotV1 &snap) noexcept;
  }
  // src/app/RetainedSession.h (hardware side)
  namespace RetainedSession {
  void bootInit();      // reads the store, caches validity, bumps nothing
  bool resumeAllowed(); // valid && attempts < MAX (call once; bumps attempts)
  void refresh(const persistence::ProjectSnapshotV1 &snap); // 1 Hz from update()
  void markBootCompleted(); // flags |= bootCompleted; resumeAttempts = 0
  bool takeResumeSnapshot(persistence::ProjectSnapshotV1 &out); // valid store -> out
  }
  ```

Semantics of `decideResume` (host-tested):
- `!watchdogReset` → `NormalBoot` (power-on/USB-upload: load from flash if present; retained store still gets refreshed for next time).
- `watchdogReset && retainedValid && attempts < MAX_RESUME_ATTEMPTS` → `ResumeRetained`.
- `watchdogReset` otherwise → `HaltRecovery` (today's halt behavior — e.g., the deliberate `while(1)` on missing MPR121 hardware, `ControlIO.cpp:109-111`, would otherwise reboot-loop forever; 3 strikes then halt).

- [ ] **Step 1: Write the failing decision tests** (append to `test_persistence.cpp`)

```cpp
#include "persistence/RetainedSessionLogic.h"

TEST_CASE("resume decision table", "[persistence]")
{
    using persistence::ResumeDecision;
    REQUIRE(persistence::decideResume(false, false, 0) == ResumeDecision::NormalBoot);
    REQUIRE(persistence::decideResume(false, true, 2) == ResumeDecision::NormalBoot);
    REQUIRE(persistence::decideResume(true, false, 0) == ResumeDecision::HaltRecovery);
    REQUIRE(persistence::decideResume(true, true, 0) == ResumeDecision::ResumeRetained);
    REQUIRE(persistence::decideResume(true, true, 2) == ResumeDecision::ResumeRetained);
    REQUIRE(persistence::decideResume(true, true, 3) == ResumeDecision::HaltRecovery);
}

TEST_CASE("retained validity and refresh", "[persistence]")
{
    persistence::RetainedStore store{};
    REQUIRE_FALSE(persistence::retainedValid(store)); // zeroed = no magic
    persistence::ProjectSnapshotV1 snap{};
    snap.settings.tempoBpm = 111.0f;
    persistence::retainedRefresh(store, snap);
    REQUIRE(persistence::retainedValid(store));
    REQUIRE(store.header.generation == 1);
    persistence::retainedRefresh(store, snap);
    REQUIRE(store.header.generation == 2);
    // CRC damage invalidates.
    store.snapshot.settings.tempoBpm = 222.0f; // edited without refresh
    REQUIRE_FALSE(persistence::retainedValid(store));
    // Wrong version invalidates.
    persistence::retainedRefresh(store, snap);
    REQUIRE(persistence::retainedValid(store));
    store.header.version = 99;
    REQUIRE_FALSE(persistence::retainedValid(store));
}
```

- [ ] **Step 2: Run to verify failure** — header missing.

- [ ] **Step 3: Implement `RetainedSessionLogic.h`** (header-only, pure):

```cpp
#ifndef PICO2SEQ_RETAINED_SESSION_LOGIC_H
#define PICO2SEQ_RETAINED_SESSION_LOGIC_H

#include "ProjectSnapshot.h"
#include "SnapshotFormat.h"
#include <cstring>

namespace persistence
{

constexpr uint32_t RETAINED_MAGIC = 0x52455431u; // 'RET1'
constexpr uint16_t RETAINED_VERSION = 1;
constexpr uint8_t MAX_RESUME_ATTEMPTS = 3;
constexpr uint16_t RETAINED_FLAG_BOOT_COMPLETED = 1u << 0;

struct RetainedHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t generation;
    uint8_t resumeAttempts;
    uint8_t reserved[3];
};

struct RetainedStore
{
    RetainedHeader header;
    ProjectSnapshotV1 snapshot;
    uint32_t crc32; // over snapshot only
};

inline bool retainedValid(const RetainedStore &store) noexcept
{
    if (store.header.magic != RETAINED_MAGIC || store.header.version != RETAINED_VERSION)
        return false;
    return crc32(reinterpret_cast<const uint8_t *>(&store.snapshot), sizeof(store.snapshot)) ==
           store.crc32;
}

enum class ResumeDecision { HaltRecovery, ResumeRetained, NormalBoot };

inline ResumeDecision decideResume(bool watchdogReset, bool retainedValidFlag,
                                   uint8_t resumeAttempts) noexcept
{
    if (!watchdogReset)
        return ResumeDecision::NormalBoot;
    if (retainedValidFlag && resumeAttempts < MAX_RESUME_ATTEMPTS)
        return ResumeDecision::ResumeRetained;
    return ResumeDecision::HaltRecovery;
}

inline void retainedRefresh(RetainedStore &store, const ProjectSnapshotV1 &snap) noexcept
{
    store.header.magic = RETAINED_MAGIC;
    store.header.version = RETAINED_VERSION;
    store.header.generation += 1u;
    store.snapshot = snap;
    store.crc32 = crc32(reinterpret_cast<const uint8_t *>(&store.snapshot), sizeof(store.snapshot));
}

} // namespace persistence

#endif
```

- [ ] **Step 4: Implement the hardware side** (`src/app/RetainedSession.h/.cpp`):

```cpp
// src/app/RetainedSession.h
#ifndef PICO2SEQ_RETAINED_SESSION_H
#define PICO2SEQ_RETAINED_SESSION_H

#include "../pico2seq-core/persistence/RetainedSessionLogic.h"

namespace RetainedSession
{
void bootInit();
bool resumeAllowed();
void refresh(const persistence::ProjectSnapshotV1 &snap);
void markBootCompleted();
bool takeResumeSnapshot(persistence::ProjectSnapshotV1 &out);
} // namespace RetainedSession

#endif
```

```cpp
// src/app/RetainedSession.cpp
#include "RetainedSession.h"
#include <Arduino.h>

#if defined(ARDUINO)
#include "pico/platform.h"
// NOLOAD section: survives watchdog/warm resets (RAM stays powered), is NOT
// cleared or initialized by crt0, loses content on power-on (validated by
// magic+CRC). The store has no constructor on purpose.
__uninitialized_ram(static persistence::RetainedStore s_store);
#else
static persistence::RetainedStore s_store; // host fallback: ordinary zeroed RAM
#endif

namespace
{
bool s_valid = false;
bool s_consumedResume = false;
} // namespace

void RetainedSession::bootInit()
{
    s_valid = persistence::retainedValid(s_store);
    s_consumedResume = false;
}

bool RetainedSession::resumeAllowed()
{
    if (s_consumedResume)
        return false; // one decision per boot
    s_consumedResume = true;
    if (!s_valid)
        return false;
    if (persistence::decideResume(true, true, s_store.header.resumeAttempts) !=
        persistence::ResumeDecision::ResumeRetained)
        return false;
    s_store.header.resumeAttempts += 1; // retained: escalates across rapid re-freezes
    return true;
}

void RetainedSession::refresh(const persistence::ProjectSnapshotV1 &snap)
{
    const uint8_t attempts = s_store.header.resumeAttempts;
    persistence::retainedRefresh(s_store, snap);
    s_store.header.resumeAttempts = attempts; // refresh never resets the counter
    s_valid = true;
}

void RetainedSession::markBootCompleted()
{
    s_store.header.flags |= persistence::RETAINED_FLAG_BOOT_COMPLETED;
    s_store.header.resumeAttempts = 0; // a boot that reached loop() is a good boot
}

bool RetainedSession::takeResumeSnapshot(persistence::ProjectSnapshotV1 &out)
{
    if (!persistence::retainedValid(s_store))
        return false;
    out = s_store.snapshot;
    return true;
}
```

`markBootCompleted()` intentionally does not rewrite the CRC (the counter/flags live in the header, which the CRC does not cover) — that is why the CRC covers `snapshot` only.

- [ ] **Step 5: Rewire `Application::begin()` recovery + refresh in `update()`**

In `begin()`, after `Serial.begin(...)`, replace the recovery gate with:

```cpp
    RetainedSession::bootInit();
    bool resumeFromRetained = false;
    if (previousFreeze.watchdogReset)
    {
        if (RetainedSession::resumeAllowed())
        {
            resumeFromRetained = true;
            Serial.println("[RECOVERY] watchdog reset; resuming live session from retained RAM");
        }
        else
        {
            // Fall back to today's behavior: park so the failure cannot repeat
            // before the USB monitor reconnects. The flash-saved session is
            // still there; a power cycle restores it at boot.
            watchdog_disable();
            Serial.println("[RECOVERY] session resume unavailable/exhausted. Power-cycle to retry.");
            return;
        }
    }
```

Then storage/load becomes: `const bool loaded = resumeFromRetained ? (RetainedSession::takeResumeSnapshot(snapshot)) : (SessionStorage::load(snapshot) == SessionStorage::LoadResult::Ok);` — and when `resumeFromRetained`, also print the freeze post-mortem once (`freezeWatchdogPrintPreviousRun()` at the top of the resume path — Serial was just begun; the periodic diagnostics keep re-printing it every 2 s anyway via `printRuntimeDiagnostics`, `Application.cpp:26`). `Session::g_bootLoadedOk` is set from `loaded` either way.

At the end of `begin()` (right before `voicesReady.store`): `RetainedSession::markBootCompleted();` — and refresh the mirror once so a freeze 100 ms into loop() still finds a fresh session: `persistence::ProjectSnapshotV1 snap; Session::captureSession(snap); RetainedSession::refresh(snap);`.

In `update()`, after `voiceManager->flushControlUpdates()` (line ~105), add the 1 Hz mirror refresh:

```cpp
    static uint32_t lastRetainedRefreshMs = 0;
    if (nowMs - lastRetainedRefreshMs >= 1000)
    {
        lastRetainedRefreshMs = nowMs;
        persistence::ProjectSnapshotV1 snap;
        Session::captureSession(snap);
        RetainedSession::refresh(snap);
    }
```

(`nowMs` already exists at `Application.cpp:107` — move its declaration above this block if needed.) ~10.4 KB memcpy once a second on Core 0 is negligible (~tens of µs); zero flash wear.

- [ ] **Step 6: Run host tests — pass; full suite green**

```bash
cmake --build build_test --parallel && ./build_test/tests/pico2seq_tests "[persistence]"
ctest --test-dir build_test --output-on-failure
```

- [ ] **Step 7: Commit**

```bash
git add src/pico2seq-core/persistence/RetainedSessionLogic.h src/app/RetainedSession.h src/app/RetainedSession.cpp src/app/Application.cpp tests/unit/test_persistence.cpp
git commit -m "feat(persistence): retained-RAM session mirror and watchdog resume"
```

---

### Task 10: Save/load UI gesture (Utility button 1), autosave-on-stop, OLED notices

**Files:**
- Modify: `src/ui/UIState.h:37` (extend `OledNoticeKind`)
- Modify: `src/OLED/oled.cpp` (notice render block, ~`:293-307`)
- Modify: `src/ui/AlchemyControlBridge.cpp` (`handleUtilityButtons`, `case 1:` free — loop `:258` covers bits 0–6, bit 7 is Shift; `case 1` currently falls to `default`)
- Modify: `src/ui/AlchemyControlBridge.h` (one latch member)
- Modify: `src/app/Application.cpp` (`update()`: deferred save/load executor + stop-edge autosave)
- Modify: `src/app/Session.h/.cpp` (request queue + `lastSavedCrc`)

**Interfaces:**
- Consumes: Tasks 6–9. Button vocabulary: `TileButton::heldMilliseconds` + `UITimingConstants::LONG_PRESS_THRESHOLD_MS` (400 ms, `src/ui/ButtonManager.h:18`) exactly as `case 0:` uses them (`AlchemyControlBridge.cpp:276-277`).
- Produces:
  ```cpp
  // Session.h additions
  namespace Session {
  enum class PendingAction { None, Save, Load };
  void requestSave();   // callable from any Core-0 UI handler; just sets a flag
  void requestLoad();
  PendingAction consumePendingAction(); // Application::update() polls this
  uint32_t lastSavedCrc();              // 0 when nothing saved this boot chain
  }
  ```
- UI contract (fits the existing Utility map, one free slot — the delay toggle was removed 2026-09-11, leaving bit 1 unassigned):
  - **Utility button 1, tap** → save now. If the transport is running it stops for the write and restarts after (a ~0.1–0.5 s gap; sector erase stalls both cores).
  - **Utility button 1, long-press (≥400 ms)** → revert to the last saved session (load). Transport stops and stays stopped.
  - Feedback: `OledNoticeKind::Saved` / `Loaded` / `LoadError` (800 ms, `OLED_NOTICE_DURATION_MS`, `src/ui/UIConstants.h:42`).
  - **Autosave:** when the transport transitions running→stopped (user Stop), after a 1 s debounce, if the current snapshot's CRC differs from `lastSavedCrc()`, save silently (no notice).
- Wear: saves happen at most once per transport stop or explicit tap. LittleFS spreads erases over the 64 KB partition (16 blocks, `block_cycles=16`); at even 100 saves/day the flash outlives the product by orders of magnitude.

- [ ] **Step 1: Extend the notice enum**

`src/ui/UIState.h:37`:
```cpp
    enum class OledNoticeKind : uint8_t { None = 0, Randomized = 1, Saved = 2, Loaded = 3, LoadError = 4 };
```

- [ ] **Step 2: Render the new notices**

In `src/OLED/oled.cpp`, find the block that renders `Randomized` (~lines 293–307; it switches on `uiState.oledNoticeKind` while `millis() < oledNoticeUntil`). Add branches mirroring the existing style:
- `Saved` → big text "SAVED", no voice line.
- `Loaded` → "LOADED".
- `LoadError` → "LOAD ERR".
Copy the exact font/position calls from the Randomized branch; only the strings differ. (OLED is hardware-bound — no host test; keep the change mechanical.)

- [ ] **Step 3: Session request queue**

`Session.h` add:
```cpp
#include <cstdint>
enum class PendingAction { None, Save, Load };
void requestSave();
void requestLoad();
PendingAction consumePendingAction();
uint32_t lastSavedCrc();
```
`Session.cpp` add:
```cpp
namespace
{
Session::PendingAction g_pending = Session::PendingAction::None; // Core-0 single-writer flags
uint32_t g_lastSavedCrc = 0;
Session::PendingAction g_consumed = Session::PendingAction::None; // unused; see consume
} // namespace

void Session::requestSave() { g_pending = PendingAction::Save; }
void Session::requestLoad() { g_pending = PendingAction::Load; }
Session::PendingAction Session::consumePendingAction()
{
    const PendingAction action = g_pending;
    g_pending = PendingAction::None;
    return action;
}
uint32_t Session::lastSavedCrc() { return g_lastSavedCrc; }
```
(Drop `g_consumed` — it exists only in this snippet by mistake; ship without it.) Also add `namespace Session { void setLastSavedCrc(uint32_t); }` used by Application after a successful save/load, and seed it at boot in Task 8's load path: after a successful `load`, compute `crc32` over the loaded snapshot and call `setLastSavedCrc`.

- [ ] **Step 4: The executor in `Application::update()`**

Insert after the retained-refresh block from Task 9 (still near the top of `update()`, before `pollHeldButtons`):

```cpp
    // Deferred flash I/O: requested by UI handlers, executed here — never in
    // ISR/uClock callback context. A save with the transport running stops
    // the clock for the erase window and restarts it after.
    const Session::PendingAction action = Session::consumePendingAction();
    if (action != Session::PendingAction::None)
    {
        const bool wasRunning = isClockRunning;
        if (wasRunning)
            stopClockForEditor(); // also drains pending steps (ClockService.cpp:136-145)
        voiceManager->flushControlUpdates();

        persistence::ProjectSnapshotV1 snap;
        Session::captureSession(snap);
        const uint32_t crc = persistence::crc32(
            reinterpret_cast<const uint8_t *>(&snap), sizeof(snap));

        if (action == Session::PendingAction::Save)
        {
            if (SessionStorage::save(snap))
            {
                Session::setLastSavedCrc(crc);
                uiState.oledNoticeKind = UIState::OledNoticeKind::Saved;
                uiState.oledNoticeUntil = millis() + OLED_NOTICE_DURATION_MS;
                Serial.println("[STORAGE] saved");
            }
            else
            {
                uiState.oledNoticeKind = UIState::OledNoticeKind::LoadError;
                uiState.oledNoticeUntil = millis() + OLED_NOTICE_DURATION_MS;
                Serial.println("[STORAGE] save FAILED");
            }
            if (wasRunning)
                uClock.start(); // onClockStart restarts all four sequencers (step 0)
        }
        else // Load
        {
            persistence::ProjectSnapshotV1 loaded{};
            if (SessionStorage::load(loaded) == SessionStorage::LoadResult::Ok)
            {
                Session::applyBeforeVoices(loaded);
                Session::applyAfterVoices(loaded);
                Session::applyAfterClock(loaded);
                Session::setLastSavedCrc(persistence::crc32(
                    reinterpret_cast<const uint8_t *>(&loaded), sizeof(loaded)));
                uiState.oledNoticeKind = UIState::OledNoticeKind::Loaded;
                uiState.oledNoticeUntil = millis() + OLED_NOTICE_DURATION_MS;
                Serial.println("[STORAGE] loaded");
            }
            else
            {
                uiState.oledNoticeKind = UIState::OledNoticeKind::LoadError;
                uiState.oledNoticeUntil = millis() + OLED_NOTICE_DURATION_MS;
                Serial.println("[STORAGE] load FAILED");
            }
        }
        freezeWatchdogFeed(FW_LOOP_USB_READ); // long flash op: re-arm the 2 s budget
    }

    // Autosave on transport stop (debounced). isClockRunning flips inside the
    // uClock callbacks (ISR-adjacent); polling the edge here stays safe.
    static bool wasClockRunningForAutosave = false;
    static uint32_t stopEdgeMs = 0;
    if (wasClockRunningForAutosave && !isClockRunning)
        stopEdgeMs = nowMs;
    wasClockRunningForAutosave = isClockRunning;
    static uint32_t lastAutosaveCheckMs = 0;
    if (stopEdgeMs != 0 && !isClockRunning && nowMs - stopEdgeMs >= 1000)
    {
        stopEdgeMs = 0;
        persistence::ProjectSnapshotV1 snap;
        Session::captureSession(snap);
        const uint32_t crc = persistence::crc32(
            reinterpret_cast<const uint8_t *>(&snap), sizeof(snap));
        if (crc != Session::lastSavedCrc())
        {
            if (SessionStorage::save(snap))
            {
                Session::setLastSavedCrc(crc);
                Serial.println("[STORAGE] autosaved on stop");
            }
        }
    }
```

Note on runtime load: `applyBeforeVoices` outside boot is safe — it only stamps `uiState.voicePresetIndices`; the heavy path is `applyAfterVoices`' `setVoiceConfig`, which stages through the normal per-voice control queue (same path `applyVoicePreset` uses at `VoiceSetup.cpp:56`, click-safe while stopped). `applyAfterClock` re-applies tempo/shuffle/theme immediately. New includes for `Application.cpp`: `../pico2seq-core/persistence/SnapshotFormat.h`, `../ui/UIConstants.h`, `<uClock.h>` (for `uClock.start()`), and `SessionStorage.h`.

Boot notice for the Task 8 load path: in the first `update()` tick, if `Session::g_bootLoadedOk`, show the `Loaded` notice once (file-scope `bool bootNoticeShown`):
```cpp
    static bool bootNoticeShown = false;
    if (!bootNoticeShown)
    {
        bootNoticeShown = true;
        if (Session::g_bootLoadedOk)
        {
            uiState.oledNoticeKind = UIState::OledNoticeKind::Loaded;
            uiState.oledNoticeUntil = millis() + OLED_NOTICE_DURATION_MS;
        }
    }
```

- [ ] **Step 5: The button handler**

`AlchemyControlBridge.h`: add member `bool saveLoadLatch_ = false;` next to `playSettingsOpenedThisPress_`.
`AlchemyControlBridge.cpp`, inside the `for` loop of `handleUtilityButtons` (between `case 0:` and `case 2:` — `case 1:` is the free slot):

```cpp
    case 1: // Session save (tap) / load last saved (long-press)
      if (edges.pressEdge)
      {
        saveLoadLatch_ = false;
      }
      else if (tileButton.held() && !saveLoadLatch_ &&
               tileButton.heldMilliseconds(nowMs) >= UITimingConstants::LONG_PRESS_THRESHOLD_MS)
      {
        saveLoadLatch_ = true; // consume the hold; release must not re-trigger
        Session::requestLoad();
      }
      else if (edges.releaseEdge && !saveLoadLatch_)
      {
        Session::requestSave();
      }
      break;
```

Add `#include "../app/Session.h"` to `AlchemyControlBridge.cpp` (precedent: it already includes `../app/VoiceEditor.h`). The deferred executor guarantees the actual flash op never runs from this input-scan context.

- [ ] **Step 6: Host suite + commit**

```bash
ctest --test-dir build_test --output-on-failure
git add src/ui/UIState.h src/OLED/oled.cpp src/ui/AlchemyControlBridge.cpp src/ui/AlchemyControlBridge.h src/app/Application.cpp src/app/Session.h src/app/Session.cpp
git commit -m "feat(persistence): save/load gesture, stop-autosave, OLED notices"
```

---

### Task 11: Documentation refresh

**Files:**
- Modify: `docs/firmware-structure.md:30` ("There is no sketch-level persistence service." → describe `src/app/Session*`, `src/pico2seq-core/persistence/`; update the `src/app/` ownership table)
- Modify: `docs/voice-edit.md:4-6` (replace "Changes currently remain in RAM until a preset is loaded or power is lost" with the save/load reality)
- Modify: `docs/architecture.md` (add a persistence subsection to the Core-0 section: LittleFS partition, retained-RAM mirror, XIP-stall rule "flash writes only while transport stopped")
- Modify: `docs/ButtonHandlers.md` (Utility-mode table: button 1 = Save tap / Load long-press)
- Modify: `docs/manual.md` (user-facing: what persists, the gesture, autosave-on-stop, watchdog resume)
- Modify: `docs/oled.md` (new notice kinds)
- Modify: `docs/testing.md` (suite row for `[persistence]` test count)
- Modify: `.agents/skills/pico2seq-codebase/references/testing-and-build.md` (test-count refresh) and `references/architecture.md` if the watchdog-recovery description there (":98-100") is now stale — it is: recovery no longer always parks
- Run: `python tests/verify_docs_links.py` after any file moves (none planned, but run it anyway)

- [ ] **Step 1: Apply the edits above; keep each doc's existing voice/format.**
- [ ] **Step 2: Run the link checker** — `python tests/verify_docs_links.py` — expected: clean.
- [ ] **Step 3: Commit**

```bash
git add docs/ .agents/skills/pico2seq-codebase/references/
git commit -m "docs: pattern & patch persistence subsystem"
```

---

### Task 12: Verification — host suite + bench checklist

**Files:** none (verification only).

- [ ] **Step 1: Full host suite from a FRESH configure** (stale MSVC caches produce fake failures):

```bash
rm -rf build_test
cmake -B build_test -G Ninja -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang++.exe" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_USE_MATH_DEFINES"
cmake --build build_test --parallel
ctest --test-dir build_test --output-on-failure
```
Expected: everything green, including the new `[persistence]` cases (13+ total).

- [ ] **Step 2: Bench checklist (user runs these — never claim firmware results yourself):**

Flash with arduino-cli (COM33, updated FQBN from CLAUDE.md):
1. **First boot after reflash:** serial shows `[STORAGE] first boot: formatting LittleFS partition` then `no valid session; factory defaults`. (The 64 KB partition is fresh.)
2. **Save across power-off:** program a distinctive pattern (e.g., Note lane steps on voice 1), change preset/scale/tempo/theme → tap Utility-1 → OLED "SAVED" → power off → power on → pattern, patch, tempo, scale, theme all restored; serial `session loaded`.
3. **Power-loss atomicity:** save, then interrupt a second save mid-write by yanking USB power during the "SAVED" notice → reboot must load the *previous* valid session (never a corrupt one), or factory defaults if there was none.
4. **Autosave on stop:** edit steps with transport running → press Stop → wait 1 s → serial `[STORAGE] autosaved on stop` → power cycle → edits restored.
5. **Load gesture:** edit something → long-press Utility-1 → OLED "LOADED" → edits reverted to last saved.
6. **Watchdog resume:** with the session playing, send `W` over serial (bench hook, below) → board reboots within ~2 s → serial `[RECOVERY] watchdog reset; resuming live session from retained RAM` + `[FREEZE] previous watchdog reset: ... phase=loop: ...` → the live session (pattern/patch/settings as of ≤1 s before the freeze) is restored; the transport starts playing from step 1 because `initializeClock()` always starts uClock at boot — same as today's power-on behavior.
7. **Watchdog escape hatch:** hold the board in a failing state (e.g., disconnect the MPR121 pad board — `ControlIO.cpp` deliberately halts on missing hardware) → expect up to 3 rapid reboot/resume attempts, then the park message `[RECOVERY] session resume unavailable/exhausted. Power-cycle to retry.` — no infinite reboot loop.
8. **Audio sanity:** save while playing — expect a brief (~0.1–0.5 s) audio gap and transport restart from step 1 of the pattern; no persistent artifacts, underrun counters (`[DIAG C1]`) unchanged after the gap.

The `W` serial hook (add in Task 10's commit, top of `Application::update()` after the recovery early-return):

```cpp
    // Bench aid: type 'W' over serial to stop feeding the watchdog and prove
    // the retained-RAM resume path end-to-end (see plan Task 12).
    if (Serial.available() > 0 && Serial.read() == 'W')
    {
        Serial.println("[BENCH] freezing Core 0 on request");
        for (;;) {}
    }
```

- [ ] **Step 3: Final commit if the bench turns up fixes** — each fix gets its own TDD cycle added back into this plan's task it belongs to; do not fix silently.

---

## Self-review record

- **Spec coverage vs improvement-plan item 1:** serialize 9 tracks/voice ✓ (Tasks 3, 5), `VoiceConfig` edits as values + preset index with pointer rule ✓ (Task 4), patch bases ✓ (inside PatchSnapshot flags/fields), globals (scale, shuffle, theme, tempo, master volume, encoder bases, cursors) ✓ (Tasks 2, 6), magic/version/checksum ✓ (Task 2; precedent AlchemyProto checksum noted), `ParameterManager` (de)serialization ✓ (codec + raw accessors, Task 3), write path stops transport + drains control updates + Core 0 only ✓ (Task 10 executor + Global Constraints), restore hooks in `VoiceSetup`-adjacent boot flow + `Application::begin` ✓ (Task 8), Phase-2 autosave on transport stop + watchdog-restore ✓ (Tasks 9, 10), golden round-trip + corruption tests ✓ (Tasks 2, 5), flash I/O hardware-bound / format portable ✓.
- **Known deviations from the spec sketch (deliberate):** storage is LittleFS-64 KB, not EEPROM — the ~10.2 KB snapshot physically cannot fit the 4 KB EEPROM sector; snapshot POD + 12-byte frame instead of per-class `serialize()` methods — keeps `pico2seq-core` free of stream types and gives one locked, static_asserted layout; the frame's `SNAPSHOT_FORMAT_VERSION` serves the spec's "magic/version/checksum" ask — a separate `FIRMWARE_VERSION` constant is omitted because the frame version already gates every migration decision (add one only when a second firmware generation needs to distinguish itself); retained-RAM mirror added beyond the sketch — it is what makes watchdog recovery *instant and wear-free* rather than "load last flash save".
- **Type consistency:** `ProjectSnapshotV1` / `PatternSnapshot` / `TrackSnapshot` / `PatchSnapshot` / `SettingsSnapshot` / `EncoderBaseSnapshot` / `RetainedStore` names and field names are used identically in Tasks 2–10; `SessionStorage::{begin,load,save}`, `Session::{captureSession,applyBeforeVoices,applyAfterVoices,applyAfterClock,requestSave,requestLoad,consumePendingAction,lastSavedCrc,setLastSavedCrc,g_bootLoadedOk}`, `RetainedSession::{bootInit,resumeAllowed,refresh,markBootCompleted,takeResumeSnapshot}` are the cross-task contract.
- **Placeholder scan:** two explicit "verify against the real header while implementing" notes (Sequencer construction pattern in tests, exact include names in `Session.cpp`) — these are verification instructions, not unspecified work; every code step shows the actual code.
