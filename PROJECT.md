# Project: Pico2Seq Documentation Audit & Synchronization

## Architecture
Pico2Seq is an RP2350 (Raspberry Pi Pico 2) dual-core firmware for a 4-voice polyphonic step sequencer and synthesizer.
- **Core 1**: Real-time audio engine. Runs `fill_audio_buffer()` -> `voiceManager->processAllVoices()` -> `FloatToPcm16()` with ARM Cortex-M33 `__SSAT` -> I2S DMA @ 48kHz stereo (3 buffers x 256 samples).
- **Core 0**: System control, sensors, MIDI, and UI. Runs 1ms sensor poll (TMAG5273A @ 0x35, VL53L1X @ 0x29, MPR121 32 touch pads @ 0x5A on Wire; Alchemy tile panel on Wire1 @ 100kHz), 20ms/50Hz display refresh (128x64 SH1106G OLED @ 0x3C, 8x4 WS2812B FastLED matrix on GPIO 1), TinyUSB CDC serial console (USB MIDI removed 2026-09-06), and uClock sequencer tick processing (stock uClock, timer ISR on core 0).
- **Cross-Core Concurrency**: Lock-free parameter and pitch staging via atomic generation counters (`paramsGen_`, `pitchGen_`) in `Voice.h/.cpp`. Mutex-free shared state with `volatile` globals.
- **Voice System**: 4 polyphonic synthesizer voices (`VoiceSystem::MAX_VOICES = 4`). Gate timing (`GateTimer`) and MIDI Note/CC output are active on Voices 0 and 1; Voices 2 and 3 are audio-only synthesis voices.
- **Sequencer Core**: Portable C++ `pico2seq-core` decoupled from hardware. Polymetric `ParameterTrack<N>` (Note, Velocity, Filter, Attack, Decay, Octave, GateLength, Gate, Slide). UI adapter `advanceSequencerStep()` in `src/ui/UIEventHandler.h/.cpp`.
- **UI & Control Surface**: Dual-surface architecture with Alchemy panel (SliderModule + ButtonModule8 on Wire1 GP14/15, GP7 mode strap) + 32-pad MPR121 step matrix (Wire GP4/5) managed via `ControlSurfaceLogic` and `UIState`.
- **Testing**: Catch2 v3.5.2 host unit test suite built with CMake/CTest using header stubs in `tests/stubs/`.

## Feature Inventory
| # | Feature / Area | Description | Milestone | Source |
|---|----------------|-------------|-----------|--------|
| 1 | Dual-Core Architecture | Document Core 0 (audio) vs Core 1 (UI/sensors/MIDI/clock), 3x256 buffer metrics, 48kHz I2S DMA | M1 | survey_1 |
| 2 | VoiceSystem Accessors & Asymmetry | Document `MAX_VOICES = 4`, accessor API pattern, and Voices 0-1 (gate/MIDI) vs Voices 2-3 (audio-only) | M1 | survey_1 |
| 3 | Lock-Free Parameter Staging | Document atomic generation counters (`paramsGen_`, `pitchGen_`), staged state, and audio-thread commit | M1 | survey_1 |
| 4 | Voice API & Accurate Types | Document `Voice` class API, `noexcept` signatures, `VoiceConfig`, `VoiceState` (`float octaveOffset`, `uint16_t gateLengthTicks`) | M1 | survey_1 |
| 5 | Voice Presets Accurate Table | Document the 15 verified presets in `VoicePresets.cpp` (Analog, Digital, Bass, Lead, Square, Pad, Percussion, SubFunk, RubberSub, WgPluck, WgNylon, WgBell, WgShimmer, Hypersaw, NoiseStorm), built as `constexpr` factory tables | M1 | survey_1 |
| 6 | RPDSP Namespace & DSP Chain | Document `rpdsp::` integration, `VoiceOscillator` variant dispatch, ladder filter, ADSR, wavefolder, compressor status | M1 | survey_1 |
| 7 | Debug Logging Utility | Document `src/utils/Debug.h/.cpp` zero-allocation logging system (`DBG_ERROR`, `DBG_WARN`, `DBG_INFO`, `DBG_VERBOSE`) | M1 | survey_3 |
| 8 | Sequencer Core Decoupling | Document `pico2seq-core/sequencer/` isolation and UI adapter `advanceSequencerStep()` in `src/ui/` | M2 | survey_2 |
| 9 | Polymetric ParameterTrack | Document independent track step counts, `ParamId` enum, and modulo indexing | M2 | survey_2 |
| 10 | ShuffleTemplates & uClock | Document `ShuffleTemplates.h` shuffle patterns and uClock tick processing | M2 | survey_2 |
| 11 | Scales & Dual Pitch Offsets | Document 13 scales, 48 steps, C3 (+48) synthesis vs C2 (+36) MIDI pitch offsets, and precomputed rank cache | M2 | survey_2 |
| 12 | Sensors Subsystem | Document TMAG5273 magnetic encoder (`MagEncoder`, `EncoderManager`), VL53L1X distance, MPR121 touch | M3 | survey_2 |
| 13 | Dual-Surface Alchemy UI | Document Alchemy panel (Wire1 @ 100kHz, SliderModule + ButtonModule8, GP7 mode strap) + 32-pad MPR121 matrix | M3 | survey_2 |
| 14 | ControlSurfaceLogic Module | Document `ModeStabilizer`, `PadBank`, `ShiftLatch`, `FaderMap`, and `UIState` single source of truth | M3 | survey_2 |
| 15 | ButtonHandlers Code Snippet Fix | Fix `BUTTON_PLAY_STOP` snippet in `docs/ButtonHandlers.md` to match `ButtonHandlers.cpp` | M3 | survey_2 |
| 16 | Migration Doc Completion Status | Update `docs/alchemyui-tmag5273-migration.md` status banner to COMPLETED with historical context note | M3 | survey_2 |
| 17 | Matrix vs LEDMatrix Differentiation | Reconcile and clearly differentiate `docs/matrix.md` (MPR121 input) and `docs/LEDMatrix.md` (WS2812B output) | M4 | survey_3 |
| 18 | LEDMatrix Pair-Based Logic | Document pair-based voice LED indicator logic (`selectedVoiceIndex % 2 == 0`) and 10 LED themes | M4 | survey_3 |
| 19 | OLED Priority Display Hierarchy | Document OLED priority screens: `PARAM`/`UTIL` banner (`alchemyModeBannerUntil`), settings, seq length gauge, edit screens | M4 | survey_3 |
| 20 | MIDI Note/CC & Realtime Clock | Document 2-voice MIDI tracking limitation (Voices 0 & 1 only), CC mappings, and realtime clock transmission | M4 | survey_3 |
| 21 | Testing Guide & CTest Workflow | Document Catch2 v3.5.2 host suite, `test_control_surface_logic.cpp`, header stubs in `tests/stubs/`, and `ctest` commands | M4 | survey_3 |
| 22 | Stale Mudras & Broken Link Cleanup | Remove legacy "Mudras" / "PicoMudrasSequencer" references, fix broken links in `src/matrix/README.md`, update root `README.md` | M4 | survey_3 |
| 23 | E2E Documentation Verification | Perform cross-document link, signature, and build test validation across all docs | M5 | orchestrator |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1 | Architecture, Dual-Core & Voice/DSP Documentation | `docs/architecture.md`, `docs/voice.md`, `docs/VoiceSystem.md` | none | DONE |
| M2 | Sequencer, Scales & Core Logic Documentation | `docs/sequencer.md`, `docs/scales.md` | none | DONE |
| M3 | Sensors, UI, Control Surface & Button Handlers | `docs/sensors.md`, `docs/ButtonHandlers.md`, `docs/alchemyui-tmag5273-migration.md` | none | DONE |
| M4 | Displays, Matrix, MIDI, Testing & Repo Cleanups | `docs/LEDMatrix.md`, `docs/matrix.md`, `docs/oled.md`, `docs/midi.md`, `docs/testing.md`, `README.md`, sub-READMEs | M1, M2, M3 | DONE |
| M5 | E2E Documentation Verification & Quality Gate | Entire `docs/` repository, tests validation, link integrity, audit | M1, M2, M3, M4 | DONE |

## Interface Contracts
### Documentation Conventions
- Accurate C++17 type annotations matching code in `src/` and `tests/`.
- Markdown links must use relative paths from the document's directory and point to existing files.
- Code blocks must use standard syntax highlighting (`cpp`, `bash`, `markdown`).
- Subsystem documents must reference `src/` file paths accurately.
- Dual-core division must explicitly specify Core 0 (Audio DMA) vs Core 1 (UI/Sensors/MIDI/Sequencer).

## Code Layout
- `docs/`: All architecture and subsystem documentation.
- `docs/superpowers/specs/`: Hardware and feature specification documents.
- `src/`: Firmware source code.
- `src/pico2seq-core/`: Portable core sequencer and scale logic.
- `src/rpdsp/`: Vendored DSP library (submodule).
- `src/AlchemyUI/`: Alchemy UI component library (tracked in-repo, not a submodule).
- `src/VelocityEncoder/`: TMAG5273 magnetic encoder driver (submodule).
- `tests/`: Catch2 unit tests and stubs.
