# Pico2Seq Architecture

This document gives a high-level overview of modules, data flow, and key responsibilities to make the codebase easier to navigate.

## Top-Level
- Pico2Seq.ino — Arduino entry point. Initializes hardware, systems, and main loop with dual-core architecture support.
- includes.h — Central aggregator of library and project headers (Arduino-friendly `src/...` includes).
- docs/ — Documentation.
- src/ — Source modules grouped by domain.

## Modules (src/)

### Core System Modules
- **audio/**: I2S configuration, audio callback wiring for Pico2, real-time audio processing
  - `audio.h`, `audio_i2s.h`, `audio.cpp` — Core audio interface and I2S management (@48kHz)
  - `buffer.h` — Audio buffer management for reliable audio streaming
  - `sample_conversion.h` — Audio format conversion utilities

### Sequencer System
- **sequencer/**: Multi-parameter step sequencer with polymetric sequencing support
  - `Sequencer.h/.cpp` — Main sequencer class with 16-step pattern storage per parameter (4 independent sequencers)
  - `SequencerDefs.h` — Parameter definitions, timing constants, and step structures
  - `ParameterManager.h/.cpp` — Thread-safe parameter management and validation
  - `ShuffleTemplates.h` — Shuffle pattern templates for rhythmic variation

### Voice Synthesis System
- **voice/**: Multi-voice synthesizer with DSP processing and preset management
  - `Voice.h/.cpp`, `VoiceManager.h/.cpp` — Voice synthesis and management systems
  - `VoiceSystem.h` — Centralized voice state management (4 voices max with array-based access)
  - `VoicePresets.h/.cpp` — Factory system for synthesizer voice configurations (7 preset types)

### Digital Signal Processing
- **dsp/**: Audio effects and signal processing algorithms
  - ADSR envelope, ladder filter, overdrive, wavefolder, delay line implementations
  - Local fork of DaisySP optimized for embedded use

### User Interface System
- **ui/**: Complete UI management system for matrix buttons and controls
  - `UIState.h` — Central UI state structure (replaces global variables with structured array management)
  - `UIEventHandler.h/.cpp` — Event-driven UI processing and button handling
  - `ButtonHandlers.h/.cpp` — Specialized button behavior implementations (randomize, parameter cycling)
  - `ButtonManager.h/.cpp` — Button state tracking and press event management
  - `UIConstants.h` — UI constants and button mappings

### Hardware Interface
- **matrix/**: 4×8 capacitive touch matrix scanning system with debounced input processing (32 buttons total)
- **LEDMatrix/**: 8×8 LED matrix display system with theme-based visual feedback for step states and parameter values
- **OLED/**: 128×64 SH1106G display for detailed parameter visualization and settings navigation
- **midi/**: USB MIDI communication with multi-voice note transmission and continuous controller (CC) support
- **sensors/**: Multi-sensor control system with AS5600 encoder and VL53L1X distance sensor

### Utilities and Helpers
- **scales/**: Musical scale definitions with 13 built-in configurations and note conversion tables
- **utils/**: Common utilities and helper functions
- **diagnostic.h**: Debugging and diagnostic utilities

## VoiceSystem Architecture

The VoiceSystem represents a significant architectural improvement that centralizes voice management:

### Key Design Principles
- **Centralization**: All voice-related data consolidated in one structure
- **Array-based Access**: Replaces individual `voice1Id`, `voice2Id` variables with `voiceIds[MAX_VOICES]`
- **Type Safety**: Bounds checking and consistent data access patterns
- **Scalability**: Easy to modify voice count by changing `MAX_VOICES` constant (currently 4)
- **Encapsulation**: Safe accessor methods prevent direct array access

```cpp
struct VoiceSystem {
    static const uint8_t MAX_VOICES = 4;

    // Core voice data arrays
    uint8_t voiceIds[MAX_VOICES];
    VoiceState voiceStates[MAX_VOICES];
    bool gates[MAX_VOICES];
    GateTimer gateTimers[MAX_VOICES];

    // Safe accessor methods
    VoiceState& getVoiceState(uint8_t index);
    void setGate(uint8_t index, bool state);
    bool getGate(uint8_t index) const;
    GateTimer& getGateTimer(uint8_t index);
};
```

### Benefits Over Legacy System
1. **Reduced Code Duplication**: Eliminates switch/case statements for voice selection
2. **Improved Performance**: Loop-based operations replace conditional branching
3. **Enhanced Safety**: Bounds checking prevents array access violations
4. **Better Organization**: All voice data in one location simplifies debugging
5. **Future Extensibility**: Easy to add features across all voices consistently

## Data Flow Architecture

```
User Input (Matrix/Sensors/MIDI)
         ↓ (UIEventHandler)
    Central UIState Management
         ↓ (Real-time updates)
      4x Independent Sequencers
         ↓ (Polymetric stepping)
      VoiceSystem (Dual-core sync)
         ↓ (Parameter mapping)
    VoiceManager/Voice DSP Chain
         ↓ (Real-time synthesis)
      I2S Audio Output @48kHz
         ↓ (Parallel processing)
    MIDI CC/MIDI Note Output
```

### Detailed Processing Chain

#### 1. Input Processing
- **Matrix Buttons**: Step editing, parameter selection, mode switching (4×8 capacitive grid)
- **AS5600 Encoder**: Real-time parameter modulation with velocity-sensitive scaling
- **VL53L1X Distance**: Hands-free parameter control (74-1400mm range)
- **MIDI Input**: Future expansion capability

#### 2. UI State Management
- **Centralized State**: All UI variables now in `UIState` struct vs global variables
- **Parameter Mapping**: Button presses mapped to voice parameters, sequencer controls
- **Mode Management**: Settings mode, gate length mode, step editing modes
- **State Synchronization**: Safe inter-core communication using volatile variables

#### 3. Sequencer Processing
- **4 Independent Sequencers**: One per voice with fully configurable parameter tracks
- **Polymetric Operation**: Each parameter can have different step counts (Note:16, Filter:8, etc.)
- **Real-time Recording**: Live sensor parameter capture during playback
- **Gate-controlled Programming**: Parameter editing restricted to active step gates

#### 4. Voice Synthesis Chain
- **VoiceSystem Coordination**: Centralized voice state management and access
- **VoiceManager Integration**: Manages voice lifecycle and DSP configuration
- **DSP Processing**: Multi-oscillator synthesis with effects (filter, overdrive, wavefolder)
- **Parameter Interpolation**: Smooth transitions between sequencer steps

#### 5. Output Processing
- **I2S Audio**: Real-time audio output @48kHz with hardware optimization
- **MIDI Communication**: USB MIDI note transmission and CC parameter output
- **Visual Feedback**: OLED display shows parameters, LED matrix shows step states

## Key Architectural Decisions

### Multi-core Architecture
- **Core 0**: Audio synthesis and I2S output (real-time critical)
- **Core 1**: UI processing, sensor reading, LED/OLED updates
- **Synchronization**: Volatile variables and careful interrupt handling
- **Performance**: Isolates real-time audio from UI processing

### UIState Centralization
- **Eliminated Globals**: 25+ global UI variables replaced with structured `UIState`
- **Thread Safety**: Single responsibility for state mutations
- **Data Flow**: Clear parameter flow from input → UIState → sequencer → voice
- **Extensibility**: Easy addition of new UI modes and parameters

### Polymetric Sequencing
- **Independent Track Lengths**: Each parameter can have different step counts
- **Complex Patterns**: Enables rhythmic diversity (e.g., note every 2 steps, filter every 8)
- **Memory Efficient**: Template-based `ParameterTrack` with static allocation
- **Real-time Capable**: O(1) access time for all parameter lookups

### Sensor Integration
- **AS5600 Encoder**: Velocity-sensitive parameter control with 12-bit resolution
- **VL53L1X Distance**: Instant parameter feedback via proximity sensing
- **Range Optimization**: Parameter values intelligently mapped to musical ranges
- **Debouncing**: Hardware and software filtering prevent erratic behavior

### VoiceSystem Benefits
1. **Code Clarity**: Reference `voiceSystem.getVoiceState(voiceIndex)` instead of individual variables
2. **Maintenance**: Changes to voice count are contained in one struct definition
3. **Testing**: Easier unit testing with centralized access pattern
4. **Performance**: Reduced function call overhead and improved cache locality

## Gate Sequence Length Mode (UI → Sequencer → LED/OLED)

- **Activation**: Long-hold the AS5600 control button enters Gate Sequence Length Mode; release to exit
- **Behavior**: Step buttons (1–16) set the Gate track length for the currently selected voice via VoiceSystem integration
- **LED Feedback**: LEDMatrixFeedback renders blinking band up to current length on selected voice row
- **OLED Feedback**: OLED display shows "Gate Len Mode", selected voice, length, and bar graph visualization

## Performance Characteristics

### Real-time Constraints
- **Audio Processing**: Pure audio synthesis runs isolated on Core 0
- **I2S Performance**: Hardware-optimized 48kHz output with minimal jitter
- **Parameter Interpolation**: Smooth parameter changes prevent zipper noise
- **Memory Layout**: Static allocation eliminates heap fragmentation

### Dual-core Synchronization
- **Data Consistency**: Volatile variables for cross-core communication
- **Race Prevention**: Careful state management prevents corruption
- **Timing Coordination**: Hardware timers coordinate sequencer stepping

### Hardware Optimizations
- **LED Matrix**: Direct GPIO control with efficient update algorithms
- **OLED Display**: Cached rendering with selective update regions
- **Sensor Reading**: Non-blocking I2C operations with timeout protection

## Next Improvements (Roadmap)
- Introduce an `AppContext` structure to further centralize all global state management
- Promote public headers to an `include/` directory with project-wide `#include` standardization
- Add comprehensive unit tests for sequencer logic and voice management
- Implement CI pipeline with clang-format, clang-tidy, and cppcheck validation
- Add performance profiling tools for real-time optimization analysis

## Coding Style
- `.clang-format` (LLVM base, 2-space indent) and `.editorconfig` enforce consistent style and EOLs
- `diagnostic.h` provides structured logging and debugging utilities
- Static analysis tools integrated into development workflow