# VoiceSystem Architecture Documentation

## Overview

The VoiceSystem is a centralized architecture for managing multiple synthesizer voices in the Pico2Seq project. It replaces the previous approach of using individual variables for each voice with a unified, array-based system that improves maintainability, scalability, and code consistency.

## Architecture Design

### Core Structure

```cpp
struct VoiceSystem {
    static const uint8_t MAX_VOICES = 4;
    
    // Core voice data arrays
    uint8_t voiceIds[MAX_VOICES];
    VoiceState voiceStates[MAX_VOICES];
    bool gates[MAX_VOICES];
    GateTimer gateTimers[MAX_VOICES];
    
    // Safe accessor methods
    uint8_t getVoiceId(uint8_t index) const;
    VoiceState& getVoiceState(uint8_t index);
    const VoiceState& getVoiceState(uint8_t index) const;
    bool getGate(uint8_t index) const;
    void setGate(uint8_t index, bool state);
    GateTimer& getGateTimer(uint8_t index);
    
    // Helper functions for common operations
    void muteAllVoices();
    void unmuteAllVoices();
    void setAllVoiceVolumes(float volume);
    void stopAllGates();
    void tickAllGateTimers();
};
```

### Design Principles

1. **Centralization**: All voice-related data consolidated in one structure
2. **Array-based Access**: Consistent indexing eliminates conditional branching
3. **Type Safety**: Bounds checking and consistent data types
4. **Scalability**: Easy to modify voice count by changing MAX_VOICES
5. **Encapsulation**: Safe accessor methods prevent direct array access
6. **Helper Functions**: Common operations implemented as reusable methods

## Migration from Legacy System

### Before: Individual Variables

The legacy system used separate variables for each voice:

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

### After: VoiceSystem Architecture

The new system uses centralized arrays:

```cpp
// New approach - clean and maintainable
extern VoiceSystem voiceSystem;

// Simple, direct access
uint8_t getCurrentVoiceId(uint8_t voiceIndex) {
    return voiceSystem.getVoiceId(voiceIndex);
}
```

## API Reference

### Accessor Methods

#### `getVoiceId(uint8_t index)`
Returns the voice ID for the specified voice index.
- **Parameters**: `index` - Voice index (0 to MAX_VOICES-1)
- **Returns**: `uint8_t` - Voice ID
- **Safety**: Bounds checking included

#### `getVoiceState(uint8_t index)`
Returns a reference to the voice state for the specified voice.
- **Parameters**: `index` - Voice index (0 to MAX_VOICES-1)
- **Returns**: `VoiceState&` - Reference to voice state
- **Usage**: For reading and modifying voice parameters

#### `getGate(uint8_t index)` / `setGate(uint8_t index, bool state)`
Gets or sets the gate state for a specific voice.
- **Parameters**: `index` - Voice index, `state` - Gate state (true/false)
- **Usage**: For note on/off control

#### `getGateTimer(uint8_t index)`
Returns a reference to the gate timer for the specified voice.
- **Parameters**: `index` - Voice index (0 to MAX_VOICES-1)
- **Returns**: `GateTimer&` - Reference to gate timer
- **Usage**: For timing-based gate control

### Helper Functions

#### `muteAllVoices()` / `unmuteAllVoices()`
Mutes or unmutes all voices simultaneously.
- **Usage**: Global volume control, panic functions
- **Implementation**: Loops through all voices and sets volume to 0.0f or restores previous levels

#### `setAllVoiceVolumes(float volume)`
Sets the same volume level for all voices.
- **Parameters**: `volume` - Volume level (0.0f to 1.0f)
- **Usage**: Global volume adjustment

#### `stopAllGates()`
Stops all gate timers and resets gate states.
- **Usage**: All notes off, panic button functionality
- **Implementation**: Loops through all voices and resets gate timers

#### `tickAllGateTimers()`
Advances all gate timers by one tick.
- **Usage**: Called from main timing loop
- **Implementation**: Efficient loop-based timer advancement

## Integration Points

### UIState Integration

The UIState has been updated to work seamlessly with VoiceSystem:

```cpp
struct UIState {
    static const uint8_t MAX_VOICES = 4;
    uint8_t voicePresetIndices[MAX_VOICES];  // Array-based preset management
    uint8_t selectedVoiceIndex;              // Current voice selection
    // ... other UI state variables
};
```

**Benefits:**
- Consistent indexing between UI and voice systems
- Simplified preset management
- Scalable voice selection

### MIDI System Integration

MIDI handling has been updated to use VoiceSystem:

```cpp
// MIDI note handling with VoiceSystem
void handleMidiNoteOn(uint8_t note, uint8_t velocity, uint8_t voiceIndex) {
    VoiceState& state = voiceSystem.getVoiceState(voiceIndex);
    state.noteIndex = note;
    state.velocityLevel = velocity / 127.0f;
    voiceSystem.setGate(voiceIndex, true);
}

void handleMidiNoteOff(uint8_t voiceIndex) {
    voiceSystem.setGate(voiceIndex, false);
}
```

### Display System Integration

OLED and LED displays now use VoiceSystem for voice information:

```cpp
// Display current voice information
void displayVoiceInfo(uint8_t selectedVoice) {
    uint8_t voiceId = voiceSystem.getVoiceId(selectedVoice);
    const VoiceState& state = voiceSystem.getVoiceState(selectedVoice);
    uint8_t presetIndex = uiState.voicePresetIndices[selectedVoice];
    
    // Display voice information...
}
```

## Performance Considerations

### Memory Usage
- **Reduced Footprint**: Eliminates duplicate variable declarations
- **Cache Efficiency**: Array-based storage improves memory locality
- **Stack Usage**: Reduced function call overhead with direct array access

### CPU Performance
- **Loop Optimization**: Helper functions use efficient loops instead of conditional branches
- **Reduced Branching**: Direct array access eliminates switch statements
- **Bulk Operations**: Operations on all voices can be vectorized

### Real-time Constraints
- **Predictable Timing**: Loop-based operations have consistent execution time
- **Atomic Operations**: Helper functions can be made atomic for thread safety
- **Interrupt Safety**: Array access is interrupt-safe with proper bounds checking

## Usage Examples

### Basic Voice Access

```cpp
// Get voice information
uint8_t voiceId = voiceSystem.getVoiceId(0);  // Get first voice ID
VoiceState& state = voiceSystem.getVoiceState(0);  // Get first voice state

// Modify voice parameters
state.noteIndex = 24.0f;  // Set note
state.velocityLevel = 0.8f;  // Set velocity
voiceSystem.setGate(0, true);  // Turn on gate
```

### Bulk Operations

```cpp
// Mute all voices for silence
voiceSystem.muteAllVoices();

// Set all voices to same volume
voiceSystem.setAllVoiceVolumes(0.7f);

// Stop all playing notes
voiceSystem.stopAllGates();

// Advance all gate timers (called from main loop)
voiceSystem.tickAllGateTimers();
```

### Loop-based Processing

```cpp
// Process all voices in a loop
for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++) {
    VoiceState& state = voiceSystem.getVoiceState(i);
    
    // Update voice parameters based on sequencer
    if (sequencer.isStepActive(i)) {
        state.noteIndex = sequencer.getStepNote(i);
        state.velocityLevel = sequencer.getStepVelocity(i);
        voiceSystem.setGate(i, true);
    }
}
```

## Benefits and Improvements

### Code Quality
1. **Reduced Duplication**: Eliminated repetitive voice handling code
2. **Improved Readability**: Clear, consistent access patterns
3. **Better Maintainability**: Changes only need to be made in one place
4. **Enhanced Testability**: Easier to unit test with centralized structure

### Scalability
1. **Easy Voice Count Changes**: Modify MAX_VOICES constant
2. **Consistent Scaling**: All systems scale together automatically
3. **Future-proof Design**: Ready for additional voice features

### Performance
1. **Reduced Memory Usage**: Eliminated duplicate variables
2. **Improved Cache Performance**: Array-based storage
3. **Faster Bulk Operations**: Loop-based helper functions
4. **Predictable Timing**: Consistent execution patterns

### Development Experience
1. **Simplified Debugging**: Single point of voice state inspection
2. **Easier Feature Addition**: Consistent patterns for new functionality
3. **Reduced Bugs**: Eliminates index-related errors
4. **Better Documentation**: Centralized API reference

## Future Enhancements

### Potential Extensions
1. **Dynamic Voice Allocation**: Runtime voice count adjustment
2. **Voice Grouping**: Logical grouping of voices for complex arrangements
3. **Voice Templates**: Predefined voice configurations
4. **Performance Monitoring**: Built-in performance metrics

### Compatibility
The VoiceSystem is designed to be:
- **Backward Compatible**: Existing voice configurations work unchanged
- **Forward Compatible**: Ready for future voice system enhancements
- **Platform Independent**: Works across different microcontroller platforms

## Conclusion

The VoiceSystem architecture represents a significant improvement in the Pico2Seq codebase, providing better maintainability, performance, and scalability while reducing code complexity. The centralized approach eliminates common sources of bugs and makes the system easier to understand and extend.