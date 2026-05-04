# Sequencer Module Documentation

Four voices move from one clock.

The Sequencer module is the step engine for Pico2Seq. It runs four independent sequencer instances against the shared VoiceSystem. Each voice can carry its own parameter tracks for Note, Velocity, Filter, Attack, Decay, Octave, GateLength, Gate, and Slide. Those tracks can use different step counts, so a pattern can turn slowly in one place and quickly in another.

The important rule is simple. Notes are only edited when the step Gate is HIGH. If Gate is LOW, note programming is ignored.

## Responsibilities

- Polyrhythmic sequencing for per-parameter step counts.
- Four independent sequencers, one per voice.
- Real-time sensor recording during playback.
- Gate-controlled note programming.
- Envelope triggering and note duration handling.
- VoiceSystem state updates through array-based access.
- MIDI CC output for voices 1 and 2 where configured.
- Hardware gate and step-clock output support.

## VoiceSystem Integration

The sequencers work through the VoiceSystem. Voice state, gate state, and voice IDs live in one place.

```cpp
// Four sequencer instances (one per voice)
Sequencer seq1(1), seq2(2), seq3(3), seq4(4);

// VoiceSystem provides centralized access
uint8_t voiceId = voiceSystem.getVoiceId(voiceIndex);           // Safe voice access
VoiceState& voiceState = voiceSystem.getVoiceState(voiceIndex); // Parameter state
bool gateActive = voiceSystem.getGate(voiceIndex);              // Gate state
voiceSystem.setGate(voiceIndex, true);                          // Set gate
```

This keeps the old scattered voice variables out of the hot path.

- Centralized access: all voice states are addressed from VoiceSystem.
- Bounds-aware access: callers use the VoiceSystem API instead of ad-hoc indexing.
- Scalable shape: the code is organized around `VoiceSystem::MAX_VOICES`.
- Clearer callsites: `voiceSystem.getVoiceState(index)` replaces branch-heavy lookup.

## Sequencer Instances

The firmware keeps four global sequencers. Each one belongs to one voice channel.

```cpp
// Global sequencer instances (Pico2Seq.ino)
Sequencer seq1(1), seq2(2), seq3(3), seq4(4); // One per voice

// Selection based on VoiceSystem
Sequencer* getActiveSequencer(uint8_t voiceIndex) {
    switch(voiceIndex) {
        case 0: return &seq1;
        case 1: return &seq2;
        case 2: return &seq3;
        case 3: return &seq4;
        default: return nullptr;
    }
}
```

## Sequencer Class API

The public API stays centered on parameter tracks, note timing, and step advance.

```cpp
class Sequencer {
public:
    // Construction - now 4 instances total
    Sequencer(uint8_t channel); // 1-4 for each voice

    // Parameter Management (per-voice)
    void initializeParameters();
    void resetAllSteps();
    void playStepNow(uint8_t stepIdx, VoiceState* voiceState);
    void toggleStep(uint8_t stepIdx);
    float getStepParameterValue(ParamId id, uint8_t stepIdx) const;
    void setStepParameterValue(ParamId id, uint8_t stepIdx, float value);
    uint8_t getParameterStepCount(ParamId id) const;
    void setParameterStepCount(ParamId id, uint8_t steps);

    // Sequencer Control (synchronized across all instances)
    void start() { running = true; }
    void stop() { running = false; }
    void reset();
    void randomizeParameters();

    // Note/Envelope Handling (VoiceSystem integrated)
    void startNote(uint8_t note, uint8_t velocity, uint16_t duration);
    void handleNoteOff(VoiceState* voiceState); // Uses VoiceSystem state
    void tickNoteDuration(VoiceState* voiceState);
    bool isNotePlaying() const;
    void setMidiNoteOffCallback(void (*callback)(uint8_t note, uint8_t channel));

    // Core Sequencing Method - VoiceSystem aware
    void advanceStep(uint8_t current_uclock_step, int mm_distance,
                    bool is_note_button_held, bool is_velocity_button_held,
                    bool is_filter_button_held, bool is_attack_button_held,
                    bool is_decay_button_held, bool is_octave_button_held,
                    int current_selected_step_for_edit, VoiceState *voiceState);

    // UIState Integration - centralized parameter handling
    void advanceStep(uint8_t current_uclock_step, int mm_distance,
                    const UIState& uiState, VoiceState *voiceState);
};
```

## VoiceSystem Shape

```cpp
// VoiceSystem provides array-based access to all voice data
struct VoiceSystem {
    static const uint8_t MAX_VOICES = 4;

    uint8_t voiceIds[MAX_VOICES];            // Voice identifier mapping
    VoiceState voiceStates[MAX_VOICES];      // Synthesis parameters per voice
    bool gates[MAX_VOICES];                  // Gate states
    GateTimer gateTimers[MAX_VOICES];        // Gate timing management

    // Safe accessor methods
    uint8_t getVoiceId(uint8_t index) const;
    VoiceState& getVoiceState(uint8_t index);
    bool getGate(uint8_t index) const;
    void setGate(uint8_t index, bool state);
    GateTimer& getGateTimer(uint8_t index);
};
```

Data moves in this order:

```text
Step Advance -> Sequencer Parameter Lookup -> VoiceState Population
                    |
          VoiceSystem Array Update -> DSP Processing
                    |
          MIDI CC Output + Audio Synthesis -> Hardware
```

## Setup

The VoiceManager is initialized with four voices. Each VoiceSystem slot receives an internal voice ID. Each ID is attached to its sequencer.

```cpp
// Initialize Voice Manager with 4 voices
voiceManager = std::make_unique<VoiceManager>(4);

// Create 4 voices with default presets
for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++) {
    voiceSystem.setVoiceId(i, voiceManager->addVoice(VoicePresets::getPresetConfig(uiState.voicePresetIndices[i])));
    voiceManager->attachSequencer(voiceSystem.getVoiceId(i), sequencers[i]);
}
```

Clock start and stop act on all four sequencers together.

```cpp
// Start all four sequencers when clock starts
void onClockStart() {
    usb_midi.sendRealTime(midi::Start);
    seq1.start(); seq2.start(); seq3.start(); seq4.start(); // All sequencers start simultaneously
    isClockRunning = true;
}

// Stop all sequencers when clock stops
void onClockStop() {
    usb_midi.sendRealTime(midi::Stop);
    seq1.stop(); seq2.stop(); seq3.stop(); seq4.stop();   // All sequencers stop simultaneously
    midiNoteManager.onSequencerStop();                    // Clean shutdown
    isClockRunning = false;
}
```

## Main Loop Pattern

The loop reads clock, distance, UI state, and then advances each voice. MIDI voice IDs for the playback path are zero-based for the first two MIDI voices.

```cpp
void loop() {
    // Get current step from UClock (synchronized across all sequencers)
    uint8_t currentStep = uClock.getCurrentStep();

    // Get sensor distance for real-time recording (global for all sequencers)
    int distance = distanceSensor.getDistance();
    float normalizedDistance = constrain(distance / 400.0f, 0.0f, 1.0f); // Global parameter input

    // Get UI state for button presses and voice selection
    const UIState& uiState = getUIState();

    // Process each sequencer with VoiceSystem integration
    for (uint8_t voiceIndex = 0; voiceIndex < VoiceSystem::MAX_VOICES; voiceIndex++) {
        // Get voice state from VoiceSystem
        VoiceState& voiceState = voiceSystem.getVoiceState(voiceIndex);

        // Select appropriate sequencer for this voice
        Sequencer* activeSequencer = nullptr;
        switch(voiceIndex) {
            case 0: activeSequencer = &seq1; break;
            case 1: activeSequencer = &seq2; break;
            case 2: activeSequencer = &seq3; break;
            case 3: activeSequencer = &seq4; break;
        }

        if (activeSequencer) {
            // Advance sequencer step with VoiceSystem integration
            activeSequencer->advanceStep(currentStep, distance, uiState, &voiceState);

            // Update voice parameters through VoiceSystem
            voiceManager->updateVoiceState(voiceSystem.getVoiceId(voiceIndex), voiceState);

            // Handle gate timing (optional - some implementations use separate gate management)
            if (voiceState.isGateHigh) {
                voiceSystem.setGate(voiceIndex, true);
                voiceSystem.getGateTimer(voiceIndex).start(voiceState.gateLengthTicks);
            }

            // MIDI output for voice 1 and 2 only (configurable)
            if (voiceIndex < 2) {
                uint8_t midiVoiceId = voiceIndex;
                midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Filter, voiceState.filterCutoff);
                midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Attack, voiceState.attackTimeSeconds);
                midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Decay, voiceState.decayTimeSeconds);
                midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Octave, voiceState.octaveOffset);
            }
        }
    }

    // Process global gate timers (VoiceSystem integration)
    voiceSystem.tickAllGateTimers();
}
```

## Gate-Controlled Programming

This is the behavior to protect.

When a Note parameter is being edited, the code checks the step Gate first. A Gate value of `0.0` or anything `<= 0.5f` means LOW. The edit returns without changing the note.

```cpp
void updateParametersForStep(uint8_t stepToUpdate) {
    // Get active sequencer based on selected voice
    uint8_t voiceIndex = uiState.selectedVoiceIndex;
    Sequencer* seqPtr = getActiveSequencer(voiceIndex);
    if (!seqPtr) return;

    // Get VoiceSystem state for this voice
    VoiceState& voiceState = voiceSystem.getVoiceState(voiceIndex);

    bool parametersWereUpdated = false;
    const ParamButtonMapping* heldMapping = getHeldParameterButton(uiState);

    if (heldMapping) {
        // GATE-CONTROLLED NOTE PROGRAMMING: Restrict note parameter editing
        if (heldMapping->paramId == ParamId::Note) {
            float gateValue = seqPtr->getStepParameterValue(ParamId::Gate, stepToUpdate);
            if (gateValue <= 0.5f) { // Gate is LOW (0.0)
                return; // Prevent note changes on inactive steps
            }
        }

        float valueToSet = mapNormalizedValueToParamRange(heldMapping->paramId, normalized_mm_value);
        seqPtr->setStepParameterValue(heldMapping->paramId, stepToUpdate, valueToSet);
        parametersWereUpdated = true;

        // MIDI CC output for voices 1 and 2 (real-time parameter recording)
        if (voiceIndex < 2) {
            uint8_t midiVoiceId = voiceIndex;
            midiNoteManager.updateParameterCC(midiVoiceId, heldMapping->paramId, valueToSet);
        }
    }

    // Provide immediate audio feedback using VoiceSystem
    if (parametersWereUpdated) {
        updateActiveVoiceState(stepToUpdate, *seqPtr, voiceIndex);
    }
}
```

## Polymetric Examples

Each voice can use different step counts per parameter.

```cpp
// Voice 1: Fast note changes, slow filter modulation
seq1.setParameterStepCount(ParamId::Note, 16);   // Notes: 16-step pattern
seq1.setParameterStepCount(ParamId::Filter, 8);  // Filter: 8-step pattern
seq1.setParameterStepCount(ParamId::Velocity, 4); // Velocity: 4-step pattern

// Voice 2: Slow note progression, fast envelope modulations
seq2.setParameterStepCount(ParamId::Note, 8);    // Notes: 8-step pattern
seq2.setParameterStepCount(ParamId::Attack, 16); // Attack: 16-step pattern
seq2.setParameterStepCount(ParamId::Decay, 16);  // Decay: 16-step pattern

// Voice 3: Complex gate patterns
seq3.setParameterStepCount(ParamId::Gate, 32);   // Gating: 32-step pattern
seq3.setParameterStepCount(ParamId::Note, 16);   // Notes: 16-step pattern

// Voice 4: Minimal pattern for base layer
seq4.setParameterStepCount(ParamId::Note, 4);    // Notes: 4-step pattern
```

## Voice State Updates

Voice updates use the VoiceSystem slot, then the internal VoiceManager ID.

```cpp
// Update voice parameters through VoiceSystem
void updateVoiceParameters(const VoiceState& newState, uint8_t voiceIndex) {
    if (voiceIndex >= VoiceSystem::MAX_VOICES) return;

    // Direct VoiceSystem access to state
    VoiceState& currentState = voiceSystem.getVoiceState(voiceIndex);
    currentState = newState; // Struct assignment for efficiency

    // Gate management through VoiceSystem
    if (newState.isGateHigh && !voiceSystem.getGate(voiceIndex)) {
        voiceSystem.setGate(voiceIndex, true);
        voiceSystem.getGateTimer(voiceIndex).start(newState.gateLengthTicks);
    } else if (!newState.isGateHigh) {
        voiceSystem.setGate(voiceIndex, false);
    }

    // Update voice manager with new state
    uint8_t voiceId = voiceSystem.getVoiceId(voiceIndex);
    voiceManager->updateVoiceState(voiceId, currentState);
}
```

Bulk operations follow the same array shape.

```cpp
// Stop all voices simultaneously
voiceSystem.stopAllGates();

// Set all voices to same volume
voiceSystem.setAllVoiceVolumes(0.7f);

// Process gate timers for all voices
voiceSystem.tickAllGateTimers();

// Check if any voice is active
bool anyVoiceActive = false;
for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++) {
    if (voiceSystem.getGate(i)) {
        anyVoiceActive = true;
        break;
    }
}
```

## UI Integration

Voice parameter buttons edit per-voice configuration through the internal voice ID.

```cpp
// Voice parameter editing (buttons 8-15: envelope, overdrive, wavefolder, filter mode, etc.)
void handleVoiceParameterButton(int voiceIndex, int paramIndex, UIState& state) {
    if (voiceIndex < 0 || voiceIndex > 3) return;

    uint8_t currentVoiceId = voiceSystem.getVoiceId(voiceIndex);
    VoiceConfig* config = voiceManager->getVoiceConfig(currentVoiceId);

    switch (paramIndex) {
        case 8: // Toggle envelope per voice
            config->hasEnvelope = !config->hasEnvelope;
            break;
        case 9: // Cycle filter modes per voice
            config->filterMode = static_cast<daisysp::LadderFilter::FilterMode>(
                (static_cast<uint8_t>(config->filterMode) + 1) % 5);
            break;
        // ... additional voice parameters
    }

    // Apply configuration changes
    voiceManager->setVoiceConfig(currentVoiceId, *config);
}
```

The LED matrix can show active voice steps from the same VoiceSystem state.

```cpp
// Display current step states for all active voices
void updateLEDMatrixForAllVoices() {
    for (uint8_t voiceIndex = 0; voiceIndex < VoiceSystem::MAX_VOICES; voiceIndex++) {
        if (voiceSystem.getVoiceState(voiceIndex).isGateHigh) {
            Sequencer* seq = getActiveSequencer(voiceIndex);
            uint8_t currentStep = seq->getCurrentStep();

            // Light corresponding LED column for this voice
            ledMatrix.setLED(voiceIndex, currentStep % 16, CRGB::White);
        }
    }
}
```

## Performance Notes

- Static allocation keeps sequencer memory predictable.
- VoiceSystem arrays keep voice state close together.
- One clock source advances all four sequencers.
- Gate timers are centralized.
- Sequencer advancement stays in one thread context.
- Cross-core audio/UI state still needs careful synchronization.

Keep the timing order plain. Advance sequencers. Apply AS5600 base values. Apply AS5600 delay values. Update synth state and store it.
