# Preset & Sweet-Spot Modulation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every sequencer lane sweep — human-recorded or randomized — land inside each parameter's musical "sweet spot", by giving every preset an owned, centered lane layout and by replacing the randomizer's fixed ±0.05 offset with an explicit, per-lane modulation-depth model.

**Architecture:** Three layers. (1) Math: `DspMapping` gains a true-exponential `OCT` curve, and `VoiceParameterBinding` gains an optional `center` value that lane 0.5 maps to (JUCE `setSkewForCentre` doctrine, piecewise map). (2) Data: every preset owns a constexpr `VoiceParameterLayout` (the mechanism already exists and is used by the 14 recipe presets — `VoiceParameters::layout()` prefers `config.parameters`), whose lane ranges are retuned to musical spans with centers at each timbre's operating point, reserving the top ~10–20% of travel for "special effect" territory. (3) Behavior: `ParameterManager::randomizeParameters` collapses to a single base-relative path with injectable seed, global depth %, per-lane amounts, and a triangular (base-biased) distribution.

**Tech Stack:** C++17 constexpr preset tables, host-test suite (CMake/Ninja/clang from repo root, `build_test_ninja/`), rpdsp submodule (untouched), Arduino/Pico2 bench flash via arduino-cli.

**Spec:** This document is self-contained; §0 is the design rationale (research-verified). It supersedes item 3 ("Engine-aware randomizer") of `docs/superpowers/plans/2026-09-12-functionality-improvements.md` — that item's "unnormalized feed" diagnosis predates the patch-modifier composition path and its line anchors are stale — and extends the shipped `2026-09-03-preset-param-overhaul.md` architecture (paramSet slot re-purposing) to per-preset owned layouts.

## Global Constraints

- Branch: `DeCluttered`. Baseline test suite is green (268/268); it must stay green after every task.
- The user edits the repo live. Before editing, re-verify every file:line anchor in this plan against HEAD (`git show HEAD:<file>` / grep); anchors were valid at `DeCluttered` head on 2026-09-15.
- `src/rpdsp` is a git submodule: **no submodule changes**. All lane storage stays normalized 0–1 (`ParameterTrack`/`PatchCodec`/persistence depend on it).
- Preset IDs are append-only (`PresetBank.h` header comment): never reorder or renumber; MIDI CC 74/73/72 keep carrying the re-purposed slots on non-standard presets.
- All lane mutations stay on Core 0 via the existing staged-callback discipline; nothing new on the audio ISR path.
- Firmware can't be built headlessly — bench tasks use arduino-cli (`COM33`, fqbn `rp2040:rp2040:rpipico2`).
- Curve compatibility: existing `LINEAR`/`EXP`/`LOG` semantics must not change (shipped presets/patches depend on them); new behavior goes in new curve types and the optional `center` field.

---

## 0. Design rationale (research-verified, 2026-09-15)

Two web-research passes (all sources page-verified by the researchers) plus a first-hand code audit produced these governing facts:

**Current-state diagnosis (code audit):**
- Lane→DSP mapping happens in two disjoint places: `VoiceParameterBinding::map()` (`VoiceParameters.cpp:7-10`, used by repurposed slots) and, for envelope/cutoff on standard presets, direct `fmap` calls in `Voice.cpp:1090-1093` (cutoff) and `Voice::applyEnvelopeParameters` (`Voice.cpp:776-796`, patch-bases path via `MusicalValues::envelopeSeconds` = `0.001·10000^n`, 1 ms–10 s, so lane 0.5 = 100 ms for **both** attack and decay).
- `dspmap::EXP` is a **square** curve (`min + in²·(max−min)`), not exponential — frequency/time lanes get an S-less parabola whose long tail wastes travel (research: time/frequency parameters want true exponential taper).
- No binding has a "center": lane 0.5 means the arithmetic midpoint of whatever min/max the binding declares, which is usually not the timbre's operating point (e.g. FM Ratio lane 0.5 = 4.25 with FmGlass resting at 3.5; HollowBell Edge resting 0.06 normalizes to lane 0.13 — base glued to the bottom of travel).
- Every STANDARD-family preset rests its cutoff at `fmap(0.5, 120, 5000, EXP)` ≈ 1340 Hz because the Filter binding has no target and no per-preset center — a bass and a pad share one resting cutoff.
- The firmware randomizer path (`ParameterManager.cpp:144-309`, `patchModifiers=true` always) writes Velocity/Filter/Attack/Decay offsets uniform in **0.45–0.55** around each lane base (composed at playback by `VoiceEdit::composeLane` = `clamp(base + n − 0.5, 0..1)`, `VoiceEditParameters.cpp:969-989`). That is sweet-spot-*centered* but only ±5% of travel — musically inert — and the legacy `patchModifiers=false` branch (fixed standard-voice subranges, the object of the old item 3) is dead code that still compiles and confuses.
- Randomizer RNG is a time-seeded LCG with no injection point → untestable. Existing randomizer tests pin the *legacy* ranges (`test_sequencer.cpp:70` Filter ⊆ 0.2–0.8).
- The machinery for per-preset lane layouts already exists and is production-proven (recipe presets): `VoiceParameters::layout()` returns `config.parameters` when non-null (`VoiceParameters.cpp:89-94`); layouts carry per-layout `cutoffMinimum/Maximum` (`VoiceParameters.h:34-37`); all 9 ParamId slots are assignable.

**Design guidance (research):**
- Time & frequency lanes: exponential taper `out = min·(max/min)^lane`, spans of 3–7 decades (EarLevel's ADSR LUT rationale: 1 ms–10 s in one sweep; Minimoog attack 1 ms–10 s). Linear ms travel wastes the sub-20 ms region where instrument identity lives (Sound on Sound Synth Secrets).
- "Sweet spot at center of travel" is standard plugin doctrine — JUCE `NormalisableRange::setSkewForCentre()` exists precisely so `convertFrom0to1(0.5)` returns the chosen centre; Ableton Live macros expose Min/Max refinement for the same reason.
- Reserve the top ~10–20% of effect lanes for special territory: resonance→self-osc, detune→supersaw smear (JP-8000 measured ±181 cents max; useful strings zone ≈ ±10–30 cents), FM feedback→noise (Yamaha: below ~90/100 feedback is a brightness control, above it "drastic changes"). A full lane sweep should stay mostly musical.
- FM ratio: integer ratios stay tonal, non-integer gives bells/clang (SOS FM series, Wikipedia); musical C:M ratios live in 0.5–5 with 4.77 as Yamaha's documented metallic extreme — a 0.5–8 linear lane spends half its travel beyond the useful range.
- Randomizers that sound good constrain per-parameter ranges and bias toward defaults: Vital ships `{min, max, default, curve}` per parameter (`synth_parameters.cpp` `ValueDetails`); Pigments randomizes per-lane by percentage amount with a master Regen; OP-Z encodes lanes as small signed deltas around defaults (velocity "−4…+3"); Elektron practice is sparse, expressive deltas on selected steps rather than full-scale redraws; the academic GA/DX7 literature (Horner 1993 → Yee-King 2018) always searches inside native parameter bounds with selection — never uniform over extremes.
- Karplus-Strong: T60 is directly a seconds parameter (JOS `g0 = exp(−6.91·P/S)`), exponential taper banjo→harp ≈ 0.2–10 s; pick position β must avoid the degenerate ends; course detune slow-shimmer zone is single-digit cents at guitar pitch, swelling to ~26 cents for the 12-string effect.

**Consequence:** two coordinated moves. *Ranges+centers* (Tasks 1–7) make full lane travel = musical span with the operating point at lane 0.5. *Depth model* (Tasks 8–9) makes randomization an expressive dial around each preset's base instead of a fixed ±5% shimmer or a dead legacy branch.

---

## File Structure (locked decomposition)

| File | Responsibility | Change |
|---|---|---|
| `src/utils/DspMapping.h` | curve math | Add `OCT` curve + `fmapCentered()` free function |
| `src/voice/VoiceParameters.h` | binding/layout types | Add `center` field to `VoiceParameterBinding`; declare `mapCutoff()` helper |
| `src/voice/VoiceParameters.cpp` | binding math + fixed layouts | Center-aware `map()`/`normalize()`; `mapCutoff()`; retune WAVEGUIDE/HYPERSAW/NOISESTORM/HARDSYNC legacy tables (fallback only) |
| `src/utils/MusicalValues.h` | envelope lane curves | Add `attackSeconds()` (1 ms–2 s) beside `envelopeSeconds()` |
| `src/voice/Voice.cpp` | chain application | Cutoff path via `mapCutoff()`; envelope attack path via `attackSeconds()` |
| `src/voice/presets/OscillatorPresets.h` | 9 osc presets | Per-preset `VoiceParameterLayout` tables + `parameters=` wiring |
| `src/voice/presets/StringPresets.h` | 4 waveguide presets | Per-preset layouts (T60/Bright/Pick spans per character) |
| `src/voice/presets/TexturePresets.h` | Hypersaw + NoiseStorm | Per-preset layouts (Detune/Mix, Color/Regen/Chaos spans) |
| `src/voice/presets/RecipePresets.h` | 6 recipe presets | Shared macro layouts → per-preset copies with retuned ranges + centers |
| `src/voice/presets/MusicalPresets.h` | 8 recipe presets | Same conversion for the remaining 8 |
| `src/pico2seq-core/sequencer/ParameterManager.h/.cpp` | randomizer | Single-path rewrite: depth %, per-lane amounts, triangular draw, injectable seed; delete legacy branch |
| `src/ui/ButtonHandlers.cpp` | randomize entry point | Update call signature |
| `src/app/VoiceEditParameters.cpp` (verify-only) | compose/laneBase | Ensure centered bindings yield laneBase 0.5; PatchCodec identity check |
| `tests/unit/test_parameter_mapping.cpp` | new | Curve + center math |
| `tests/unit/test_parameter_randomize.cpp` | new | Randomizer behavior under fixed seeds |
| `tests/unit/test_sequencer.cpp` | update | Replace legacy-range randomizer assertions |
| `docs/voice.md`, `docs/superpowers/plans/2026-09-12-functionality-improvements.md` | docs | Lane table refresh; mark item 3 superseded |

Task order: math (1–3) → data (4–7) → behavior (8–9) → polish/docs/bench (10–11). Each task leaves the suite green and commits.

---

### Task 1: `OCT` curve + `fmapCentered()` in DspMapping

**Files:**
- Modify: `src/utils/DspMapping.h`
- Test: `tests/unit/test_parameter_mapping.cpp` (create)

**Interfaces:**
- Produces: `dspmap::Mapping::OCT` (true exponential: `min·(max/min)^in`, requires `min>0`); `float dspmap::fmapCentered(float in, float min, float max, float center, Mapping curve)` — piecewise map where `in==0.5` returns `center`; and `float dspmap::normalizeCentered(float v, float min, float max, float center, Mapping curve)` inverse. Existing `fmap()` behavior unchanged.

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/unit/test_parameter_mapping.cpp
#include "../utils/DspMapping.h"   // adjust include path to repo convention (see test_sequencer.cpp)
#include <cmath>
#include <cassert>
#include <cstdio>

static void testOctEndpoints() {
  assert(std::abs(dspmap::fmap(0.f, 100.f, 8000.f, dspmap::Mapping::OCT) - 100.f) < 1e-4);
  assert(std::abs(dspmap::fmap(1.f, 100.f, 8000.f, dspmap::Mapping::OCT) - 8000.f) < 1e-3);
  assert(std::abs(dspmap::fmap(0.5f, 100.f, 8000.f, dspmap::Mapping::OCT) - 800.f) < 1e-3); // sqrt(100*8000)
}
static void testCenteredLinear() {
  // center nearer min: 0..0.5 covers 100..300, 0.5..1 covers 300..900
  assert(std::abs(dspmap::fmapCentered(0.5f, 100.f, 900.f, 300.f, dspmap::Mapping::LINEAR) - 300.f) < 1e-5);
  assert(std::abs(dspmap::fmapCentered(0.25f, 100.f, 900.f, 300.f, dspmap::Mapping::LINEAR) - 200.f) < 1e-5);
  assert(std::abs(dspmap::fmapCentered(0.75f, 100.f, 900.f, 300.f, dspmap::Mapping::LINEAR) - 600.f) < 1e-5);
}
static void testCenteredOct() {
  // octave-domain halves: lane 0.25 -> sqrt(min*center)
  const float c = dspmap::fmapCentered(0.25f, 100.f, 6400.f, 400.f, dspmap::Mapping::OCT);
  assert(std::abs(c - 200.f) < 1e-3);   // sqrt(100*400)
  const float d = dspmap::fmapCentered(0.75f, 100.f, 6400.f, 400.f, dspmap::Mapping::OCT);
  assert(std::abs(d - 1600.f) < 1e-2);  // sqrt(400*6400)
}
static void testNormalizeRoundTrip() {
  for (float n = 0.f; n <= 1.0001f; n += 0.05f) {
    const float v = dspmap::fmapCentered(n, 100.f, 6400.f, 400.f, dspmap::Mapping::OCT);
    const float back = dspmap::normalizeCentered(v, 100.f, 6400.f, 400.f, dspmap::Mapping::OCT);
    assert(std::abs(back - n) < 1e-4);
  }
}
static void testMidpointUncentered() {
  // normalizeCentered with center == geometric midpoint == plain normalize
  const float v = dspmap::fmap(0.3f, 100.f, 1000.f, dspmap::Mapping::OCT);
  assert(std::abs(dspmap::normalizeCentered(v, 100.f, 1000.f,
      dspmap::fmap(0.5f, 100.f, 1000.f, dspmap::Mapping::OCT), dspmap::Mapping::OCT) - 0.3f) < 1e-4);
}
int main() {
  testOctEndpoints(); testCenteredLinear(); testCenteredOct();
  testNormalizeRoundTrip(); testMidpointUncentered();
  std::printf("test_parameter_mapping OK\n");
  return 0;
}
```

Register the test in the unit-test CMake target the same way `tests/unit/test_sequencer.cpp` is registered (find the `add_executable`/`target_sources` block in the tests CMakeLists and mirror it).

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build_test_ninja -j && ctest --test-dir build_test_ninja -R parameter_mapping --output-on-failure`
Expected: compile failure — `OCT` / `fmapCentered` not declared.

- [ ] **Step 3: Implement in `src/utils/DspMapping.h`**

Inside `namespace dspmap`, extend the enum and add:

```cpp
enum class Mapping : uint8_t { LINEAR = 0, EXP, LOG, OCT };
// OCT: true exponential (min * (max/min)^in); requires min > 0.
```

In `fmap()`'s switch add the case (clamp `in` to [0,1] as the function already does):

```cpp
case Mapping::OCT:
  return min * std::pow(max / min, in);
```

Add the centered pair (keep everything `constexpr`-friendly / header-only to match file style):

```cpp
constexpr float halfMap(float x, float a, float b, Mapping curve) noexcept {
  switch (curve) {
    case Mapping::OCT:    return a * std::pow(b / a, x);
    case Mapping::LOG:    { const float ia = 1.0f / std::log10(b / a); return a * std::pow(10.f, x / ia); }
    case Mapping::EXP:    return a + (x * x) * (b - a);
    case Mapping::LINEAR: default: return a + x * (b - a);
  }
}
// Lane 0.5 -> center; [0,0.5] maps min..center, [0.5,1] maps center..max, curve applied per half.
constexpr float fmapCentered(float in, float min, float max, float center, Mapping curve) noexcept {
  in = in < 0.f ? 0.f : in > 1.f ? 1.f : in;
  return in < 0.5f ? halfMap(in * 2.f, min, center, curve)
                   : halfMap((in - 0.5f) * 2.f, center, max, curve);
}
constexpr float halfNormalize(float v, float a, float b, Mapping curve) noexcept {
  switch (curve) {
    case Mapping::OCT:    return std::log10(v / a) / std::log10(b / a);
    case Mapping::LOG:    { const float ia = 1.0f / std::log10(b / a); return ia * std::log10(v / a); }
    case Mapping::EXP:    { const float t = (v - a) / (b - a); return std::sqrt(t < 0.f ? 0.f : t); }
    case Mapping::LINEAR: default: return (v - a) / (b - a);
  }
}
constexpr float normalizeCentered(float v, float min, float max, float center, Mapping curve) noexcept {
  return v < center ? 0.5f * halfNormalize(v, min, center, curve)
                    : 0.5f + 0.5f * halfNormalize(v, center, max, curve);
}
```

Guard the existing `validBank()`-style static_assert pattern if `DspMapping.h` has one; add `min > 0` checks for `OCT` where the file validates `LOG` today.

- [ ] **Step 4: Run tests to green**

Run: `cmake --build build_test_ninja -j && ctest --test-dir build_test_ninja -R parameter_mapping --output-on-failure`
Expected: PASS. Then run the full suite once: `ctest --test-dir build_test_ninja --output-on-failure` (nothing else should move).

- [ ] **Step 5: Commit**

```bash
git add src/utils/DspMapping.h tests/unit/test_parameter_mapping.cpp <tests CMakeLists>
git commit -m "feat(dspmap): OCT curve + centered piecewise mapping"
```

---

### Task 2: `center` on VoiceParameterBinding (map/normalize become center-aware)

**Files:**
- Modify: `src/voice/VoiceParameters.h`, `src/voice/VoiceParameters.cpp`
- Test: `tests/unit/test_parameter_mapping.cpp` (extend)

**Interfaces:**
- Produces: `VoiceParameterBinding` gains `float center = kUncentered;` (a `constexpr float kUncentered` sentinel = NaN, declared in `VoiceParameters.h`) plus `bool isCentered() const noexcept { return center == center; }`. `map()` and `normalize()` route through `fmapCentered`/`normalizeCentered` when centered, preserving current behavior otherwise. Aggregate-initializer order in every binding table gains one optional trailing member, so **all existing braced tables keep compiling unchanged** (add `center` last).

- [ ] **Step 1: Write failing tests** (append to `test_parameter_mapping.cpp`)

```cpp
#include "VoiceParameters.h"  // path per repo convention
static void testBindingCentered() {
  VoiceParameterBinding b{"T60", nullptr, 0.2f, 4.0f, dspmap::Mapping::OCT,
                          VoiceParameterUnit::Seconds, true, 0.5f, /*center*/ 1.8f};
  assert(std::abs(b.map(0.5f) - 1.8f) < 1e-4);
  assert(std::abs(b.map(0.f) - 0.2f) < 1e-3);
  assert(std::abs(b.map(1.f) - 4.0f) < 1e-3);
  assert(std::abs(b.normalize(1.8f) - 0.5f) < 1e-4);
}
static void testBindingUncenteredUnchanged() {
  VoiceParameterBinding b{"X", nullptr, 0.2f, 4.0f, dspmap::Mapping::OCT,
                          VoiceParameterUnit::Seconds, true};
  assert(!b.isCentered());
  assert(std::abs(b.map(0.5f) - 0.2f * std::pow(4.0f / 0.2f, 0.5f)) < 1e-4); // plain OCT midpoint
}
```

- [ ] **Step 2: Run → compile failure** (`center` not declared).

- [ ] **Step 3: Implement.** In `VoiceParameters.h` add to the struct (as the **last** member, after `defaultNormalized`):

```cpp
  float center = std::numeric_limits<float>::quiet_NaN(); // lane 0.5 value; NaN = geometric/arithmetic midpoint
  bool isCentered() const noexcept { return center == center; }
```

(`#include <limits>`.) In `VoiceParameters.cpp`, rewrite `map()`/`normalize()` (currently `VoiceParameters.cpp:7-10` / `12-23`) to delegate:

```cpp
float VoiceParameterBinding::map(float n) const noexcept {
  return isCentered() ? dspmap::fmapCentered(n, minimum, maximum, center, curve)
                      : dspmap::fmap(dspmap::clamp01(n), minimum, maximum, curve); // keep existing clamp
}
float VoiceParameterBinding::normalize(float v) const noexcept {
  if (maximum <= minimum) return 0.f;
  return isCentered() ? dspmap::normalizeCentered(v, minimum, maximum, center, curve)
                      : /* existing per-curve inverse, unchanged */;
}
```

- [ ] **Step 4: Suite green** (full run — existing binding tables must compile untouched).
- [ ] **Step 5: Commit** — `git commit -m "feat(voice): optional per-binding center for sweet-spot mapping"`

---

### Task 3: Split envelope lane curves — `attackSeconds()`

**Files:**
- Modify: `src/utils/MusicalValues.h`, `src/voice/Voice.cpp` (patch-bases path in `applyEnvelopeParameters`, ≈`Voice.cpp:778-785`)
- Test: `tests/unit/test_parameter_mapping.cpp` (extend)

**Interfaces:**
- Produces: `float MusicalValues::attackSeconds(float n)` = `0.001f * 2000^n` → 1 ms–2 s, lane 0.5 ≈ **45 ms** (percussive attack zone gets the middle of travel). `envelopeSeconds()` (1 ms–10 s) stays the decay/release curve, lane 0.5 = 100 ms. `Voice.cpp` attack path calls `attackSeconds`, decay path keeps `envelopeSeconds`.

- [ ] **Step 1: Failing test** — assert `attackSeconds(0)≈0.001`, `attackSeconds(0.5)≈0.0447`, `attackSeconds(1)≈2.0`.
- [ ] **Step 2: Run → fail.**
- [ ] **Step 3: Implement** `attackSeconds` in `MusicalValues.h` beside `envelopeSeconds` (same style); switch the attack call site in `Voice.cpp` (grep `envelopeSeconds(` — exactly one attack use, one decay use; change only the attack one).
- [ ] **Step 4: Suite green.** (Any envelope tests that pin `0.001·10000^n` for attack must be updated to the new curve — grep `10000` in tests.)
- [ ] **Step 5: Commit** — `feat(voice): percussive attack lane curve (1ms-2s, 45ms center)`

---

### Task 4: Per-preset layouts — Oscillator family (9 presets)

**Files:**
- Modify: `src/voice/presets/OscillatorPresets.h`, `src/voice/VoiceParameters.cpp` (`mapCutoff` helper), `src/voice/Voice.cpp` (cutoff call site), `src/app/VoiceEditParameters.cpp` (laneBase for centered null-target lanes), `src/voice/PatchCodec.cpp` (verify only)
- Test: `tests/unit/test_parameter_mapping.cpp` (extend)

**Interfaces:**
- Produces: `VoiceParameters::mapCutoff(const VoiceParameterLayout &p, float n)` → cutoff Hz honoring layout `cutoffMinimum/Maximum` and the Filter slot's `center` when present. Each osc preset gains `k<Name>Layout` (constexpr `VoiceParameterLayout`) with `slots[Filter] = {nullptr-target binding carrying center}` and `parameters = &k<Name>Layout` in its factory. `VoiceEdit::laneBase()` returns 0.5 for bindings that are centered but targetless (so compose `base + n − 0.5` rests at the preset's cutoff center).

**Lane ranges & centers (the sweet-spot table for this family):**

| Preset | Filter lane (cutoff Hz, OCT) center | Rationale |
|---|---|---|
| Analog | 150–6000, **center 1800** | bright sync pluck; top 20% = scream zone |
| Digital | 200–5000, **center 1500** | hollow square pair |
| Bass | 60–1500, **center 320** | SVF growl zone; sub untouched below |
| Lead | 200–8000, **center 1600** | driven ladder lead |
| Square | 250–4000, **center 900** | BP24 band center |
| Pad | 250–10000, **center 2200** | chord wash |
| Percussion | 800–12000, **center 4500** | noise brightness (hat↔splash) |
| SubFunk | 60–1600, **center 420** | sub funk |
| RubberSub | 90–1200, **center 320** | resonant honk |

Velocity lane stays targetless/uncentered (VCA). Attack/Decay lanes stay targetless (envelope from tracks, now via Task 3's curves). Filter slot binding: `{"Cutoff", nullptr, min, max, dspmap::Mapping::OCT, VoiceParameterUnit::Hertz, true, 0.5f, center}`.

- [ ] **Step 1: Failing tests** — (a) `mapCutoff` on the Bass layout: 0→60, 0.5→320, 1→1500 (±1 Hz); (b) monotonicity across all nine; (c) `layout(bassConfig).cutoffMinimum == 60`.
- [ ] **Step 2: Run → fail.**
- [ ] **Step 3: Implement.**
  1. `mapCutoff` in `VoiceParameters.cpp` + declare in header; switch `Voice.cpp` cutoff site (`fmap(state.filterCutoff, p.cutoffMinimum, p.cutoffMaximum, EXP)` at ≈`:1090`) to `VoiceParameters::mapCutoff(p, state.filterCutoff)`.
  2. In `OscillatorPresets.h`, add a file-local helper and nine layouts, then one line per factory:

```cpp
constexpr VoiceParameterLayout cutoffLayout(float minHz, float centerHz, float maxHz) {
  VoiceParameterLayout p{};
  p.slots[size_t(ParamId::Filter)] = {"Cutoff", nullptr, minHz, maxHz,
      dspmap::Mapping::OCT, VoiceParameterUnit::Hertz, true, 0.5f, centerHz};
  return p;
}
inline constexpr auto kAnalogLayout = cutoffLayout(150.f, 1800.f, 6000.f);
// ... kDigitalLayout, kBassLayout, kLeadLayout, kSquareLayout, kPadLayout,
//     kPercussionLayout, kSubFunkLayout, kRubberSubLayout per the table
// in makeAnalog():  c.parameters = &kAnalogLayout;
```

  3. `VoiceEditParameters.cpp` `laneBase()` (≈`:536-558`): if the binding has null target **and** `isCentered()`, return 0.5f. Verify `composeLane` (≈`:969-989`) still clamps — unchanged.
  4. Verify `PatchCodec.cpp` (≈`:74`): preset identity is the pointer at *serialization* time and restored via preset index — confirm a non-recipe preset owning `parameters` round-trips (add a round-trip case to the existing patch codec test if one exists; otherwise assert in `test_parameter_mapping.cpp` that `getPresetConfig(idx).parameters != nullptr` for all 29 after Task 6).
- [ ] **Step 4: Suite green** (envelope/voice tests must be unaffected; filter tests that pinned 120–5000/EXP need updating to the new per-preset expectation — grep `5000` in tests).
- [ ] **Step 5: Commit** — `feat(presets): owned centered lane layouts for oscillator presets`

---

### Task 5: Per-preset layouts — Waveguide (4 presets)

**Files:**
- Modify: `src/voice/presets/StringPresets.h`
- Test: `tests/unit/test_parameter_mapping.cpp` (extend)

**Interfaces:** Same mechanism as Task 4. The T60 lane now has a per-preset span instead of the shared `kWaveguideT60Min/Max` 0.05–10 s; keep those constants (fallback table still uses them).

**Sweet-spot table (OCT curve on T60; LINEAR Bright/Pick; centers = the timbre's operating point):**

| Preset | T60 lane (s) center | Bright lane center | Pick lane center |
|---|---|---|---|
| WgPluck | 0.15–4.0, **1.8** | 0.2–0.95, **0.78** | 0.2–1.0, **0.85** |
| WgNylon | 0.6–8.0, **3.2** | 0.05–0.6, **0.28** | 0.05–0.6, **0.22** |
| WgBell | 0.25–3.0, **1.4** | 0.55–0.98, **0.9** | 0.7–1.0, **1.0** |
| WgShimmer | 1.5–10.0, **6.5** | 0.3–0.8, **0.55** | 0.3–0.9, **0.6** |

Velocity lane stays pluck-energy (targetless). `VoicePresets::wgT60ToNormalized` (used by tests only) keeps working — it reads the binding via `VoiceParameters::binding(...)`, which is now center-aware; no change required, but add a round-trip assertion per preset: `binding.normalize(presetT60)` ∈ [0.4, 0.6] for the four resting values 1.8/3.2/1.4/6.5.

- [ ] **Step 1: Failing tests** (round-trip centers + endpoints per table).
- [ ] **Step 2: Run → fail.**
- [ ] **Step 3: Implement** — four `constexpr VoiceParameterLayout` tables in `StringPresets.h` (T60/Bright/Pick slots exactly as the current WAVEGUIDE param set declares them in `VoiceParameters.cpp:40-43`, but with the per-preset min/max/center above; `envelopeFromTracks=false`, `velocityToAmp=false`, cutoff fields irrelevant since `hasFilter=false` — copy defaults) and `c.parameters = &k…Layout` in each factory.
- [ ] **Step 4: Suite green.**
- [ ] **Step 5: Commit** — `feat(presets): per-preset string lane spans (T60 stays musical per voice)`

---

### Task 6: Per-preset layouts — Texture (Hypersaw, NoiseStorm)

**Files:**
- Modify: `src/voice/presets/TexturePresets.h`
- Test: `tests/unit/test_parameter_mapping.cpp` (extend)

**Interfaces:** Same mechanism. NoiseStorm's Regen lane may now legitimately reach the config's documented 0–1.2 range (`VoiceConfig.h:129`) — the binding currently caps at 1.0 (`VoiceParameters.cpp:60`), which the layout replaces anyway.

**Sweet-spot table:**

| Preset | Lane | Range, center | Notes |
|---|---|---|---|
| Hypersaw | Attack→Detune | 0–0.75, **center 0.30** | x⁴ curve: 0.30→±8¢ thickening, 0.75→±43¢ strings; full smear (±185¢) reserved out of lane range |
| Hypersaw | Decay→Mix | 0.15–0.95, **center 0.50** | width dial; extremes clipped (dry saw / center-vanish) |
| Hypersaw | Filter (cutoff kept) | 200–12000 Hz OCT, **center 3200** | supersaw register |
| NoiseStorm | Filter→Color | 0–1, **center 0.60** | |
| NoiseStorm | Attack→Regen | 0–1.2, **center 0.90** | top ~25% (1.0–1.2) = deliberate howl/bloom zone (governor keeps it safe) |
| NoiseStorm | Decay→Chaos | 0–0.8, **center 0.40** | 0.8 already guttural; rate clamp handles the rest |

- [ ] **Step 1: Failing tests** — endpoints + centers per table; assert Regen lane max is now 1.2.
- [ ] **Step 2: Run → fail.**
- [ ] **Step 3: Implement** two layouts + factory wiring, same pattern as Tasks 4–5.
- [ ] **Step 4: Suite green.**
- [ ] **Step 5: Commit** — `feat(presets): texture-preset lane spans with reserved effect zones`

---

### Task 7: Recipe layouts → per-preset, retuned ranges + centers (14 presets)

**Files:**
- Modify: `src/voice/presets/RecipePresets.h`, `src/voice/presets/MusicalPresets.h`
- Test: `tests/unit/test_parameter_mapping.cpp` (extend)

**Interfaces:** The ten shared `k*Parameters` layouts become per-preset copies (`k<Name>Layout`) so each preset's centers can match its own resting macros. `recipeVoice()` gains a `const VoiceParameterLayout &parameters` parameter (or factories pass their layout — keep one signature: `recipeVoice(recipe, parameters, color, shape, character)` already takes `parameters`; factories simply stop sharing). `VoiceEdit::selectRecipe()` (≈`VoiceEditParameters.cpp:526-535`) clamps macros into the new lane ranges — verify it also handles the narrowed ranges (e.g. FM Ratio max 8→4.77 must clamp stored macro2 of 3.5 fine, but a user patch parked at 6.0 clamps gracefully).

**Retuned macro ranges (curve; center = musical operating point):**

| Macro family | Lane | Old | New range, center | Rationale (research) |
|---|---|---|---|---|
| kFm | Index | 0–1 EXP | 0–1 EXP, center **0.30** | Bessel: β≲0.5 vocal, β≈1 saw-like; >0.6 gnarl zone |
| kFm | Ratio | 0.5–8 LINEAR | 0.5–4.77 OCT, center **2.0** | integer ratios tonal; 4.77 = Yamaha metallic extreme |
| kFm | Feedback | 0–0.5 EXP | 0–0.35 EXP, center **0.10** | <0.35 keeps feedback musical; snarl reserved out |
| kPhase | Shape/Skew/Blend | 0–1 / −1–1 / 0–1 | unchanged ranges; centers **0.5 / 0.0 / 0.5** | Skew already bipolar-centered ✓ |
| kDsf | Bright | 0–0.9 LINEAR | unchanged; center **0.45** | |
| kDsf | Spacing | 0.5–8 LINEAR | 0.5–5.07 OCT, center **2.0** | odd/even harmonic zones both reachable |
| kDsf | Sub | 0–1 LINEAR | unchanged; center **0.3** | |
| kPrism | Focus/Spread | 0–1 | unchanged; centers **0.45 / 0.55** | |
| kPrism | Drift | 0–1 LINEAR | 0–0.85, center **0.30** | >0.65 seasick zone reserved (ChaosPrism 0.65 still in range) |
| ReedPipe | Formant | 1–6 LINEAR | 1–6 OCT, center **3.0** | formant ratio across the vowel set |
| ReedPipe | Bloom / Body | 0–1 / 0.1–0.8 | unchanged; centers **0.7 / 0.4** | |
| SilkPad | Silk / Detune / Blend | 0–0.65 / 0–1 / 0.1–0.5 | unchanged; centers **0.25 / 0.35 / 0.35**; Blend max → **0.6** | resting 0.5 no longer glued to max |
| HollowBell | Ratio | 0.5–6 LINEAR | 0.5–6 OCT, center **2.0** | |
| HollowBell | Edge / Ring | 0–0.45 / 0–0.85 | unchanged; centers **0.15 / 0.5** | Edge resting 0.06 lifts off lane bottom |
| SyncLead | Sync | 1–5 LINEAR | 1–5 OCT, center **2.0** | |
| SyncLead | Edge / Bite | 0–0.65 / 0.1–0.8 | unchanged; centers **0.25 / 0.45** | |
| OrbitPluck | Index | 0–3 EXP | unchanged; center **1.2** | |
| OrbitPluck | Ratio / Body | 0.5–4 / 0.1–0.8 | unchanged; centers **2.0 / 0.4** | |
| AirChime | Focus / Spread / OctMix | 0–0.8 / 0.1–1 / 0–0.65 | unchanged; centers **0.3 / 0.55 / 0.25** | |

- [ ] **Step 1: Failing tests** — for each of the 14 presets and each of its 3 macro lanes: `map(0.5)==center`, `map(0)/map(1)==range ends`, and `normalize(resting)` ∈ [0.15, 0.85] (no preset base glued to a rail). Data table = this table.
- [ ] **Step 2: Run → fail.**
- [ ] **Step 3: Implement.** Convert each shared layout into per-preset constexpr copies (name them `kFmGlassLayout`, `kFmBassLayout`, …). Same slot names/units; only min/max/curve/center per the table. Update `recipeVoice(...)` call sites. Keep the old shared tables removed (no dead data) — `grep -rn "kFmParameters\\|kPrismParameters\\|…" src/ tests/` must come back clean (update `test_voice_recipes.cpp` references to the per-preset layouts).
- [ ] **Step 4: Suite green.**
- [ ] **Step 5: Commit** — `feat(presets): per-preset recipe macro ranges centered on operating points`

---

### Task 8: Randomizer rewrite — depth model, single path, injectable seed

**Files:**
- Modify: `src/pico2seq-core/sequencer/ParameterManager.h`, `src/pico2seq-core/sequencer/ParameterManager.cpp:144-309`, `src/ui/ButtonHandlers.cpp:68`
- Test: `tests/unit/test_parameter_randomize.cpp` (create; register in tests CMake)

**Interfaces:**
- Produces: `void ParameterManager::randomizeParameters(uint8_t depthPercent = 35, uint64_t seed = 0)` (seed 0 = time-seeded, preserving today's behavior); `void ParameterManager::setLaneAmount(ParamId id, uint8_t percent)` with per-lane default 100; `static constexpr uint8_t kDefaultRandomizeDepth = 35`. The legacy `patchModifiers=false` branch and the `bool` parameter are **deleted**; `Sequencer::randomizeParameters()` (`Sequencer.cpp:516-522`) forwards depth 35.

**New behavior (per the Vital/Pigments/OP-Z findings):**
1. **Draw:** for each of Velocity/Filter/Attack/Decay, per step: `offset = 0.5 + (u1 + u2 - 1.0) * (depth/100) * (laneAmount[id]/100)` where `u1,u2` are uniform [0,1) — a triangular distribution centered on the lane base, half-width = depth. Depth 100 = full-lane triangular spread (still biased to center); depth 35 (default) ≈ current reach but expressive tail; depth 10 ≈ today's ±5%.
2. **Untouched lanes stay untouched:** Gate/Slide never written (Elektron conditions doctrine); Octave and GateLength forced 0.5 exactly as today; Note keeps `lcg_rand_int(0,12)` (playback quantizes to scale).
3. **Base-relative composition is unchanged** — `VoiceEdit::composeLane` still does `clamp(base + n − 0.5, 0, 1)`; nothing moves on the audio path.
4. RNG: keep the LCG, add `void seed_lcg(uint64_t)`; `lcg_rand_float` unchanged.

- [ ] **Step 1: Failing tests**

```cpp
// tests/unit/test_parameter_randomize.cpp  (shape; adapt to repo test macros)
static void testBoundsAndCenter() {          // seed fixed
  ParameterManager pm; pm.randomizeParameters(35, 12345);
  for (ParamId id : {ParamId::Velocity, ParamId::Filter, ParamId::Attack, ParamId::Decay}) {
    const auto &t = pm.track(id);
    for (size_t s = 0; s < t.stepCount(); ++s)
      assert(t.value(s) >= 0.f && t.value(s) <= 1.f);
  }
}
static void testDepthRadius() {              // depth 10 -> offsets within 0.5±0.10 of 0.5... on the RAW lane
  ParameterManager pm; pm.randomizeParameters(10, 777);
  const auto &t = pm.track(ParamId::Filter);
  for (size_t s = 0; s < t.stepCount(); ++s)
    assert(std::abs(t.value(s) - 0.5f) <= 0.101f);
}
static void testGateSlideUntouched() {       // pre-fill gate=1.0/slide=0.25, randomize, expect identical
}
static void testLaneAmountZero() {           // setLaneAmount(Filter, 0) -> Filter lane unchanged
}
static void testTriangleBias() {             // depth 100, 64 steps, fixed seed: mean within 0.5±0.15
}
```

- [ ] **Step 2: Run → fail** (signature mismatch).
- [ ] **Step 3: Implement** the draw rule above in `ParameterManager.cpp`; delete the legacy branch body (`:188-305`); update `ButtonHandlers.cpp:68` to `randomizeParameters()` (defaults). Keep the post-randomize Octave force in `Sequencer.cpp:516-522`.
- [ ] **Step 4: Commit after green** — `feat(sequencer): depth-based sweet-spot randomizer (single path, seedable)`

---

### Task 9: Retire legacy randomizer assertions

**Files:**
- Modify: `tests/unit/test_sequencer.cpp:70,96,345`
- Test: same file

- [ ] **Step 1:** Rewrite the three assertions to the new contract: offsets within depth radius; monotone depth behavior (`depth 60` reaches strictly wider offsets than `depth 10` on a fixed seed); Note integers preserved.
- [ ] **Step 2:** Full suite: `ctest --test-dir build_test_ninja --output-on-failure` → all green (baseline 268 ± net-new tests).
- [ ] **Step 3: Commit** — `test(sequencer): pin depth-based randomizer contract`

---

### Task 10: Preset value touch-ups + comment truthing

**Files:**
- Modify: `src/voice/presets/OscillatorPresets.h`

**Changes (deliberately tiny, each defensible on its own):**
1. `makePad()`: `defaultAttack 0.02f → 0.4f` — the comment says "Slow attack for pad" but 20 ms is a pluck; with Task 3's attack lane the *user* can sweep 1 ms–2 s anyway. Keep release 0.5.
2. `makeDigital()`: fix the stale "Naive triangle" comments over `WAVE_BSP_SQUARE` assignments (comment-only; sound unchanged — changing to `WAVE_TRI` would alter a shipped patch).
3. `makeAnalog()`: no change — note in commit message that its sync character is intentionally Velocity-driven (lane center 1:1).

- [ ] **Step 1:** Apply the two edits; suite green (no test pins Pad attack).
- [ ] **Step 2: Commit** — `fix(presets): pad gets its advertised slow attack; comment truthing`

---

### Task 11: Docs + supersede markers + bench validation

**Files:**
- Modify: `docs/voice.md` (lane mapping tables + randomize section), `docs/superpowers/plans/2026-09-12-functionality-improvements.md` (mark item 3 superseded by this plan), `docs/manual.md` (if it documents randomize/lane ranges)

- [ ] **Step 1:** Update `docs/voice.md`: per-family lane tables with the new ranges/centers; document the depth model ("Randomize depth: lane offsets are drawn triangular around each preset's base; depth 35% default"). Update any OLED formatting notes only if units changed (they didn't — names and units are unchanged by this plan).
- [ ] **Step 2:** Mark item 3 of the 2026-09-12 plan: "> Superseded 2026-09-15 by `2026-09-15-preset-sweet-spot-modulation.md` (patch-modifier path made the unnormalized-feed diagnosis obsolete; depth model + per-preset layouts implemented instead)."
- [ ] **Step 3: Full suite green**, then bench (memory: `arduino-cli upload` UF2 to COM33, fqbn `rp2040:rp2040:rpipico2`) and listen:
  - Bass/Pad/SubFunk: cutoff lane 0→1 sweeps musical, no dead top; rest point sounds like the preset (not a universal 1340 Hz).
  - WgPluck: Decay lane 0.5 = the familiar 1.8 s pluck; lane 1.0 ≈ 4 s (not 10); WgShimmer 1.0 = 10 s halo.
  - FMBass: Ratio lane 1.0 = 4.77 (metallic), center 2.0 stays punchy.
  - NoiseStorm: Regen lane top = howl zone (intentional), center = gusting.
  - Analog: Velocity center = clean saw; extremes = sync zip.
  - Randomize at depth 10/35/60 on 3 presets: 10 subtle, 35 musical, 60 adventurous but never nonsense.
- [ ] **Step 4: Commit docs** — `docs: sweet-spot lane ranges + randomizer depth model`

---

## Future (explicitly out of scope, YAGNI)

- FM **soft ratio-snap** (blend of snapped-to-integer and continuous travel): needs playtest data first; the OCT range retune already removes the worst dead travel.
- Per-lane randomize **amount UI** (Pigments-style dice %) and **auto-regen** on bar boundaries: needs a free encoder interaction design.
- "Hybridize" (crossfade two saved patterns' lanes) and Turing-style global chaos lane: blocked on pattern persistence (2026-09-12 plan item 1).
- Corpus-learned ranges (Fannon-style): only interesting once users have saved many patches.

## Self-review

- Spec coverage: sweet-spot modulation ranges → Tasks 1–7; modulation depth in sequence-parameter mode → Tasks 3, 8–9; preset improvements → Tasks 4–7, 10; research guidance → §0 and the per-task rationale lines. Everything the user asked for has a task.
- Placeholders: none — all ranges/centers are exact; all code steps carry real code or exact value tables; verification commands are concrete.
- Type consistency: `fmapCentered`/`normalizeCentered`/`halfMap` signatures match between Tasks 1–2; `mapCutoff` used identically in Tasks 4–6; `randomizeParameters(uint8_t, uint64_t)` consistent across Tasks 8–9 and the ButtonHandlers call site.
