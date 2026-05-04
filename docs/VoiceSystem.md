# VoiceSystem

A small map for four voices.
No ceremony. Just IDs, state, and the gate timers that still need to tick.

`VoiceSystem` is the project-level structure that keeps voice lookup and voice state in one place. It replaces scattered globals with indexed storage and gives UI, MIDI, OLED, LED, and sketch code one shared way to address a voice.

## Core Structure

```cpp
struct VoiceSystem {
    static constexpr uint8_t MAX_VOICES = 4;

    // Core voice data arrays
    uint8_t voiceIds[MAX_VOICES] = {0, 0, 0, 0};
    VoiceState voiceStates[MAX_VOICES];

    // Gate state is only used for voices 0 and 1
    volatile bool gates[2] = {false, false};
    GateTimer gateTimers[2];

    // Safe accessor methods
    uint8_t getVoiceId(uint8_t voiceIndex) const;
    inline int16_t getVoiceIdFromUIIndex(int uiIndex) const;
    void setVoiceId(uint8_t voiceIndex, uint8_t voiceId);

    VoiceState& getVoiceState(uint8_t voiceIndex);
    const VoiceState& getVoiceState(uint8_t voiceIndex) const;
    volatile bool& getGate(uint8_t voiceIndex);
    GateTimer& getGateTimer(uint8_t voiceIndex);

    // Gate helpers
    void stopAllGates();
    void tickAllGateTimers();
};
```

## Design Rules

- Voice indices are zero-based: `0..3`.
- `MAX_VOICES` is `4`.
- `voiceIds[]` stores IDs returned by `VoiceManager`.
- `voiceStates[]` stores one `VoiceState` per voice.
- `gates[]` and `gateTimers[]` are only for voices `0` and `1`.
- Invalid `getVoiceState(...)` access falls back to index `0`.
- Invalid gate or gate-timer access returns a dummy reference.
- `getVoiceIdFromUIIndex(...)` returns `-1` for invalid or unassigned UI indices.

The split between four voices and two gate timers is intentional. Voices `0` and `1` use the special gated sequencer path. Voices `2` and `3` still have voice state and audio processing, but not entries in `gates[]` or `gateTimers[]`.

## Before And After

Before, voice data lived in separate names:

```cpp
// Legacy approach - repetitive and hard to maintain
extern uint8_t voice1Id, voice2Id, voice3Id, voice4Id;
extern VoiceState voiceState1, voiceState2, voiceState3, voiceState4;
extern bool GATE1, GATE2, GATE3, GATE4;
extern GateTimer gateTimer1, gateTimer2, gateTimer3, gateTimer4;

// Accessing voices required conditional logic
uint8_t getCurrentVoiceId(uint8_t voiceIndex) {
    switch(voiceIndex) {
        case 0: return voice1Id;
        case 1: return voice2Id;
        case 2: return voice3Id;
        case 3: return voice4Id;
        default: return 0;
    }
}
```

Now the index is the path:

```cpp
// New approach - clean and maintainable
extern VoiceSystem voiceSystem;

// Simple, direct access
uint8_t getCurrentVoiceId(uint8_t voiceIndex) {
    return voiceSystem.getVoiceId(voiceIndex);
}
```

## Accessors

### `getVoiceId(uint8_t voiceIndex)`

Returns the `VoiceManager` voice ID for `voiceIndex`.

- Valid range: `0..3`.
- Invalid range: returns `0`.
- Use when the caller already has a valid internal voice index.

### `getVoiceIdFromUIIndex(int uiIndex)`

Maps a zero-based UI voice index to a `VoiceManager` voice ID.

- Valid input range: `0..3`.
- Invalid or unassigned input: returns `-1`.
- Use at UI/event boundaries when a missing voice should be explicit.

### `setVoiceId(uint8_t voiceIndex, uint8_t voiceId)`

Stores a `VoiceManager` voice ID in `voiceIds[]`.

- Valid range: `0..3`.
- Invalid range: ignored.

### `getVoiceState(uint8_t voiceIndex)`

Returns the `VoiceState` for a voice.

- Valid range: `0..3`.
- Invalid range: returns `voiceStates[0]`.

### `getGate(uint8_t voiceIndex)`

Returns the gate flag for a gated voice.

- Valid range: `0..1`.
- Invalid range: returns a dummy volatile bool reference.

### `getGateTimer(uint8_t voiceIndex)`

Returns the gate timer for a gated voice.

- Valid range: `0..1`.
- Invalid range: returns a dummy `GateTimer`.

## Gate Helpers

### `stopAllGates()`

Stops the two tracked gates.

It loops over voices `0` and `1`, clears `gates[i]`, and stops each matching `GateTimer`.

### `tickAllGateTimers()`

Ticks the two tracked gate timers.

When a timer expires and its gate is high, the helper clears that gate.

## UIState Integration

`UIState` mirrors the same four-voice indexing.

```cpp
struct UIState {
    static const uint8_t MAX_VOICES = 4;
    uint8_t voicePresetIndices[MAX_VOICES];  // Array-based preset management
    uint8_t selectedVoiceIndex;              // Current voice selection
    // ... other UI state variables
};
```

`selectedVoiceIndex` is the active voice index for editing. `voicePresetIndices[]` tracks the current preset index for each voice. `voiceSelected` is used by preset assignment so a preset button does not apply until the user has selected a voice or cycled into one.

## MIDI Integration

MIDI-style updates use the selected voice index to reach the matching `VoiceState`.

```cpp
// MIDI note handling with VoiceSystem
void handleMidiNoteOn(uint8_t note, uint8_t velocity, uint8_t voiceIndex) {
    VoiceState& state = voiceSystem.getVoiceState(voiceIndex);
    state.noteIndex = note;
    state.velocityLevel = velocity / 127.0f;
    if (voiceIndex < 2) {
        voiceSystem.getGate(voiceIndex) = true;
    }
}

void handleMidiNoteOff(uint8_t voiceIndex) {
    if (voiceIndex < 2) {
        voiceSystem.getGate(voiceIndex) = false;
    }
}
```

Keep gate writes inside the valid range, `0..1`.

## Display Integration

OLED and LED code use `selectedVoiceIndex` for display state and `VoiceSystem` for voice ID and voice state lookup.

```cpp
// Display current voice information
void displayVoiceInfo(uint8_t selectedVoice) {
    uint8_t voiceId = voiceSystem.getVoiceId(selectedVoice);
    const VoiceState& state = voiceSystem.getVoiceState(selectedVoice);
    uint8_t presetIndex = uiState.voicePresetIndices[selectedVoice];

    // Display voice information...
}
```

The current branch uses zero-based labels in this path. Keep display strings aligned with the branch behavior.

## Usage Examples

```cpp
// Get voice information
uint8_t voiceId = voiceSystem.getVoiceId(0);  // Get first voice ID
VoiceState& state = voiceSystem.getVoiceState(0);  // Get first voice state

// Modify voice parameters
state.noteIndex = 24.0f;  // Set note
state.velocityLevel = 0.8f;  // Set velocity
voiceSystem.getGate(0) = true;  // Turn on gate for a gated voice
```

```cpp
// Stop all tracked gates
voiceSystem.stopAllGates();

// Advance tracked gate timers (called from timing loop)
voiceSystem.tickAllGateTimers();
```

```cpp
// Process all voice states in a loop
for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++) {
    VoiceState& state = voiceSystem.getVoiceState(i);

    // Update voice parameters based on sequencer state.
    // Gate writes must stay inside the valid gate range.
    if (i < 2 && sequencer.isStepActive(i)) {
        state.noteIndex = sequencer.getStepNote(i);
        state.velocityLevel = sequencer.getStepVelocity(i);
        voiceSystem.getGate(i) = true;
    }
}
```

## Benefits

- Voice ID lookup has one home.
- Voice state storage is four-wide and loop-friendly.
- UI preset state uses the same index model.
- Gated voices are explicit.
- Invalid index behavior is visible in the API.
- Future refactors can touch one structure instead of many globals.

## Constraints

- `VoiceSystem` is not a dynamic allocator.
- It does not own `Voice` objects. `VoiceManager` does that.
- It does not persist presets.
- It does not make cross-core state changes safe by itself.
- It does not provide gate arrays for voices `2` and `3`.

## Gate Sequence Length Mode

Gate Sequence Length Mode changes the selected voice's Gate track length from `2..16` steps.

Activation:

- Hold the AS5600 control button to enter the mode.
- Release the button to exit.
- Toggling slide mode also exits the mode.

Controls:

- While held, press any step button `1..16` to set the Gate sequence length for the selected voice.
- Values are clamped to `2..16`.

LED feedback:

- The selected voice row blinks up to the current Gate length.
- The non-selected row is dimmed.
- Length changes trigger `uiState.resetStepsLightsFlag`.

OLED feedback:

- Header: `Gate Len Mode`
- Selected voice number
- Current Gate length
- Horizontal fill bar scaled to 16 steps

Implementation:

- UI handling: `src/ui/UIEventHandler.cpp`
- LED rendering: `src/LEDMatrix/LEDMatrixFeedback.cpp`
- OLED rendering: `src/OLED/oled.cpp`
