# MIDI Module Documentation

## Overview

The `src/midi` folder contains the MIDI communication system for the PicoMudrasSequencer. This module handles MIDI note events, continuous controller (CC) messages, and USB MIDI interface management. It provides both note-on/note-off functionality and real-time parameter control via MIDI CC messages.

## Key Components

### 1. MidiCCConfig.h - CC Configuration Constants

**CC Number Mapping:**
- Voice 1: CC71-74 (Octave=71, Decay=72, Attack=73, Filter=74)
- Voice 2: CC75-78 (Octave=75, Decay=76, Attack=77, Filter=78)
- Voice 3: CC79-82 (Octave=79, Decay=80, Attack=81, Filter=82)
- Voice 4: CC83-86 (Octave=83, Decay=84, Attack=85, Filter=86)

**Note:** The system now supports up to 4 voices through the VoiceSystem architecture, with CC mappings extended accordingly.

**Transmission Settings:**
- Minimum interval: 10ms between CC transmissions
- Rate limiting: Enabled to prevent MIDI spam
- Channel: All messages on MIDI channel 1

**Value Scaling:**
- Parameter range: 0.0-1.0 (normalized)
- MIDI range: 0-127
- Linear scaling for all parameters

### 2. MidiManager.h - Core MIDI Interface

**MidiNoteManager Class:**
- Handles multi-voice MIDI note tracking through VoiceSystem integration
- Manages monophonic behavior per voice (up to 4 voices)
- Synchronizes note events with gate timing
- Uses centralized VoiceSystem for voice state management

**Key Data Structures:**

```cpp
struct MidiNoteTracker {
    volatile int8_t activeMidiNote;      // Currently playing note (-1 = none)
    volatile uint8_t activeVelocity;     // Note velocity
    volatile uint8_t activeChannel;      // MIDI channel (1-16)
    volatile MidiNoteState state;        // Note state tracking
    volatile bool gateActive;            // Gate synchronization
    volatile uint16_t gateStartTick;     // Timing synchronization
};

struct CCParameterState {
    float lastValue;                     // Last normalized parameter value
    uint8_t lastMidiValue;               // Last transmitted MIDI value
    bool hasChanged;                     // Change detection flag
    unsigned long lastTransmissionTime;  // Rate limiting timestamp
};
```

### 3. MidiManager.cpp - Implementation

**Core Functions:**
- `noteOn()` - Send MIDI note-on with gate duration
- `noteOff()` - Send MIDI note-off for voice
- `updateTiming()` - Handle gate expiration
- `allNotesOff()` - Emergency note shutdown
- `updateParameterCC()` - Send parameter changes as CC

**Thread Safety:**
- Atomic updates for concurrent access
- Volatile state variables for multi-core safety
- Begin/end atomic update methods

## MIDI Note Management

### Monophonic Behavior
- Each voice supports only one active note
- New notes automatically terminate previous notes
- Proper note-off/on pairing maintained

### Gate Synchronization
- Notes can be tied to gate timing
- Automatic note-off when gate expires
- Configurable gate durations per note

### Timing Control
```cpp
void updateTiming(uint16_t currentTick) {
    // Check all voices for expired gates using VoiceSystem
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++) {
        if (voiceSystem.getGateTimer(i).isExpired()) {
            processNoteOff(i);
        }
    }
}
```

### VoiceSystem Integration

The MIDI system now integrates with the centralized VoiceSystem architecture:

```cpp
// Access voice states through VoiceSystem
VoiceState& voiceState = voiceSystem.getVoiceState(voiceIndex);

// Gate management through VoiceSystem
voiceSystem.setGate(voiceIndex, true);  // Note on
voiceSystem.setGate(voiceIndex, false); // Note off

// Bulk operations for all voices
voiceSystem.stopAllGates();  // All notes off
```

## MIDI CC System

### Parameter Mapping
- **Filter**: CC74 (Voice 1), CC78 (Voice 2)
- **Attack**: CC73 (Voice 1), CC77 (Voice 2)
- **Decay**: CC72 (Voice 1), CC76 (Voice 2)
- **Octave**: CC71 (Voice 1), CC75 (Voice 2)

### Change Detection
- Prevents MIDI spam with intelligent filtering
- Only transmits when values change significantly
- Rate limiting to respect minimum intervals

### Value Processing
```cpp
void sendCCIfChanged(uint8_t voiceId, ParamId paramId, float value) {
    // Clamp to valid range
    float clampedValue = clampParameterValue(value);
    
    // Check transmission conditions
    if (!shouldTransmitCC(voiceId, paramId, clampedValue)) {
        return;
    }
    
    // Scale and send
    uint8_t midiValue = scaleParameterToMidi(paramId, clampedValue);
    sendCC(ccNumber, midiValue);
}
```

## Dependencies

- **MIDI Library**: Arduino MIDI library for USB interface
- **Adafruit TinyUSB**: USB MIDI hardware abstraction
- **Sequencer Module**: Note value and timing source
- **Scale Module**: Note-to-MIDI conversion tables

## Usage Examples

### Basic Note Control
```cpp
// Play note on Voice 1
midiNoteManager.noteOn(0, 60, 100, 1, 480);  // C4, velocity 100, 480 ticks

// Turn off Voice 1
midiNoteManager.noteOff(0);

// Check if Voice 2 is active
if (midiNoteManager.isNoteActive(1)) {
    int note = midiNoteManager.getActiveNote(1);
}
```

### Parameter CC Updates
```cpp
// Update filter cutoff for Voice 1
midiNoteManager.updateParameterCC(0, ParamId::Filter, 0.75f);

// Update attack time for Voice 2  
midiNoteManager.updateParameterCC(1, ParamId::Attack, 0.3f);
```

### Emergency Control
```cpp
// Stop all notes immediately
midiNoteManager.emergencyStop();

// Clean shutdown
midiNoteManager.allNotesOff();
```

## Hardware Integration

### USB MIDI Interface
- **Library**: Adafruit TinyUSB
- **Interface**: `midi::MidiInterface<midi::SerialMIDI<Adafruit_USBD_MIDI>>`
- **Channel**: Fixed to MIDI channel 1
- **Buffer**: Automatic USB transmission queue

### Pico Hardware
- **USB Port**: Native USB with TinyUSB stack
- **Real-time**: Interrupt-driven MIDI transmission
- **Thread Safety**: Multi-core compatible design

## Performance Characteristics

- **Low Latency**: Direct USB transmission
- **Efficient Filtering**: Change detection prevents spam
- **Rate Limited**: 10ms minimum CC interval
- **Thread Safe**: Atomic operations for multi-core access

## Integration Points

- **Sequencer Module**: Receives note triggers and gate events
- **Voice Module**: Provides parameter values for CC transmission
- **Scale Module**: Supplies MIDI note number mappings
- **UI System**: Parameter changes trigger CC updates

## Error Handling

- **Parameter Validation**: Range checking for all inputs
- **Note Range Limits**: MIDI note numbers 0-127
- **CC Value Limits**: MIDI CC values 0-127
- **Emergency Stop**: Panic button functionality

## File Structure

```
src/midi/
├── MidiCCConfig.h          # CC configuration constants
├── MidiManager.cpp         # Implementation with note management
├── MidiManager.h           # Interface and data structures
└── README.md              # Module overview and usage
```

## Development Notes

- **Modular Design**: Extracted from main .ino for better organization
- **Comprehensive Comments**: Extensive inline documentation
- **Legacy Compatibility**: Maintains existing API while adding features
- **Extensible Architecture**: Easy to add new CC parameters or voices

## Troubleshooting

- **No MIDI Output**: Check USB connection and TinyUSB initialization
- **Stuck Notes**: Use `allNotesOff()` or `emergencyStop()` methods
- **CC Spam**: Verify change detection thresholds in config
- **Timing Issues**: Check gate duration calculations in sequencer

## Future Enhancements

- **Multi-Channel Support**: Expand beyond channel 1
- **NRPN Support**: Add NRPN parameter control
- **MIDI Clock**: Add tempo synchronization
- **Sysex Messages**: Add system exclusive support