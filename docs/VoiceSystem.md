# VoiceSystem Architecture Documentation

## 1. Overview

The `VoiceSystem` structure (`src/voice/VoiceSystem.h`) provides a centralized, array-based architecture for managing multi-voice state, voice identifiers, gate states, and gate countdown timers across the Pico2Seq firmware.

It replaces disparate global variables (`voice1Id`, `voice2Id`, `voiceState1`, `voiceState2`, `GATE1`, `GATE2`, etc.) with a single, bounds-checked data structure configured for `MAX_VOICES = 4` polyphonic voices.

---

## 2. Core Structure & Definition

Defined in `src/voice/VoiceSystem.h`:

```cpp
#pragma once

#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include <stdint.h>
#include "VoiceManager.h"

struct VoiceSystem {
    static constexpr uint8_t MAX_VOICES = 4;

    // Voice IDs assigned by VoiceManager
    uint8_t voiceIds[MAX_VOICES] = {0, 0, 0, 0};

    // Voice states containing per-voice synthesis parameters
    VoiceState voiceStates[MAX_VOICES];

    // Software gate flags (active on Voices 0 and 1 only; consumed by the
    // MIDI note-on path in the main sketch)
    volatile bool gates[2] = {false, false};

    // Gate countdown timers (active on Voices 0 and 1 only)
    GateTimer gateTimers[2];

    // Accessor methods with bounds checking
    uint8_t getVoiceId(uint8_t voiceIndex) const {
        return (voiceIndex < MAX_VOICES) ? voiceIds[voiceIndex] : 0;
    }

    void setVoiceId(uint8_t voiceIndex, uint8_t voiceId) {
        if (voiceIndex < MAX_VOICES) {
            voiceIds[voiceIndex] = voiceId;
        }
    }

    VoiceState& getVoiceState(uint8_t voiceIndex) {
        return voiceStates[voiceIndex < MAX_VOICES ? voiceIndex : 0];
    }

    const VoiceState& getVoiceState(uint8_t voiceIndex) const {
        return voiceStates[voiceIndex < MAX_VOICES ? voiceIndex : 0];
    }

    volatile bool& getGate(uint8_t voiceIndex) {
        static volatile bool dummy = false;
        return (voiceIndex < 2) ? gates[voiceIndex] : dummy;
    }

    GateTimer& getGateTimer(uint8_t voiceIndex) {
        static GateTimer dummy;
        return (voiceIndex < 2) ? gateTimers[voiceIndex] : dummy;
    }

    void stopAllGates() {
        for (uint8_t i = 0; i < 2; i++) {
            gates[i] = false;
            gateTimers[i].stop();
        }
    }

    void tickAllGateTimers() {
        for (uint8_t i = 0; i < 2; i++) {
            gateTimers[i].tick();
            if (gateTimers[i].isExpired() && gates[i]) {
                gates[i] = false;
            }
        }
    }
};

extern VoiceSystem voiceSystem;
```

---

## 3. Design Principles & Capabilities

1. **Centralized Voice Management**: All voice runtime state is grouped into one struct instance (`extern VoiceSystem voiceSystem`), eliminating scattered extern declarations.
2. **Array-Based Access**: Index-based operations allow clean iteration across voices without `switch/case` branching or redundant per-voice code paths.
3. **Hardware & MIDI Asymmetry Support**:
   - **Voices 0 and 1**: Fully equipped with USB MIDI note on/off tracking and `GateTimer` PPQN countdowns.
   - **Voices 2 and 3**: Audio-only synthesis voices. They participate in full 4-voice audio mixing via `VoiceManager`, but have no MIDI note outputs.
4. **Safe Dummy Access for Asymmetric Voices**:
   - Accessing `getGate(2)` or `getGate(3)` returns a safe reference to an internal `static volatile bool dummy = false`.
   - Accessing `getGateTimer(2)` or `getGateTimer(3)` returns a safe reference to an internal `static GateTimer dummy`.
   - Accessing `getVoiceState(index)` with an out-of-bounds index clamps to index `0`.
5. **Reference-Based Gate Assignment**: `getGate(voiceIndex)` returns a reference to the `volatile bool`, allowing callers to write directly: `voiceSystem.getGate(voiceIndex) = true;`.

---

## 4. Subsystem Integration

### 4.1 Dual-Core Role Division
- **Core 1 (Audio Thread)**: Synthesizes audio samples via `voiceManager->processAllVoices()`. It does not access `voiceSystem.gates` directly; parameter and pitch updates are staged lock-free from `VoiceState` into each `Voice` instance.
- **Core 0 (Control Thread)**: Updates `voiceSystem.voiceStates` on sequencer steps, toggles `voiceSystem.gates`, updates `voiceSystem.gateTimers`, and routes MIDI events.

### 4.2 UIState Integration
`UIState` (`src/ui/UIState.h`) mirrors `VoiceSystem`'s array-based model:
```cpp
struct UIState {
    static constexpr uint8_t MAX_VOICES = 4;
    uint8_t voicePresetIndices[MAX_VOICES] = {4, 2, 1, 6}; // Default presets: Square, Bass, Digital, Percussion (indices into the 15-entry VoicePresets bank)
    uint8_t selectedVoiceIndex = 0;                        // Currently focused voice (0-3)
    // ...
};
```

### 4.3 Sequencer Step Integration
When `uClock` fires `onStepCallback` on Core 0 (timer ISR) the step number is only
enqueued into `stepQueue`; `loop()` drains it via `processClockEvents()` →
`processSequencerStep()` (thread context), which does the per-step work:
```cpp
// Advance sequencers for all 4 voices
Sequencer* sequencers[VoiceSystem::MAX_VOICES] = {&seq1, &seq2, &seq3, &seq4};

for (uint8_t v = 0; v < VoiceSystem::MAX_VOICES; v++) {
    sequencers[v]->advanceStep();
    VoiceState& state = voiceSystem.getVoiceState(v);
    
    // Read active polymetric tracks
    state.noteIndex = sequencers[v]->getParameterValue(ParamId::Note);
    state.velocityLevel = sequencers[v]->getParameterValue(ParamId::Velocity);
    state.filterCutoff = sequencers[v]->getParameterValue(ParamId::Filter);
    state.isGateHigh = (sequencers[v]->getParameterValue(ParamId::Gate) > 0.5f);
    
    // Update Voice instance (lock-free staging to Core 1 audio)
    uint8_t voiceId = voiceSystem.getVoiceId(v);
    voiceManager->updateVoiceState(voiceId, state);
    
    // Gated voices (0 and 1) trigger hardware gates and timers
    if (v < 2 && state.isGateHigh) {
        voiceSystem.getGate(v) = true;
        uint16_t durationTicks = static_cast<uint16_t>(sequencers[v]->getParameterValue(ParamId::GateLength));
        voiceSystem.getGateTimer(v).start(durationTicks);
    }
}
```

### 4.4 Timing & PPQN Tick Processing
Inside `loop()` on Core 0, pending clock ticks drain from `ppqnTicksPending`:
```cpp
while (ppqnTicksPending > 0) {
    ppqnTicksPending--;
    globalTickCounter++;
    
    // Update MIDI note durations
    midiNoteManager.updateTiming(globalTickCounter);
    
    // Advance sequencer note duration countdowns
    seq1.tickNoteDuration();
    seq2.tickNoteDuration();
    seq3.tickNoteDuration();
    seq4.tickNoteDuration();
    
    // Tick gate countdown timers and clear expired gates
    voiceSystem.tickAllGateTimers();
}
```

---

## 5. API Reference Summary

| Method | Parameters | Returns | Description |
|---|---|---|---|
| `getVoiceId(voiceIndex)` | `uint8_t voiceIndex` (0–3) | `uint8_t` | Returns `voiceIds[voiceIndex]`, or `0` if index $\ge 4$. |
| `setVoiceId(voiceIndex, id)` | `uint8_t voiceIndex`, `uint8_t id` | `void` | Sets `voiceIds[voiceIndex]` if index $< 4$. |
| `getVoiceState(voiceIndex)` | `uint8_t voiceIndex` (0–3) | `VoiceState&` | Returns reference to `voiceStates[voiceIndex]`. Clamps invalid index to 0. |
| `getVoiceState(voiceIndex) const` | `uint8_t voiceIndex` (0–3) | `const VoiceState&` | Const reference version for read-only query. |
| `getGate(voiceIndex)` | `uint8_t voiceIndex` (0–3) | `volatile bool&` | Returns reference to `gates[voiceIndex]` for 0–1; returns static dummy `false` for $\ge 2$. |
| `getGateTimer(voiceIndex)` | `uint8_t voiceIndex` (0–3) | `GateTimer&` | Returns reference to `gateTimers[voiceIndex]` for 0–1; returns static dummy for $\ge 2$. |
| `stopAllGates()` | none | `void` | Resets `gates[0..1] = false` and calls `gateTimers[0..1].stop()`. |
| `tickAllGateTimers()` | none | `void` | Ticks `gateTimers[0..1]` and deasserts `gates[i]` when expired. |

---

## 6. Gate Sequence Length Mode Integration

Gate Sequence Length Mode allows per-voice adjustment of the Gate track length (2–16 steps):
- **User Action**: Long-hold encoder button (Utility mode bit 5) and press step pads 1–16.
- **Sequencer Call**: Calls `Sequencer::setParameterStepCount(ParamId::Gate, stepCount)` on the selected voice sequencer (`seq1..seq4`).
- **Feedback**:
  - `LEDMatrixFeedback` renders a blinking bar on the selected voice row up to the active Gate track length.
  - `oled.cpp` displays the "Gate Len Mode" screen with active voice number and horizontal length bar.