# Pico2Seq

A powerful 4-voice polyphonic step sequencer for the Raspberry Pi Pico2 microcontroller, featuring real-time parameter control, polymetric sequencing, and comprehensive synthesizer voice management.

## Features

###  Synthesis
- **4 Independent Voices**: Each with complete DSP chain (oscillators, filters, envelopes, effects)
- **Professional Filters**: 24dB ladder filters with multiple modes (lowpass, bandpass) and drive
- **Effects Processing**: Overdrive and wavefolder distortion effects per voice
- **ADSR Envelopes**: Configurable attack, decay, release with voice-specific timing

### Advanced Sequencing
- **Polymetric Sequencing**: Independent track lengths for each parameter (Notes: 16 steps, Filter: 8 steps, etc.)
- **Real-time Recording**: Live parameter capture during playback using distance sensor and magnetic encoder
- **Scale Support**: 13 built-in musical scales with chromatic fallback

### Intuitive Controls
- **32-Button Touch Matrix**: Capacitive touch interface for step editing and parameter control
- **Real-time Sensors**: TMAG5273 magnetic encoder (Velocity Encoder board) for velocity-sensitive parameter adjustment
- **Distance Control**: VL53L1X TOF sensor for hands-free parameter modulation (74-1400mm range)
- **Visual Feedback**: OLED display for parameter visualization and settings navigation
- **LED Matrix**: 8×8 WS2812B display for step sequencing feedback

### Architecture Highlights
- **VoiceSystem Architecture**: Centralized voice management with array-based access patterns
- **Dual-core Design**: Audio synthesis on Core 0, UI processing on Core 1
- **Real-time Performance**: Hardware-optimized 48kHz audio output using I2S
- **Modular Design**: Clean separation between DSP, sequencing, UI, and hardware layers
- **Embedded Optimized**: Designed for limited RAM and processing constraints

## Project Structure

```
├── Pico2Seq.ino              # Main Arduino sketch (dual-core setup)
├── includes.h                # Library and header aggregator
├── .gitmodules               # Git submodule configuration
├── src/
│   ├── audio/                # I2S audio interface and buffering
│   ├── pico2seq-core/        # Portable core sequencer and scale logic
│   ├── rpdsp/                # Submodule: IC-Alchemy/RPDSP (header-only DSP)
│   ├── VelocityEncoder/      # Submodule: IC-Alchemy/VelocityEncoder (TMAG5273 & AS5600 driver)
│   ├── VL53L1X/              # Non-blocking VL53L1X TOF driver
│   ├── voice/                # VoiceSystem and synthesizer voices
│   │   ├── VoiceSystem.h     # Central voice management (4 voices max)
│   │   ├── VoicePresets.h    # 7 predefined voice configurations
│   │   └── VoiceManager.h    # Voice lifecycle and audio processing
│   ├── ui/                   # UI state management, step-pad handling, and Alchemy tile bridge
│   │   ├── ButtonHandlers.h  # Specialized button event processing
│   │   ├── ControlSurfaceLogic.h  # Pure tile-surface decision logic (unit-tested)
│   │   ├── AlchemyControlBridge.h # Alchemy tile panel -> firmware UI glue
│   │   └── UIState.h         # Centralized UI state (array-based)
│   ├── matrix/               # 4×8 capacitive touch matrix — 32 dedicated step pads
│   ├── sensors/              # Sensor management (encoder and TOF distance)
│   ├── midi/                 # USB MIDI input/output and CC handling
│   ├── LEDMatrix/            # 8×8 LED visual feedback system
│   ├── OLED/                 # Display system for parameter visualization
│   └── AlchemyUI/            # Vendored Alchemy Modular UI tile library (protocol, TileButton, ButtonMap)
├── docs/                     # Comprehensive documentation
├── tests/                    # Host-side CMake unit test suite
└── diagnostic.h             # Debugging and diagnostic utilities
```

## Getting Started

### Prerequisites

**Hardware:**
- Raspberry Pi Pico2 (RP2350) microcontroller
- I2S-compatible audio codec/DAC (e.g., PCM5102A, PT8211)
- MPR121 capacitive touch sensor (4×8 grid, wired as 32 dedicated step pads)
- Alchemy Modular UI tiles: SliderModule (4 faders + 4 buttons) and ButtonModule8 (8 buttons) on a dedicated Wire1 bank
- GP7 strap switch (mode select: LOW = Param mode, HIGH = Utility mode)
- OLED display (128×64 SH1106G)
- Velocity Encoder board (TMAG5273 magnetic encoder)
- VL53L1X time-of-flight distance sensor
- WS2812B LED matrix (8×8, 64 LEDs)

**Software:**
- Arduino IDE with RP2040/RP2350 board support
- Required Arduino libraries:
  - Adafruit_MPR121
  - Adafruit_VL53L1X
  - Adafruit_SH110X
  - Adafruit_TinyUSB
  - FastLED

### Installation

1. **Clone the repository with submodules:**
   ```bash
   git clone --recurse-submodules https://github.com/IC-Alchemy/Pico2Seq.git
   cd Pico2Seq
   ```
   *(If already cloned without `--recurse-submodules`, run `git submodule update --init --recursive`)*

2. **Open in Arduino IDE:**
   - Launch Arduino IDE
   - Open `Pico2Seq.ino`
   - Select board: "Raspberry Pi Pico2" or "RP2350"
   - Choose correct serial port

3. **Install Dependencies:**
   - All required libraries are standard Arduino libraries


4. **Compile and Upload:**
   - Verify the project compiles without errors
   - Upload to Pico2 board
   - Monitor serial output for initialization messages

### Hardware Wiring

**I2S Audio (Required):**
- GP15 → DAC DATA (I2S data)
- GP16 → DAC BCK (I2S bit clock)
- GP17 → DAC LRCK (I2S word clock)

**Touch Matrix (MPR121, I2C on Wire):**
- I2C (Wire): GP4 (SDA), GP5 (SCL)
- Address: 0x5A (default)
- All 12 electrodes wired as the 4×8 touch grid — 32 dedicated step pads
  (bank = electrode index / 16, step = index % 16; see docs/matrix.md)

**OLED Display:**
- I2C (Wire): GP4 (SDA), GP5 (SCL)
- Address: 0x3C (default)

**Alchemy Tiles (param/utility buttons + faders):**
- Wire1 (I2C1): GP14 (SDA), GP15 (SCL), 400 kHz
- ⚠️ GP15 is also the I2S DAC data pin (`PICO_AUDIO_I2S_DATA_PIN` in `Pico2Seq.ino`) — resolve this collision before power-up (move the DAC data pin or the tile bank)
- SliderModule addresses 0x08–0x0A, ButtonModule8 addresses 0x0B–0x0D
- GP7 → mode strap switch to GND (LOW = Param mode, HIGH = Utility mode; if the polarity is inverted on the bench, flip `ControlSurface::kModeParamLevel` in `src/ui/ControlSurfaceLogic.h`)

**TMAG5273 Magnetic Encoder (Velocity Encoder board):**
- I2C (Wire): GP4 (SDA), GP5 (SCL)
- Address: 0x22 (TMAG5273B default)

**VL53L1X Distance Sensor:**
- I2C (Wire): GP4 (SDA), GP5 (SCL)
- Address: 0x29 (default)

**LED Matrix:**
- GP1 → WS2812B data pin

## Usage Guide

### Basic Operation

1. **Power on the device** - All four voices initialize with default presets
2. **Start playback** - Press Play/Stop (Utility mode tile button, or Shift+Voice1) to begin sequencing
3. **Edit steps** - The 32 touch pads set gate states on their own voice's steps (pads address two voices at a time: voice 1+2 or 3+4, depending on the selected voice); long-press enters step edit
4. **Select a voice** - SliderModule Voice 1–4 buttons (direct select, both modes)
5. **Adjust parameters** - Use magnetic encoder to modify current parameter (LED feedback shows selection)
6. **Real-time recording** - Hold (or Shift+tap to latch) a parameter button, touch a step to enter edit, then move the sensor or the matching fader for live capture
7. **Switch function sets** - Flip the GP7 mode strap between Param (parameter buttons + Filter/Attack/Decay/Velocity faders) and Utility (transport/scale/swing/theme + Tempo/Swing/Delay/GateLength faders); the OLED flashes a PARAM/UTIL banner on change

### Voice System

The VoiceSystem enables seamless management of all four voices:

```cpp
// Access voice state centrally
VoiceState& state = voiceSystem.getVoiceState(voiceIndex);

// Control voice gates
voiceSystem.setGate(voiceIndex, true);

// Bulk operations across all voices
voiceSystem.stopAllGates();
voiceSystem.setAllVoiceVolumes(0.8f);
```

### Parameter Control

**Per-Step Editing (Param mode tiles):**
- Hold the Note tile button, touch step pads (on the pad's own voice) to set pitches
- Hold Velocity, touch steps to set dynamics — or Shift+tap a param button to latch it hands-free
- Hold Filter/Attack/Decay buttons for advanced envelope control; the matching fader records into the step in edit

**Real-time Modulation:**
- Move hand over VL53L1X sensor for hands-free control of all parameters
- Visual feedback on OLED and LED matrix shows current values

### Preset System

Each voice supports 7 preset configurations:
1. **Analog** - Classic analog synthesizer sound
2. **Digital** - Clean digital waveforms
3. **Bass** - Deep bass with sub-octave detuning
4. **Lead** - Bright lead with octave harmonies
5. **Square** - PWM square wave with filter emphasis
6. **Pad** - Atmospheric pads with slow envelopes
7. **Percussion** - Fast-decaying percussive sounds
8. **Custom** - User-defined voice configurations

## Architecture Overview

### VoiceSystem Design

The VoiceSystem represents a significant improvement over traditional voice management:


**(VoiceSystem):**
```cpp
// Centralized, scalable approach
struct VoiceSystem {
    VoiceState voiceStates[MAX_VOICES];    // Array-based access
    bool gates[MAX_VOICES];               // Gate state management
    GateTimer gateTimers[MAX_VOICES];     // Timer coordination

    VoiceState& getVoiceState(uint8_t index);  // Safe access methods
    void setGate(uint8_t index, bool state);   // Consistent API
};
```

### Dual-Core Operation

**Core 0 (Audio):**
- Real-time audio synthesis at 48kHz
- DSP processing and I2S output
- Voice state processing and MIDI generation
- Time-critical operations only

**Core 1 (UI):**
- Touch pad scanning and debouncing (32 step pads)
- Alchemy tile polling (param/utility buttons, voice selects, faders, GP7 mode strap)
- Sensor input processing (TMAG5273, VL53L1X)
- OLED and LED matrix updates
- UI state management and button handling

### Real-time Performance

The system achieves sub-millisecond response times through:
- Hardware-optimized I2S audio output
- Pre-allocated static memory (no heap usage)
- Template-based parameter management
- Interrupt-driven sensor reading
- Efficient LED matrix batch updating

## Development

### Extending the System

**Adding New Presets:**
1. Define new preset configuration in `VoicePresets.cpp`
2. Update preset count and enum in `VoicePresets.h`
3. Test with different voice configurations

**Custom Oscillators:**
1. Add the oscillator class under `src/rpdsp/src/rpdsp/`
2. Add a waveform id + variant entry in `src/voice/VoiceOscillator.h`
3. Update DSP processing in `Voice.cpp`

## Documentation

Comprehensive documentation is available in the `docs/` directory:

- `architecture.md` - System architecture and component interactions
- `voice.md` - Voice system, presets, and DSP capabilities
- `sequencer.md` - 4-voice sequencing and VoiceSystem integration
- `matrix.md` - Touch matrix operation and button mapping
- `midi.md` - MIDI I/O and continuous controller support
- `oled.md` - Display system and parameter visualization
- `sensors.md` - TMAG5273 magnetic encoder and VL53L1X distance sensor integration
- `ButtonHandlers.md` - Button event handling and UI state management

## License

MIT License - see LICENSE file for details.

## Contributing

Contributions welcome! Please see documentation for coding style guidelines and submit pull requests with comprehensive testing.

## Related Projects

- [Pico-DSP-Garden](https://github.com/IC-Alchemy/Pico-DSP-Garden) - Alternative Pico DSP projects
