# Sequencer Module Documentation

## Overview and Responsibilities

The Sequencer module is the core sequencing engine of the Pico2Seq synthesizer, implementing a **polyrhythmic step sequencer** with independent parameter tracks. It provides the foundation for creating complex musical patterns where different synthesis parameters (Note, Velocity, Filter, Attack, Decay, Octave, GateLength, Gate, Slide) can evolve at different rates simultaneously.

### Key Features

- **Polyrhythmic Sequencing**: Each parameter operates as an independent track with configurable step counts, enabling complex evolving patterns
- **Real-time Parameter Recording**: Live sensor-based parameter capture during playback
- **Thread-safe Operation**: Dual-core architecture support with proper synchronization
- **Dual Voice Support**: Independent sequencing for layered compositions
- **Gate-controlled Programming**: Intelligent note parameter editing based on gate states
- **Envelope Management**: Built-in envelope triggering and note duration handling
- **Hardware Integration**: Direct control of gate outputs and step clock signals

## Public Classes/Structs and Method Signatures

### Sequencer Class

The main sequencer class that orchestrates all sequencing operations.

```cpp
class Sequencer {
public:
    // Construction
    Sequencer();
    Sequencer(uint8_t channel);
    ~Sequencer() = default;

    // Parameter Management
    void initializeParameters();
    void resetAllSteps();
    void playStepNow(uint8_t stepIdx, VoiceState* voiceState);
    void toggleStep(uint8_t stepIdx);
    float getStepParameterValue(ParamId id, uint8_t stepIdx) const;
    void setStepParameterValue(ParamId id, uint8_t stepIdx, float value);
    uint8_t getParameterStepCount(ParamId id) const;
    void setParameterStepCount(ParamId id, uint8_t steps);

    // Sequencer Control
    void start() { running = true; }
    void stop() { running = false; }
    void reset();
    void randomizeParameters();

    // Note/Envelope Handling
    void startNote(uint8_t note, uint8_t velocity, uint16_t duration);
    void handleNoteOff(VoiceState* voiceState);
    void tickNoteDuration(VoiceState* voiceState);
    bool isNotePlaying() const;
    void setMidiNoteOffCallback(void (*callback)(uint8_t note, uint8_t channel));

    // Core Sequencing Method
    void advanceStep(uint8_t current_uclock_step, int mm_distance,
                    bool is_note_button_held, bool is_velocity_button_held,
                    bool is_filter_button_held, bool is_attack_button_held,
                    bool is_decay_button_held, bool is_octave_button_held,
                    int current_selected_step_for_edit, VoiceState *voiceState);

    // UIState Integration
    void advanceStep(uint8_t current_uclock_step, int mm_distance,
                    const UIState& uiState, VoiceState *voiceState);

    // State Queries
    uint8_t getCurrentStep() const { return currentStep; }
    uint8_t getCurrentStepForParameter(ParamId paramId) const;
    Step getStep(uint8_t stepIdx) const;
    bool isRunning() const { return running; }
};
```

### EnvelopeController Class

Simple envelope state manager for note triggering and release.

```cpp
class EnvelopeController {
public:
    void trigger() { triggered = true; released = false; }
    void release() { triggered = false; released = true; }
    bool isTriggered() const { return triggered; }
    bool isReleased() const { return released; }
};
```

### NoteDurationTracker Class

Tracks note duration in sequencer pulses with automatic deactivation.

```cpp
class NoteDurationTracker {
public:
    void start(uint16_t duration) { counter = duration; active = true; }
    void tick() { if (active && counter > 0) { --counter; if (counter == 0) active = false; } }
    bool isActive() const { return active && counter > 0; }
    void reset() { counter = 0; active = false; }
};
```

### ParameterManager Class

Thread-safe parameter management with validation and clamping.

```cpp
class ParameterManager {
public:
    ParameterManager();
    void init();
    void setStepCount(ParamId id, uint8_t steps);
    uint8_t getStepCount(ParamId id) const;
    float getValue(ParamId id, uint8_t stepIdx) const;
    void setValue(ParamId id, uint8_t stepIdx, float value);
    void randomizeParameters();
};
```

## Key Constants and Data Structures

### Timing Constants

```cpp
namespace SequencerConstants {
    static constexpr uint16_t PULSES_PER_QUARTER_NOTE_PPQN = 480;
    static constexpr uint8_t PULSES_PER_SEQUENCER_STEP_TICKS = PULSES_PER_QUARTER_NOTE_PPQN / 4;
    static constexpr uint8_t MAX_STEPS_COUNT = 64;
    static constexpr uint8_t MIN_STEPS_COUNT = 2;
    static constexpr uint8_t DEFAULT_STEPS_COUNT = 16;
    static constexpr uint16_t DEFAULT_GATE_LENGTH_TICKS = PULSES_PER_SEQUENCER_STEP_TICKS / 2;
    static constexpr uint16_t MIN_GATE_LENGTH_TICKS = 1;
    static constexpr uint16_t MAX_GATE_LENGTH_TICKS = PULSES_PER_SEQUENCER_STEP_TICKS;
}
```

### Parameter Enumeration

```cpp
enum class ParamId : uint8_t {
    Note,       // 0.0-21.0 (Scale step index)
    Velocity,   // 0.0-1.0 (Voice amplitude)
    Filter,     // 0.0-1.0 (Filter cutoff frequency)
    Attack,     // 0.0-1.0 (Envelope attack time in seconds)
    Decay,      // 0.0-1.0 (Envelope decay time in seconds)
    Octave,     // 0.0-1.0 (Octave offset: 0.0=C2, 0.5=C3, 1.0=C4)
    GateLength, // 0.001-1.0 (Gate duration as fraction of step)
    Gate,       // boolean (Gate on/off state)
    Slide,      // boolean (Portamento enable)
    Count       // Total parameter count for array sizing
};
```

### VoiceState Structure

Contains all parameters needed by the audio synthesis engine.

```cpp
struct VoiceState {
    float noteIndex = 0.0f;                    // Scale step index (0-21) for scale array lookup
    float velocityLevel = 0.8f;                // Voice amplitude (0.0-1.0)
    float filterCutoff = 0.37f;                // Filter cutoff frequency (0.0-1.0)
    float attackTimeSeconds = 0.01f;           // Envelope attack time (0.0-1.0 seconds)
    float decayTimeSeconds = 0.01f;            // Envelope decay time (0.0-1.0 seconds)
    float octaveOffset = 0.0f;                 // Octave offset (0.0=C2, 0.5=C3, 1.0=C4)
    uint16_t gateLengthTicks = SequencerConstants::DEFAULT_GATE_LENGTH_TICKS; // Gate duration in clock ticks
    bool isGateHigh = false;                   // Voice on/off state
    bool hasSlide = false;                     // Portamento enable flag
    bool shouldRetrigger = false;              // Envelope restart command flag
};
```

### Step Structure

Contains all automatable parameters for a single sequencer step.

```cpp
struct Step {
    float noteIndex = 0.0f;                    // Scale step index (0-21)
    float velocityLevel = 0.5f;                // Voice amplitude (0.0-1.0)
    float filterCutoff = 0.5f;                 // Filter cutoff frequency (0.0-1.0)
    float attackTimeSeconds = 0.04f;           // Envelope attack time (0.0-1.0 seconds)
    float decayTimeSeconds = 0.1f;             // Envelope decay time (0.0-1.0 seconds)
    float octaveOffset = 0.0f;                 // Octave offset (0.0=C2, 0.5=C3, 1.0=C4)
    uint16_t gateLengthTicks = SequencerConstants::DEFAULT_GATE_LENGTH_TICKS; // Gate duration in clock ticks
    bool isGateActive = false;                 // Step active/inactive state
    bool hasSlide = false;                     // Portamento enable for this step
};
```

### ParameterTrack Template

Fixed-size parameter automation track with step wrapping support.

```cpp
template <uint8_t MAX_SIZE>
struct ParameterTrack {
    float parameterValues[MAX_SIZE];          // Parameter values for each step
    uint8_t currentStepCount;                 // Current sequence length (2-64 steps)
    float defaultParameterValue;              // Default value for new steps

    void init(float defaultValue);
    float getValue(uint8_t stepIndex) const;
    void setValue(uint8_t stepIndex, float newValue);
    void resize(uint8_t newStepCount);
};
```

### GateTimer Structure

Manages gate duration timing with automatic turn-off.

```cpp
struct GateTimer {
    volatile bool isActive = false;                     // Timer active state
    volatile uint16_t ticksRemaining = 0;               // Clock ticks remaining until gate off
    volatile uint32_t totalTicksProcessed = 0;          // Debug counter for diagnostics

    void start(uint16_t durationTicks) volatile;
    void tick() volatile;
    void stop() volatile;
    bool isExpired() const volatile;
};
```

## Typical Lifecycle and Usage Pattern

### Initialization

```cpp
// Create sequencer instance
Sequencer sequencer(channel);

// Initialize all parameters with defaults
sequencer.initializeParameters();

// Set up MIDI callback for note-off events
sequencer.setMidiNoteOffCallback(midiNoteOffCallback);
```

### Main Loop Integration

```cpp
// In the main loop, advance the sequencer on each step
void loop() {
    // Get current step from UClock
    uint8_t currentStep = uClock.getCurrentStep();

    // Get sensor distance for real-time recording
    int distance = distanceSensor.getDistance();

    // Get UI state for button presses
    UIState uiState = uiHandler.getUIState();

    // Prepare voice state for audio synthesis
    VoiceState voiceState;

    // Advance sequencer (polyrhythmic parameter advancement)
    sequencer.advanceStep(currentStep, distance, uiState, &voiceState);

    // Process voice state in audio synthesis engine
    audioEngine.processVoice(voiceState);

    // Handle note duration timing
    sequencer.tickNoteDuration(&voiceState);
}
```

### Step Preview Mode

```cpp
// Preview a specific step without advancing the sequence
VoiceState previewState;
sequencer.playStepNow(stepIndex, &previewState);
audioEngine.processVoice(previewState);
```

### Parameter Editing

```cpp
// Set parameter value for a specific step
sequencer.setStepParameterValue(ParamId::Note, stepIndex, 12.0f);

// Get current parameter value
float currentValue = sequencer.getStepParameterValue(ParamId::Velocity, stepIndex);

// Toggle step gate
sequencer.toggleStep(stepIndex);

// Set parameter track length (enables polyrhythms)
sequencer.setParameterStepCount(ParamId::Filter, 8); // Filter changes every 8 steps
sequencer.setParameterStepCount(ParamId::Note, 16);  // Note changes every 16 steps
```

## Integration Points

### MIDI Integration

The sequencer integrates with MIDI through callback functions for note-off events:

```cpp
void midiNoteOffCallback(uint8_t note, uint8_t channel) {
    // Handle MIDI note-off messages
    midiManager.sendNoteOff(note, channel);
}
```

### Voice/Audio Integration

The sequencer communicates with the audio synthesis engine through the VoiceState structure:

```cpp
// VoiceState is populated by the sequencer and consumed by audio engine
VoiceState voiceState; // Contains note, velocity, filter, envelope, etc.
audioEngine.processVoice(voiceState);
```

### LED Matrix Integration

The sequencer provides step position information for LED matrix feedback:

```cpp
// Current global step position
uint8_t currentStep = sequencer.getCurrentStep();

// Current step position for specific parameter
uint8_t filterStep = sequencer.getCurrentStepForParameter(ParamId::Filter);
```

### Sensor Integration

Real-time parameter recording through distance sensors:

```cpp
// Distance sensor input is mapped to parameter ranges
int mm_distance = distanceSensor.getDistance(); // 0-400mm range
// Automatically handled in advanceStep() method
```

### UI Integration

The sequencer integrates with UI event handling for parameter editing:

```cpp
// Button states determine which parameters are recorded in real-time
bool noteButtonHeld = uiState.parameterButtonHeld[static_cast<int>(ParamId::Note)];
bool velocityButtonHeld = uiState.parameterButtonHeld[static_cast<int>(ParamId::Velocity)];
```

## Blocking Issues or Assumptions

### Required Global State

The sequencer module has the following external dependencies:

1. **Arduino Library**: Required for `pinMode()`, `digitalWrite()`, and timing functions
2. **UClock Integration**: Depends on external clock source for step timing
3. **Scale System**: Assumes existence of SCALE_STEPS array for note-to-frequency conversion
4. **UIState Structure**: Requires UI state for button press detection
5. **VoiceState Processing**: Assumes audio engine will process VoiceState structures

### Hardware Dependencies

```cpp
// GPIO Pin Requirements
pinMode(10, OUTPUT);  // Voice 1 gate output
pinMode(11, OUTPUT);  // Voice 2 gate output
pinMode(12, OUTPUT);  // Step clock output
```

### Thread Safety Considerations

- ParameterManager uses spin locks for thread-safe access
- GateTimer uses volatile members for interrupt-safe access
- advanceStep() method should be called from a single thread to avoid race conditions

### Memory Constraints

- Fixed-size parameter tracks (64 steps maximum)
- Static allocation for all data structures (no dynamic memory allocation)
- Template-based ParameterTrack to avoid runtime memory allocation

### Timing Assumptions

- Operates at 480 PPQN (Pulses Per Quarter Note)
- Step clock pulse is 120 ticks (480/4)
- Gate length is specified as fraction of step duration
- All timing is based on external UClock step counter

### Parameter Constraints

- Gate-controlled note programming: Note parameters can only be modified on steps with HIGH gates
- Parameter value clamping: All parameter values are automatically clamped to defined ranges
- Binary parameter handling: Gate and Slide parameters are treated as binary (0.0 or 1.0)

This documentation covers all aspects of the sequencer module as requested, including module purpose, public API, timing model, integration points, and any blocking issues or assumptions that developers should be aware of when working with this system.