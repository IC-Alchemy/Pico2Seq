# E2E Test Infra: Pico2Seq Documentation Audit

## Test Philosophy
- Requirement-driven, opaque-box documentation validation.
- Methodology: Category-Partition + BVA + Pairwise Cross-Subsystem + Real-World Workflow Testing.
- All documentation files in `docs/` must strictly match the codebase in `src/`, `Pico2Seq.ino`, and `tests/`.

## Feature Inventory & Test Coverage
| # | Feature | Requirement Source | Tier 1 | Tier 2 | Tier 3 |
|---|---------|-------------------|:------:|:------:|:------:|
| 1 | Dual-Core Architecture | R1, ORIGINAL_REQUEST §1 | 5 | 5 | ✓ |
| 2 | VoiceSystem Accessors & Asymmetry | R1, ORIGINAL_REQUEST §1 | 5 | 5 | ✓ |
| 3 | Lock-Free Parameter Staging | R1, ORIGINAL_REQUEST §1 | 5 | 5 | ✓ |
| 4 | Voice API & Accurate Types | R1, ORIGINAL_REQUEST §1 | 5 | 5 | ✓ |
| 5 | Voice Presets Accurate Table | R1, ORIGINAL_REQUEST §1 | 5 | 5 | ✓ |
| 6 | RPDSP Namespace & DSP Chain | R1, R3, ORIGINAL_REQUEST §1, §3 | 5 | 5 | ✓ |
| 7 | Debug Logging Utility | R2, ORIGINAL_REQUEST §2 | 5 | 5 | ✓ |
| 8 | Sequencer Core Decoupling | R1, R2, ORIGINAL_REQUEST §1, §2 | 5 | 5 | ✓ |
| 9 | Polymetric ParameterTrack | R1, ORIGINAL_REQUEST §1 | 5 | 5 | ✓ |
| 10 | ShuffleTemplates & uClock | R1, ORIGINAL_REQUEST §1 | 5 | 5 | ✓ |
| 11 | Scales & Dual Pitch Offsets | R1, ORIGINAL_REQUEST §1 | 5 | 5 | ✓ |
| 12 | Sensors Subsystem | R1, ORIGINAL_REQUEST §1 | 5 | 5 | ✓ |
| 13 | Dual-Surface Alchemy UI | R1, R2, ORIGINAL_REQUEST §1, §2 | 5 | 5 | ✓ |
| 14 | ControlSurfaceLogic Module | R1, R2, ORIGINAL_REQUEST §1, §2 | 5 | 5 | ✓ |
| 15 | ButtonHandlers Code Snippet Fix | R1, ORIGINAL_REQUEST §1 | 5 | 5 | ✓ |
| 16 | Migration Doc Completion Status | R2, ORIGINAL_REQUEST §2 | 5 | 5 | ✓ |
| 17 | Matrix vs LEDMatrix Differentiation | R2, ORIGINAL_REQUEST §2 | 5 | 5 | ✓ |
| 18 | LEDMatrix Pair-Based Logic | R1, ORIGINAL_REQUEST §1 | 5 | 5 | ✓ |
| 19 | OLED Priority Display Hierarchy | R1, ORIGINAL_REQUEST §1 | 5 | 5 | ✓ |
| 20 | MIDI Note/CC & Realtime Clock | R1, ORIGINAL_REQUEST §1 | 5 | 5 | ✓ |
| 21 | Testing Guide & CTest Workflow | R1, ORIGINAL_REQUEST §1 | 5 | 5 | ✓ |
| 22 | Stale Mudras & Broken Link Cleanup | R2, R3, ORIGINAL_REQUEST §2, §3 | 5 | 5 | ✓ |

## Test Architecture
- Verification Commands:
  - Unit tests: `cmake -B build_test -DCMAKE_BUILD_TYPE=Debug && cmake --build build_test --parallel && ctest --test-dir build_test --output-on-failure`
  - Link checking: Verify all markdown relative links across `docs/` and root/sub-READMEs.
  - Term consistency: Verify zero stale "Mudras", "PicoMudrasSequencer", "PROGRAMMERS_MANUAL", or legacy "src/dsp" DaisySP references exist.
  - Signature verification: Match all C++ signatures in docs against header files in `src/`.

## Real-World Application Scenarios (Tier 4)
| # | Scenario | Features Exercised | Complexity |
|---|----------|--------------------|------------|
| 1 | New Developer Onboarding: Architecture & Dual-Core flow | F1, F2, F3, F6, F7 | High |
| 2 | Sound Designer Workflow: Presets, DSP chain & Scales | F4, F5, F6, F11 | High |
| 3 | Sequencer Customization: Polymetric tracks & UI adapter | F8, F9, F10, F14 | High |
| 4 | Hardware Integration: Sensors, Dual-Surface Tiles, Matrix & OLED | F12, F13, F14, F15, F17, F18, F19 | High |
| 5 | External Connectivity & Testing: MIDI & Host Test Suite | F20, F21, F22 | Medium |

## Coverage Thresholds
- Tier 1: ≥5 per feature
- Tier 2: ≥5 per feature
- Tier 3: Pairwise coverage of major feature interactions
- Tier 4: 5 realistic developer/application scenarios
