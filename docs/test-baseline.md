# Host test baseline

Recorded on **2026-10-06**, Linux x86-64, GCC 14.2, Debug, default HALF reverb storage.
Nine executables register **1,023 CTest checks: 991 pass, 32 fail, 0 skipped**.
The restored bypass target contributes two passing checks. The original 32 failure
names and assertion output match the 1,021-check baseline before cleanup.

## Reproduction

Run from a checkout containing the restored bypass target:

```bash
cmake -S . -B build_test_ninja -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPICO2SEQ_REVERB_STORAGE=HALF
cmake --build build_test_ninja --parallel 4
ctest --test-dir build_test_ninja --parallel 4 --output-on-failure --output-junit build_test_ninja/host-tests.xml
```

CTest returns a failing status while these failures remain. Compare the failed
names and assertions with this baseline when assessing a change; count alone
cannot show whether an old failure was replaced by a new one. Some checks appear
in both the main and focused executables.

| Input | Measured version |
|---|---|
| Firmware and existing test sources | `e8c5eabc45283a45d18369d31787c60b8bf50ca8` |
| `tests/CMakeLists.txt` with restored bypass target (Git blob) | `c43a571b2e3e326ed3733e2a56a7f91ff18a0fe4` |
| Compiler | GCC 14.2.0 |
| CMake / Ninja | 3.31.10 / 1.13.0 |
| Catch2 | v3.5.2 |
| rpdsp pin | `111751161e3bacd9c218907235f683eab7a96e4a` |
| VelocityEncoder pin | `a8ed15d9088ddc157d824e3c8f0fa42031dd9a04` |

The source revision precedes the build-configuration change that registers the
bypass checks. The blob identifies that configuration without depending on this
document's commit hash. Compiler and build variants can change the failure set;
older platform observations remain dated in the [testing guide](testing.md).

## Results by executable

| Executable | Passed | Failed | Skipped |
|---|---:|---:|---:|
| `pico2seq_tests` | 602 | 26 | 0 |
| `pico2seq_ui_tests` | 205 | 0 | 0 |
| `pico2seq_voice_tests` | 118 | 6 | 0 |
| `pico2seq_watchdog_tests` | 4 | 0 | 0 |
| `pico2seq_audio_tests` | 1 | 0 | 0 |
| `pico2seq_tile_tests` | 25 | 0 | 0 |
| `py32_slider_tests` | 21 | 0 | 0 |
| `py32_button_tests` | 13 | 0 | 0 |
| `pico2seq_reverb_bypass_tests` | 2 | 0 | 0 |

## Failed checks

### Main suite (26)

- `Oscillator presets own octave cutoff lanes centered on their resting cutoff`
- `Hard sync follows the oscillator bank and keeps the preset cutoff lane`
- `Texture presets own spans that keep their effect zones at the top`
- `Notes stay compact integers and octave/gate length return to neutral`
- `Default sequencer starts at neutral octave and half-step gate`
- `Rest steps leave the triggering note's voice settings intact`
- `Step Edit waits for a selected future step while transport runs`
- `Lidar button lanes reach stored steps, OLED values and published voices`
- `Patch bases and recorded step values are independent`
- `OLED snapshot matches playback and never triggers a note`
- `Displayed notes use the same tuning as rendered oscillator pitches`
- `Sequencer OLED formats final physical and preset-specific units`
- `Patch randomization stays within its depth around the preset bases`
- `Live parameter modulation with distance sensor produces distinct values across all lanes`
- `Recorded attack and decay are heard on every oscillator voice`
- `An encoder base edit moves an oscillator voice's patch value while it plays`
- `Live envelope edits and patch re-sends never step a sounding note`
- `A step's own sustain holds its note`
- `A step's own release shapes its tail`
- `golden full-project round-trip through frame bytes`
- `Pitch lookup honors the injected scale table over the global`
- `No injected table falls back to chromatic mapping`
- `Voice combines note indices with octave track semitones`
- `Pitch lookup clamps out-of-range indices`
- `Only Analog and Lead keep the ladder filter`
- `Preset seeding survives the first audio update and preserves musical tracks`

### Focused voice suite (6)

- `voice-focused:Pitch lookup honors the injected scale table over the global`
- `voice-focused:No injected table falls back to chromatic mapping`
- `voice-focused:Voice combines note indices with octave track semitones`
- `voice-focused:Pitch lookup clamps out-of-range indices`
- `voice-focused:Only Analog and Lead keep the ladder filter`
- `voice-focused:Preset seeding survives the first audio update and preserves musical tracks`

The failures involve preset ranges, octave/gate defaults, pitch lookup, envelope
behavior, displayed units, modulation, and a persistence round trip. Each needs
investigation against the intended musical behavior before changing code or
expectations. They are reported here without disabling or weakening assertions.

## Scope of verification

The bypass checks also pass with FLOAT storage. The full FLOAT and fast-math
host suites were not re-recorded for this baseline. Hardware timing, controls,
sensors, and audible behavior require separate board checks; see the
[audio performance guide](audio-performance.md).
