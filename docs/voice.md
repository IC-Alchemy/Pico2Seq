# Voice Module Documentation

## Overview

The voice module provides a comprehensive synthesizer voice system with multi-oscillator support, filtering, effects processing, and preset management. It is designed for embedded systems (specifically the Raspberry Pi Pico) and integrates with the sequencer and MIDI systems.

### Architecture Components

The voice system consists of several key components:

- **`Voice`**: Individual synthesizer voice with oscillators, filter, envelope, and effects
- **`VoiceManager`**: Manages multiple voices with allocation, deallocation, and unified audio processing
- **`VoicePresets`**: Factory system for common synthesizer voice configurations
- **Supporting Classes**: `VoiceManagerBuilder`, `VoiceFactory` for easy setup and configuration

### Key Features

- **Multi-oscillator synthesis**: Up to 3 oscillators per voice with independent waveforms, amplitudes, detuning, and harmony
- **Advanced filtering**: 24dB ladder filter with multiple modes, plus high-pass filtering
- **Effects processing**: Overdrive and wavefolder effects
- **Envelope control**: ADSR envelope with configurable attack, decay, sustain, release
- **Frequency sliding**: Smooth portamento/glide effects between notes
- **Scale integration**: Support for different musical scales with chromatic fallback
- **Preset system**: 7 predefined voice types (Analog, Digital, Bass, Lead, Square, Pad, Percussion)
- **Memory efficient**: Designed for embedded systems with limited RAM

## Public Classes and APIs

### VoiceConfig Structure

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

### Voice Class

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

### VoiceManager Class

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

### VoiceManagerBuilder Class

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

### VoiceFactory Class

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

## Preset System

### Available Presets

The system includes 7 predefined voice presets:

1. **Analog**: Classic analog synthesizer sound with 3 detuned saw waves, wavefolder distortion
2. **Digital**: Clean digital sound with single saw oscillator, subtle wavefolder
3. **Bass**: Deep bass sound with saw and triangle oscillators, octave detuning
4. **Lead**: Bright lead sound with saw and square oscillators, 12th detuning
5. **Square**: Classic square wave sound with PWM capability
6. **Pad**: Atmospheric pad sound with 3 harmonically rich oscillators
7. **Percussion**: Percussive sound with sine waves and fast envelope

### Preset Loading and Application

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

### Preset Configuration Details

#### Analog Preset
- 3 oscillators: All saw waves with slight detuning (0.0, +0.045, -0.04 semitones)
- Wavefolder distortion with gain 5.5, offset 1.3
- Filter: 24dB lowpass, resonance 0.33, drive 3.1
- Envelope: Attack 0.04s, Decay 0.14s, Sustain 0.45, Release 0.1s

#### Digital Preset
- 1 oscillator: Saw wave
- Wavefolder with gain 3.0
- Filter: 24dB lowpass, resonance 0.2, drive 1.5
- Envelope: Attack 0.015s, Decay 0.1s, Sustain 0.1, Release 0.1s

#### Bass Preset
- 2 oscillators: Saw + Triangle, with -12 semitone detuning on second oscillator
- Filter: 24dB lowpass, resonance 0.33, drive 2.5
- Envelope: Attack 0.01s, Decay 0.3s, Sustain 0.4, Release 0.2s

#### Lead Preset
- 2 oscillators: Saw + Square with 12 semitone detuning
- Square wave pulse width: 0.3
- Filter: 24dB lowpass, resonance 0.23, drive 3.0
- Envelope: Attack 0.02s, Decay 0.2s, Sustain 0.35, Release 0.15s

#### Square Preset
- 1 oscillator: Square wave with pulse width 0.255
- Filter: 24dB lowpass, resonance 0.55, drive 2.8
- Envelope: Attack 0.02s, Decay 0.2s, Sustain 0.0, Release 0.15s

#### Pad Preset
- 3 oscillators: All saw waves with harmony (0, +4, +9 semitones)
- Filter: 12dB lowpass, resonance 0.1, drive 1.0
- Envelope: Attack 0.5s, Decay 0.2s, Sustain 0.5, Release 0.5s

#### Percussion Preset
- 3 oscillators: All sine waves with detuning (+0, +7, -5 semitones)
- Filter: 24dB lowpass, resonance 0.4, drive 2.3
- Envelope: Attack 0.005s, Decay 0.08s, Sustain 0.0, Release 0.07s

## Parameter Ranges and Units

### Oscillator Parameters
- **Amplitude**: 0.0 to 1.0 (linear)
- **Detuning**: -12.0 to +12.0 semitones
- **Pulse Width**: 0.0 to 1.0 (square/pulse waves only)
- **Harmony**: -12 to +12 scale steps

### Filter Parameters
- **Cutoff Frequency**: 250.0 to 8000.0 Hz (mapped from 0.0-1.0 normalized input)
- **Resonance**: 0.0 to 1.0
- **Drive**: 0.0 to 10.0
- **Passband Gain**: 0.0 to 1.0
- **High-pass Frequency**: 20.0 to 20000.0 Hz
- **High-pass Resonance**: 0.0 to 1.0

### Envelope Parameters
- **Attack Time**: 0.002 to 0.75 seconds
- **Decay Time**: 0.002 to 0.8 seconds
- **Sustain Level**: 0.0 to 1.0
- **Release Time**: 0.001 to 10.0 seconds

### Effects Parameters
- **Overdrive Gain**: 0.0 to 2.0
- **Overdrive Drive**: 0.0 to 1.0
- **Wavefolder Gain**: 0.0 to 10.0
- **Wavefolder Offset**: 0.0 to 5.0

### Voice Control Parameters
- **Output Level**: 0.0 to 1.0
- **Slide Time**: 0.0 to 10.0 seconds
- **Voice Mix**: 0.0 to 1.0
- **Global Volume**: 0.0 to 1.0

## MIDI and Sequencer Integration

### Voice State Structure

The voice receives real-time updates through the `VoiceState` structure:

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

### Sequencer Integration

Each voice can be attached to a sequencer for automated playback:

```cpp
// Create and attach sequencer
auto sequencer = std::make_unique<Sequencer>();
voiceManager.attachSequencer(voiceId, std::move(sequencer));

// Or attach existing sequencer
voiceManager.attachSequencer(voiceId, existingSequencerPtr);
```

### MIDI Integration

The voice system expects MIDI-style control through the `VoiceState` structure:

- **Note numbers**: Converted to frequency using scale-aware mapping
- **Velocity**: 0-127 MIDI velocity mapped to 0.0-1.0
- **Gate events**: Note on/off triggers envelope
- **Controller changes**: Map to filter cutoff, envelope parameters

### Scale Integration

The voice system supports multiple musical scales:

```cpp
// Inject scale data
extern int scale_data[SCALES_COUNT][48];  // 48 steps per scale
voice.setScaleTable(scale_data, SCALES_COUNT);

// Set current scale
extern uint8_t currentScale;
voice.setCurrentScalePointer(&currentScale);
```

Scale steps (0-47) are mapped to semitone offsets. If no scale is injected, the system falls back to chromatic mapping.

## Usage Examples

### Basic Voice Usage

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

### Voice Configuration

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

### Real-time Parameter Control

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

### Builder Pattern Usage

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

### Sequencer Integration

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

### Individual Voice Processing

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

### Memory Management

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

## Blocking Issues Discovered

### 1. Missing Dependencies

The voice system depends on several external components that need to be verified:

- **DaisySP library**: Required for oscillators, filters, envelope, and effects
  - `daisysp::Oscillator`
  - `daisysp::LadderFilter`
  - `daisysp::Svf`
  - `daisysp::Adsr`
  - `daisysp::Overdrive`
  - `daisysp::Wavefolder`
  - `daisysp::WhiteNoise`
  - `daisysp::mtof()` and `fmap()` functions

- **Scale data**: The system references external scale arrays:
  - `extern int scale[SCALES_COUNT][48]`
  - `extern uint8_t currentScale`
  - `extern const int SCALES_COUNT`
  - `extern const int SCALE_STEPS`

- **VoiceState structure**: Defined in `SequencerDefs.h` but not shown in voice module

### 2. Global Variable Coupling

The voice system reduces but doesn't eliminate global dependencies:

- Scale data injection requires external management
- Falls back to chromatic mapping if scale injection fails
- Debug system uses `DBG_INFO`, `DBG_WARN`, `DBG_VERBOSE` macros

### 3. Resource Constraints

For embedded systems (Raspberry Pi Pico):

- **Memory usage**: Each voice requires significant RAM for DSP components
- **CPU usage**: Real-time audio processing with multiple voices may strain RP2040
- **Static allocation**: Pre-allocated lookup tables and preset storage

### 4. Integration Points

The system integrates with:
- **Sequencer system**: For rhythmic playback and pattern generation
- **MIDI system**: For external control and note input
- **UI system**: For parameter control and voice management
- **Audio system**: For final output processing

### 5. Thread Safety

- Static frequency lookup table is initialized once (thread-safe)
- Preset storage uses static initialization (thread-safe)
- Voice manager operations should be synchronized in multi-threaded environments

## Recommendations

1. **Verify DaisySP Integration**: Ensure all DaisySP components are properly included and initialized
2. **Scale System Integration**: Confirm scale data arrays are properly defined and accessible
3. **Memory Profiling**: Monitor RAM usage with multiple voices on target hardware
4. **Performance Testing**: Verify real-time audio processing performance with maximum voice count
5. **Error Handling**: Add proper error handling for missing presets and invalid configurations