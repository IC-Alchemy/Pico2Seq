# Voice Module

Four voices, held close to the metal.
Each one is a small synth voice with its own state, preset, sequencer link, and place in the mix.

This document describes the voice module used by Pico2Seq. It covers `Voice`, `VoiceManager`, `VoiceSystem`, presets, scale-aware pitch, and the migration away from scattered per-voice globals.

## Module Shape

The voice module is built from a few fixed parts:

- `Voice` - one synthesizer voice with oscillators, filter, envelope, effects, slide, and scale mapping.
- `VoiceManager` - owns voices, applies configs, attaches sequencers, and mixes active voice output.
- `VoiceSystem` - stores the project-level voice IDs, voice states, and gate timers in arrays.
- `VoicePresets` - returns the built-in voice configurations and applies them to a selected voice.
- `VoiceManagerBuilder` and `VoiceFactory` - setup helpers for common voice-manager layouts.

The project currently uses `VoiceSystem::MAX_VOICES == 4`.

## VoiceSystem

`VoiceSystem` is the shared index map. It replaced separate globals like `voice1Id`, `voice2Id`, `voiceState1`, and `GATE1`.

The main voice data is four-wide. Gate state and gate timers are two-wide because the special sequencer gate path only applies to voices `0` and `1`.

```cpp
struct VoiceSystem {
    static constexpr uint8_t MAX_VOICES = 4;

    uint8_t voiceIds[MAX_VOICES] = {0, 0, 0, 0};
    VoiceState voiceStates[MAX_VOICES];

    // Gate states and timers are only used for voices 0 and 1.
    volatile bool gates[2] = {false, false};
    GateTimer gateTimers[2];

    uint8_t getVoiceId(uint8_t voiceIndex) const;
    inline int16_t getVoiceIdFromUIIndex(int uiIndex) const;
    void setVoiceId(uint8_t voiceIndex, uint8_t voiceId);

    VoiceState& getVoiceState(uint8_t voiceIndex);
    const VoiceState& getVoiceState(uint8_t voiceIndex) const;

    volatile bool& getGate(uint8_t voiceIndex);
    GateTimer& getGateTimer(uint8_t voiceIndex);

    void stopAllGates();
    void tickAllGateTimers();
};
```

Rules:

- Voice indices are zero-based: `0..3`.
- `voiceIds[]` stores IDs returned by `VoiceManager::addVoice(...)`.
- Do not assume `voiceId == voiceIndex`.
- `getVoiceIdFromUIIndex(...)` returns `-1` for invalid or unassigned UI indices.
- Gate access is valid only for voices `0` and `1`; invalid gate access returns a dummy reference.

## UIState Integration

`UIState` stores selected voice and preset data with the same zero-based voice index.

```cpp
struct UIState {
    // Voice preset management using arrays
    static const uint8_t MAX_VOICES = 4;
    uint8_t voicePresetIndices[MAX_VOICES];

    // Other UI state variables...
    uint8_t selectedVoiceIndex;
    // ...
};
```

The settings UI uses `selectedVoiceIndex` for sequencer selection, preset assignment, OLED output, and LED feedback. Preset assignment also checks `voiceSelected` so a preset button does not silently modify the wrong voice.

## VoiceConfig

`VoiceConfig` is the patch. It defines oscillator count, waveforms, filter settings, effect flags, envelope times, and output level.

```cpp
struct VoiceConfig {
    // Oscillator configuration
    uint8_t oscillatorCount = 3;           // Number of oscillators (1-3)
    uint8_t oscWaveforms[3] = {...};       // Waveform types (DaisySP constants)
    float oscAmplitudes[3] = {0.5f, 0.5f, 0.5f};  // Oscillator amplitudes (0.0-1.0)
    float oscDetuning[3] = {0.0f, 0.0f, 0.0f};    // Detuning in semitones (-12.0 to +12.0)
    float oscPulseWidth[3] = {0.5f, 0.5f, 0.5f};  // Pulse width for square waves (0.0-1.0)
    int harmony[3] = {0, 0, 0};             // Harmony intervals in scale steps (-12 to +12)

    // Filter settings
    float filterRes = 0.2f;                // Filter resonance (0.0-1.0)
    float filterDrive = 1.8f;              // Filter drive amount (0.0-10.0)
    float filterPassbandGain = 0.23f;      // Passband gain compensation (0.0-1.0)
    daisysp::LadderFilter::FilterMode filterMode = daisysp::LadderFilter::FilterMode::LP24;
    float highPassFreq = 80.0f;            // High-pass cutoff frequency in Hz (20.0-20000.0)
    float highPassRes = 0.1f;              // High-pass resonance (0.0-1.0)

    // Effects configuration
    bool hasOverdrive = false;             // Enable overdrive effect
    bool hasWavefolder = false;            // Enable wavefolder effect
    bool hasEnvelope = true;               // Enable envelope (recommended: true)
    float overdriveGain = 0.34f;           // Overdrive output gain (0.0-2.0)
    float overdriveDrive = 0.25f;          // Overdrive drive amount (0.0-1.0)
    float wavefolderGain = 3.5f;           // Wavefolder gain (0.0-10.0)
    float wavefolderOffset = 2.0f;         // Wavefolder DC offset (0.0-5.0)

    // Envelope settings
    float defaultAttack = 0.04f;           // Default attack time in seconds (0.001-10.0)
    float defaultDecay = 0.14f;            // Default decay time in seconds (0.001-10.0)
    float defaultSustain = 0.5f;           // Default sustain level (0.0-1.0)
    float defaultRelease = 0.1f;           // Default release time in seconds (0.001-10.0)

    // Voice mixing
    float outputLevel = 0.6f;              // Voice output level (0.0-1.0)
    bool enabled = true;                   // Voice enabled state
};
```

## Voice API

`Voice` owns the DSP state for one configured voice. It processes audio one sample at a time and receives real-time updates through `VoiceState`.

```cpp
class Voice {
public:
    // Constructor/Destructor
    Voice(uint8_t id, const VoiceConfig& config);
    ~Voice() = default;

    // Initialization and configuration
    void init(float sampleRate);
    void setConfig(const VoiceConfig& config);
    const VoiceConfig& getConfig() const;
    VoiceConfig& getConfig();

    // Audio processing
    float process();

    // Parameter updates
    void updateParameters(const VoiceState& newState);

    // Sequencer integration
    void setSequencer(std::unique_ptr<Sequencer> seq);
    void setSequencer(Sequencer* seq);
    Sequencer* getSequencer();

    // State management
    VoiceState& getState();
    const VoiceState& getState() const;
    void setGate(bool gateState);
    bool getGate() const;

    // Filter control
    void setFilterFrequency(float freq);
    float getFilterFrequency() const;

    // Voice identification
    uint8_t getId() const;
    bool isEnabled() const;
    void setEnabled(bool enabled);

    // Frequency control
    void setFrequency(float frequency);
    void setSlideTime(float slideTime);

    // Scale integration
    void setScaleTable(const int (*table)[48], size_t scaleCount);
    void setCurrentScalePointer(const uint8_t* currentScalePtr);
};
```

## VoiceManager API

`VoiceManager` owns the active voices and provides the main control surface for configuration, sequencing, and mixing.

```cpp
class VoiceManager {
public:
    // Constructor
    VoiceManager(uint8_t maxVoices = 8);
    ~VoiceManager() = default;

    // Voice Management
    uint8_t addVoice(const VoiceConfig& config);
    uint8_t addVoice(const std::string& presetName);
    bool removeVoice(uint8_t voiceId);
    void removeAllVoices();

    // Voice Configuration
    bool setVoiceConfig(uint8_t voiceId, const VoiceConfig& config);
    bool setVoicePreset(uint8_t voiceId, const std::string& presetName);
    VoiceConfig* getVoiceConfig(uint8_t voiceId);

    // Voice State Management
    bool updateVoiceState(uint8_t voiceId, const VoiceState& state);
    VoiceState* getVoiceState(uint8_t voiceId);

    // Sequencer Management
    bool attachSequencer(uint8_t voiceId, std::unique_ptr<Sequencer> sequencer);
    bool attachSequencer(uint8_t voiceId, Sequencer* sequencer);
    Sequencer* getSequencer(uint8_t voiceId);

    // Audio Processing
    void init(float sampleRate);
    float processAllVoices();
    float processVoice(uint8_t voiceId);

    // Voice Control
    void enableVoice(uint8_t voiceId, bool enabled = true);
    void disableVoice(uint8_t voiceId);
    bool isVoiceEnabled(uint8_t voiceId) const;

    // Voice Information
    uint8_t getVoiceCount() const;
    uint8_t getMaxVoices() const;
    std::vector<uint8_t> getActiveVoiceIds() const;

    // Memory Management
    size_t getMemoryUsage() const;
    bool hasAvailableSlots() const;

    // Callbacks
    void setVoiceCountCallback(VoiceCountCallback callback);
    void setVoiceUpdateCallback(VoiceUpdateCallback callback);

    // Preset Management
    static std::vector<std::string> getAvailablePresets();
    static VoiceConfig getPresetConfig(const std::string& presetName);

    // Global Voice Parameters
    void setGlobalVolume(float volume);
    float getGlobalVolume() const;
    void setVoiceMix(uint8_t voiceId, float mix);
    float getVoiceMix(uint8_t voiceId) const;
    void setVoiceVolume(uint8_t voiceId, float volume);
    void setVoiceFrequency(uint8_t voiceId, float frequency);
    void setVoiceSlide(uint8_t voiceId, float slideTime);
};
```

## Setup Helpers

```cpp
class VoiceManagerBuilder {
public:
    VoiceManagerBuilder& withMaxVoices(uint8_t maxVoices);
    VoiceManagerBuilder& withVoice(const std::string& presetName);
    VoiceManagerBuilder& withVoice(const VoiceConfig& config);
    VoiceManagerBuilder& withGlobalVolume(float volume);
    VoiceManagerBuilder& withVoiceCountCallback(VoiceManager::VoiceCountCallback callback);
    VoiceManagerBuilder& withVoiceUpdateCallback(VoiceManager::VoiceUpdateCallback callback);

    std::unique_ptr<VoiceManager> build();
};
```

```cpp
class VoiceFactory {
public:
    // Create a basic dual-voice setup (like current implementation)
    static std::unique_ptr<VoiceManager> createDualVoiceSetup();

    // Create a quad-voice setup for more complex arrangements
    static std::unique_ptr<VoiceManager> createQuadVoiceSetup();

    // Create a full 8-voice polyphonic setup
    static std::unique_ptr<VoiceManager> createPolyphonicSetup();

    // Create a custom setup based on user preferences
    static std::unique_ptr<VoiceManager> createCustomSetup(
        const std::vector<std::string>& presets,
        uint8_t maxVoices = 8);
};
```

## Presets

The built-in preset set has seven entries:

1. `Analog` - three detuned saw waves, wavefolder, 24 dB low-pass filter.
2. `Digital` - one saw oscillator, subtle wavefolder, fast envelope.
3. `Bass` - saw plus triangle with one oscillator an octave down.
4. `Lead` - saw plus square with 12 semitone detuning.
5. `Square` - single square wave with pulse width control.
6. `Pad` - three saw oscillators with harmony intervals.
7. `Percussion` - sine-based voice with fast attack and decay.

```cpp
// Load preset by name
VoiceManager voiceManager(8);
uint8_t voiceId = voiceManager.addVoice("analog");

// Apply preset to existing voice
voiceManager.setVoicePreset(voiceId, "bass");

// Get preset configuration
VoiceConfig config = VoiceManager::getPresetConfig("lead");

// List available presets
std::vector<std::string> presets = VoiceManager::getAvailablePresets();
```

Preset details:

- Analog: three saw oscillators, detune `0.0`, `+0.045`, `-0.04` semitones; wavefolder gain `5.5`, offset `1.3`; LP24 resonance `0.33`, drive `3.1`; envelope `0.04 / 0.14 / 0.45 / 0.1`.
- Digital: one saw oscillator; wavefolder gain `3.0`; LP24 resonance `0.2`, drive `1.5`; envelope `0.015 / 0.1 / 0.1 / 0.1`.
- Bass: saw plus triangle, second oscillator at `-12` semitones; LP24 resonance `0.33`, drive `2.5`; envelope `0.01 / 0.3 / 0.4 / 0.2`.
- Lead: saw plus square, square at `+12` semitones, pulse width `0.3`; LP24 resonance `0.23`, drive `3.0`; envelope `0.02 / 0.2 / 0.35 / 0.15`.
- Square: one square oscillator, pulse width `0.255`; LP24 resonance `0.55`, drive `2.8`; envelope `0.02 / 0.2 / 0.0 / 0.15`.
- Pad: three saw oscillators with harmony `0`, `+4`, `+9`; LP12 resonance `0.1`, drive `1.0`; envelope `0.5 / 0.2 / 0.5 / 0.5`.
- Percussion: three sine oscillators, detune `0`, `+7`, `-5`; LP24 resonance `0.4`, drive `2.3`; envelope `0.005 / 0.08 / 0.0 / 0.07`.

## Parameter Ranges

Oscillator:

- Amplitude: `0.0..1.0`
- Detuning: `-12.0..+12.0` semitones
- Pulse width: `0.0..1.0`
- Harmony: `-12..+12` scale steps

Filter:

- Cutoff: `250.0..8000.0` Hz from normalized input
- Resonance: `0.0..1.0`
- Drive: `0.0..10.0`
- Passband gain: `0.0..1.0`
- High-pass frequency: `20.0..20000.0` Hz
- High-pass resonance: `0.0..1.0`

Envelope:

- Attack: `0.002..0.75` seconds
- Decay: `0.002..0.8` seconds
- Sustain: `0.0..1.0`
- Release: `0.001..10.0` seconds

Effects and mix:

- Overdrive gain: `0.0..2.0`
- Overdrive drive: `0.0..1.0`
- Wavefolder gain: `0.0..10.0`
- Wavefolder offset: `0.0..5.0`
- Output level: `0.0..1.0`
- Slide time: `0.0..10.0` seconds
- Voice mix: `0.0..1.0`
- Global volume: `0.0..1.0`

## VoiceState

Real-time note and modulation data arrives through `VoiceState`.

```cpp
struct VoiceState {
    float noteIndex;           // Note position in scale (0-47)
    float velocityLevel;       // Note velocity (0.0-1.0)
    float filterCutoff;        // Filter cutoff (0.0-1.0 normalized)
    float attackTimeSeconds;   // Envelope attack time
    float decayTimeSeconds;    // Envelope decay time
    int8_t octaveOffset;       // Octave offset in semitones
    bool isGateHigh;           // Gate on/off state
    bool hasSlide;             // Enable slide/portamento
    bool shouldRetrigger;      // Retrigger envelope
    uint8_t gateLengthTicks;   // Gate length in sequencer ticks
};
```

MIDI-style values are mapped into this structure:

- Notes become scale-aware `noteIndex` values.
- Velocity maps to `0.0..1.0`.
- Gate events drive envelope state.
- Controller changes map to filter and envelope parameters.

## Scale Integration

`Voice` can use an injected scale table. If no table is injected, it falls back to chromatic mapping.

```cpp
// Inject scale data
extern int scale_data[SCALES_COUNT][48];  // 48 steps per scale
voice.setScaleTable(scale_data, SCALES_COUNT);

// Set current scale
extern uint8_t currentScale;
voice.setCurrentScalePointer(&currentScale);
```

## Usage Examples

```cpp
#include "Voice.h"
#include "VoiceManager.h"

// Create voice manager
VoiceManager voiceManager(4);

// Add voices using presets
uint8_t voice1 = voiceManager.addVoice("analog");
uint8_t voice2 = voiceManager.addVoice("bass");

// Initialize with sample rate
voiceManager.init(48000.0f);

// Process audio (call this every sample)
float output = voiceManager.processAllVoices();
```

```cpp
// Create custom voice configuration
VoiceConfig customConfig;
customConfig.oscillatorCount = 2;
customConfig.oscWaveforms[0] = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
customConfig.oscWaveforms[1] = daisysp::Oscillator::WAVE_POLYBLEP_SQUARE;
customConfig.oscAmplitudes[0] = 0.7f;
customConfig.oscAmplitudes[1] = 0.3f;
customConfig.filterRes = 0.4f;
customConfig.hasWavefolder = true;

// Add custom voice
uint8_t voiceId = voiceManager.addVoice(customConfig);
```

```cpp
// Create voice state for real-time control
VoiceState state;
state.noteIndex = 24.0f;           // Middle C in scale
state.velocityLevel = 0.8f;        // 80% velocity
state.filterCutoff = 0.5f;         // 50% filter cutoff
state.isGateHigh = true;           // Note on
state.hasSlide = false;            // No slide
state.octaveOffset = 0;            // No octave shift

// Update voice parameters
voiceManager.updateVoiceState(voiceId, state);
```

```cpp
// Create voice manager using builder pattern
auto voiceManager = VoiceManagerBuilder()
    .withMaxVoices(6)
    .withVoice("analog")
    .withVoice("bass")
    .withVoice("lead")
    .withGlobalVolume(0.8f)
    .build();

// Create predefined setups
auto dualSetup = VoiceFactory::createDualVoiceSetup();
auto polySetup = VoiceFactory::createPolyphonicSetup();
```

```cpp
// Create sequencer and attach to voice
auto sequencer = std::make_unique<Sequencer>();
voiceManager.attachSequencer(voiceId, std::move(sequencer));

// Get sequencer for configuration
Sequencer* seq = voiceManager.getSequencer(voiceId);
if (seq) {
    // Configure sequencer parameters
    // seq->setTempo(120.0f);
    // seq->setPatternLength(16);
}
```

```cpp
// Process individual voice (for solo monitoring or effects)
float voiceOutput = voiceManager.processVoice(voiceId);

// Control individual voice parameters
voiceManager.setVoiceVolume(voiceId, 0.7f);
voiceManager.setVoiceMix(voiceId, 0.8f);
voiceManager.setVoiceSlide(voiceId, 0.1f);  // 100ms slide time

// Enable/disable voice
voiceManager.enableVoice(voiceId, true);
voiceManager.disableVoice(voiceId);
```

```cpp
// Check memory usage
size_t memoryUsed = voiceManager.getMemoryUsage();

// Check available slots
if (voiceManager.hasAvailableSlots()) {
    // Add more voices
}

// Remove voices to free memory
voiceManager.removeVoice(voiceId);
voiceManager.removeAllVoices();
```

## Migration From Individual Variables

Before:

```cpp
// Old approach - individual variables for each voice
extern uint8_t voice1Id, voice2Id, voice3Id, voice4Id;
extern VoiceState voiceState1, voiceState2, voiceState3, voiceState4;
extern bool GATE1, GATE2, GATE3, GATE4;
extern GateTimer gateTimer1, gateTimer2, gateTimer3, gateTimer4;

// UI state with individual preset indices
struct UIState {
    uint8_t voice1PresetIndex;
    uint8_t voice2PresetIndex;
    uint8_t voice3PresetIndex;
    uint8_t voice4PresetIndex;
};
```

After:

```cpp
// New approach - centralized VoiceSystem
extern VoiceSystem voiceSystem;

// UI state with array-based preset management
struct UIState {
    uint8_t voicePresetIndices[MAX_VOICES];
};
```

Access changed from branching to indexed lookup:

```cpp
// Old way - conditional selection
uint8_t currentVoiceId;
if (selectedVoice == 0) currentVoiceId = voice1Id;
else if (selectedVoice == 1) currentVoiceId = voice2Id;
// ...

// New way - direct array access
uint8_t currentVoiceId = voiceSystem.getVoiceId(selectedVoice);
```

```cpp
// Old way - individual variable updates
if (voiceIndex == 0) voiceState1 = newState;
else if (voiceIndex == 1) voiceState2 = newState;
// ...

// New way - direct array assignment
voiceSystem.getVoiceState(voiceIndex) = newState;
```

```cpp
// Old way - manual loops
for (int i = 0; i < 4; i++) {
    if (i == 0) osc1.SetAmp(0.0f);
    else if (i == 1) osc2.SetAmp(0.0f);
    // ...
}

// New way - helper functions
voiceSystem.stopAllGates();
```

Files involved in this architecture:

- `src/voice/VoiceSystem.h`
- `src/ui/UIState.h`
- `src/midi/MidiManager.cpp` and `src/midi/MidiManager.h`
- `src/ui/ButtonHandlers.cpp`
- `src/ui/UIEventHandler.cpp`
- `src/OLED/oled.cpp`
- `src/LEDMatrix/LEDMatrixFeedback.cpp`
- `Pico2Seq.ino`

## Integration Notes

- DSP is vendored under `src/dsp/` and uses DaisySP-style classes and functions.
- Scale arrays are injected into `Voice`; chromatic fallback is used when scale data is absent.
- Debug output depends on the project debug macros.
- Audio processing is real-time. Avoid heap allocation and blocking work in the audio path.
- Audio and UI run on separate cores in the sketch. Keep cross-core state changes explicit and synchronized.
- Four voices are allocated, but the gate timer arrays are intentionally limited to voices `0` and `1`.

## Gate Sequence Length Mode

Gate Sequence Length Mode sets the selected voice's Gate track length from `2..16` steps.

Activation:

- Hold the AS5600 control button to enter the mode.
- Release the button to exit.
- Toggling slide mode also exits.

Controls:

- While held, press any step button `1..16` to set the Gate sequence length for the selected voice.
- Values are clamped to `2..16`.

Feedback:

- The LED matrix blinks the selected voice row up to the current Gate length.
- The non-selected voice row is dimmed.
- Changing length triggers `uiState.resetStepsLightsFlag`.
- OLED shows `Gate Len Mode`, selected voice, current length, and a proportional bar.

Implementation anchors:

- UI handling: `src/ui/UIEventHandler.cpp`
- LED rendering: `src/LEDMatrix/LEDMatrixFeedback.cpp`
- OLED rendering: `src/OLED/oled.cpp`
