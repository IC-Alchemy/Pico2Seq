# Pico2Seq Architecture

This document gives a high-level overview of modules, data flow, and key responsibilities to make the codebase easier to navigate.

## Top-Level
- Pico2Seq.ino — Arduino entry point. Initializes hardware, systems, and main loop.
- includes.h — Central aggregator of library and project headers (Arduino-friendly `src/...` includes).
- docs/ — Documentation.
- src/ — Source modules grouped by domain.

## Modules (src/)
- audio/
  - `audio.h`, `audio_i2s.h`, `audio.cpp` — I2S configuration and audio callback wiring for Pico2.
- dsp/
  - DSP building blocks (ADSR, filters, oscillators, delay, overdrive, wavefolder). Local fork of DaisySP-style code.
- sequencer/
  - `Sequencer.h/.cpp`, `SequencerDefs.h`, `ShuffleTemplates.h` — Step/parameter tracks, polymeter lengths, shuffle.
- voice/
  - `Voice.h/.cpp`, `VoiceManager.h/.cpp`, `VoiceSystem.h/.cpp` — Voice rendering pipeline and central voice coordination.
- midi/
  - `MidiManager.h/.cpp` — MIDI I/O and note routing.
- matrix/
  - `Matrix.h/.cpp` — Touch matrix abstraction.
- ui/
  - `UIState.h`, `UIEventHandler.h/.cpp`, `ButtonManager.h/.cpp`, `ButtonHandlers.h/.cpp`, `UIConstants.h` — UI state and event handling across modes (slide, settings, step edit, etc.).
- sensors/
  - `AS5600Manager`, `DistanceSensor`, etc. — Real-time control inputs.
- LEDMatrix/
  - LED feedback abstractions and control.
- OLED/
  - `oled.h/.cpp` — Display feedback.
- utils/
  - Helpers and common utilities.

## Data Flow (simplified)
```
Touch Matrix / Encoder / Sensors / MIDI In
             │
             ▼
          UIEventHandler (ui/)
             │  (updates UIState, toggles steps/params, invokes control handlers)
             ▼
        Sequencer (sequencer/) ──► per-step params (note/vel/filter/env/slide)
             │
             ▼
        VoiceSystem (voice/) ──► VoiceManager/Voices ──► DSP (dsp/)
             │                                 │
             ├──────────► audio out (I2S)      └──────────► MIDI out (midi/)
             ▼
         LED/OLED feedback (LEDMatrix/, OLED/)
```

## Key Architectural Decisions
- UI handlers avoid hard-coding access to globals where possible and prefer operating on caller-supplied sequencer arrays. Back-compat overloads are provided.
- Sequencer supports independent track lengths per parameter for polymetric patterns.
- VoiceSystem centralizes voice access and can scale voice count with minimal code changes.

## Gate Sequence Length Mode (UI → Sequencer → LED/OLED)

- __Activation__: Long-hold the `AS5600` control button enters Gate Sequence Length Mode; release to exit. Toggling slide mode exits.
- __Behavior__: Step buttons set the Gate track length (2–16) for the currently selected voice via `Sequencer::setParameterStepCount(ParamId::Gate, len)`.
- __LED Feedback__: `src/LEDMatrix/LEDMatrixFeedback.cpp` renders a blinking band up to the current length on the selected voice row; the other row is dimmed. `uiState.resetStepsLightsFlag` forces a one-shot refresh when the length changes.
- __OLED Feedback__: `src/OLED/oled.cpp` shows "Gate Len Mode", selected voice, length, and a bar graph up to 16 while the mode is active.

## Next Improvements (Roadmap)
- Introduce an `AppContext` to replace `extern` usage across modules (UI, voices, MIDI). This improves testability and readability.
- Promote public headers to an `include/` directory and change project-wide includes from `../` to `#include "<module>/Header.h"`. Arduino configuration will keep `include/` and `src/` on the include path.
- Add unit tests for `sequencer/` logic.
- Add static analysis and formatting to CI (clang-format, clang-tidy/cppcheck).

## Coding Style
- `.clang-format` (LLVM base, 2-space indent) and `.editorconfig` enforce consistent style and EOLs.
