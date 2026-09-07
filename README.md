# Pico2Seq

A powerful 4-voice polyphonic step sequencer and synthesizer for the Raspberry Pi Pico 2 (RP2350 microcontroller), featuring real-time parameter control, polymetric sequencing, and comprehensive synthesizer voice management.

## Features

### Synthesis
- **4 Independent Polyphonic Voices**: Each with a complete DSP chain (B-spline oscillator bank, resonant main filter, ADSR envelope, overdrive distortion)
- **Three Sound Engines per Voice**: A classic oscillator bank (up to 3 oscillators, or raw noise), a Karplus-Strong **waveguide** engine for plucked/nylon/bell/shimmer strings, and a **noise-FX texture** engine (prime-tap diffuser, regenerative allpass swarm, pitch-tracked Lorenz chaos growl)
- **Two Filter Topologies**: A 24dB multi-mode ladder filter (LP12, LP24, BP12, BP24, HP12, HP24) with drive and passband gain compensation on the character voices, plus a clean modulation-stable state-variable filter (LP/BP/HP) everywhere else — including all three bass presets
- **Effects Processing**: Per-voice overdrive distortion
- **ADSR Envelopes**: Fast, analog-modeled attack, decay, sustain, and release stages with microsecond accuracy
- **15 Voice Presets**: Stored as `constexpr` tables in flash (.rodata), covering classic subtractive, sub-bass, waveguide string, hypersaw, and noise-texture sounds

### Advanced Sequencing
- **Polymetric Sequencing**: Independent track step lengths for each parameter (Notes: 16 steps, Filter: 8 steps, Velocity: 12 steps, etc.)
- **Real-time Recording**: Live parameter capture during playback using the TOF distance sensor, magnetic encoder, and physical faders
- **Scale Support**: 13 built-in musical scales with chromatic fallback and precomputed rank tables
- **Shuffle & Swing**: 16 PPQN shuffle templates for groovy swing timing

### Intuitive Controls
- **32-Button Touch Matrix**: MPR121 capacitive touch grid providing 32 dedicated step sequencing pads across two voice banks
- **Alchemy Modular UI Tiles**: Dedicated `SliderModule` (4 faders + 4 voice selects) and `ButtonModule8` (8 multi-function buttons) on a dedicated I2C1 bus
- **Hardware Mode Strap (GP7)**: Instant hardware toggle between Parameter mode and Utility mode
- **Real-time Sensors**: TMAG5273 magnetic encoder (Velocity Encoder board) for responsive parameter dialing
- **Distance Control**: VL53L1X TOF sensor for hands-free optical parameter modulation (74–1400 mm range)
- **Visual Feedback**: 128×64 SH1106G OLED display with 6-tier priority screen rendering
- **LED Matrix**: 8×4 WS2812B RGB LED display (mirroring the 4×8 touch matrix) with 10 vibrant color themes and playhead visualization

### Architecture Highlights
- **VoiceSystem Architecture**: Centralized, array-based voice management with safe accessor methods
- **Dual-Core Asymmetric Design**: Core 1 dedicated exclusively to 48kHz audio synthesis; Core 0 handles UI, sensors, clock, display rendering, and the USB CDC serial console
- **Lock-Free Parameter Staging**: Atomic generation counters allow Core 0 to stage parameter changes without blocking Core 1 audio processing
- **Host Test Suite**: Catch2 v3 unit test suite with hardware stubs, built and run locally via CTest

---

## Project Structure

```
├── Pico2Seq.ino              # Main Arduino sketch (dual-core setup & uClock callbacks)
├── includes.h                # Library and header aggregator
├── CMakeLists.txt            # Host unit test CMake entry point
├── .gitmodules               # Git submodule configuration
├── src/
│   ├── audio/                # I2S audio interface, PIO DMA, and buffer management
│   ├── pico2seq-core/        # Portable core sequencer, ParameterTrack, and scale tables
│   │   ├── scales/           # 13 scale tables and MIDI mapping
│   │   └── sequencer/        # Sequencer, ParameterManager, SequencerDefs, ShuffleTemplates
│   ├── rpdsp/                # Submodule: IC-Alchemy/RPDSP (header-only DSP algorithms)
│   ├── VelocityEncoder/      # Submodule: IC-Alchemy/VelocityEncoder (TMAG5273 driver)
│   ├── voice/                # Synthesizer voices, VoiceSystem, and VoicePresets
│   │   ├── Voice.h/.cpp      # Synthesizer voice DSP chain and staged parameters
│   │   ├── VoiceSystem.h     # Centralized 4-voice container and accessors
│   │   ├── VoicePresets.h/.cpp # 15 built-in voice presets as constexpr flash tables
│   │   ├── VoiceOscillator.h # Variant-based oscillator dispatch
│   │   └── VoiceManager.h    # Multi-voice lifecycle and master mix processing
│   ├── ui/                   # UI state, button handling, and control surface logic
│   │   ├── UIState.h         # Centralized UI state container
│   │   ├── ControlSurfaceLogic.h/.cpp # Pure control surface state machines (unit-tested)
│   │   ├── AlchemyControlBridge.h/.cpp# Alchemy I2C tile panel hardware bridge
│   │   ├── ButtonHandlers.h/.cpp      # Hardware button event handlers
│   │   └── UIEventHandler.h/.cpp      # Sequencer step adapter logic
│   ├── matrix/               # MPR121 4×8 touch matrix — 32 dedicated step pads
│   ├── sensors/              # Sensor management (EncoderManager and VL53L1X DistanceSensor)
│   ├── midi/                 # USB MIDI input/output, CC management, and clock
│   ├── LEDMatrix/            # 8×4 WS2812B RGB visual feedback (pad-mirror) and 10 color themes
│   ├── OLED/                 # 128×64 SH1106G OLED display manager and priority screens
│   ├── utils/                # Debug logging utilities (Debug.h/.cpp)
│   └── AlchemyUI/            # Vendored Alchemy Modular UI tile library (tracked in-repo)
├── docs/                     # Comprehensive architecture and subsystem documentation
├── tests/                    # Host-side Catch2 v3.5.2 unit test suite and stubs
└── diagnostic.h             # Hardware diagnostics
```

---

## Getting Started

### Prerequisites

**Hardware:**
- Raspberry Pi Pico 2 (RP2350) microcontroller
- I2S-compatible audio codec/DAC (e.g., PCM5102A, PT8211)
- MPR121 capacitive touch sensor (4×8 grid wired as 32 dedicated step pads)
- Alchemy Modular UI tiles: `SliderModule` (4 faders + 4 buttons) and `ButtonModule8` (8 buttons) on Wire1
- GP7 mode strap switch (LOW = Param mode, HIGH = Utility mode)
- OLED display (128×64 SH1106G on I2C `Wire` @ `0x3C`)
- Velocity Encoder board (TMAG5273A magnetic encoder on I2C `Wire` @ `0x35`)
- VL53L1X time-of-flight distance sensor (I2C `Wire` @ `0x29`)
- WS2812B RGB LED matrix (8×4 on GPIO pin 1)

**Software:**
- Arduino IDE with RP2040/RP2350 board support installed
- For command-line builds: Arduino CLI 1.4.1 and the Earle Philhower
  `rp2040:rp2040` core 6.0.0
- Required Arduino libraries:
  - `Adafruit MPR121` 1.2.1
  - `Adafruit VL53L1X` 3.1.2
  - `Adafruit SH110X` 2.1.15
  - `Adafruit TinyUSB Library` 3.7.7
  - `FastLED` 3.9.20
  - `uClock` 2.2.1 (stock library-manager install; the rp2040 backend runs the
    uClock timer in the SDK default alarm pool, so the ISR fires on core 0 — the
    control core. Upstream 2.3.0 changed the callback API; re-verify before
    upgrading.)

### Installation & Flashing

1. **Clone the repository with submodules:**
   ```bash
   git clone --recurse-submodules https://github.com/IC-Alchemy/Pico2Seq.git
   cd Pico2Seq
   ```
   *(If cloned without `--recurse-submodules`, run `git submodule update --init --recursive`)*

2. **Open in Arduino IDE:**
   - Launch Arduino IDE
   - Open `Pico2Seq.ino`
   - Select board: **Raspberry Pi Pico 2** / **RP2350**
   - Ensure USB stack is set to **Adafruit TinyUSB**

3. **Compile and Upload:**
   - Compile and flash to the Pico 2 board
   - Monitor the USB serial console (115200 baud) for startup diagnostics

### Building with Arduino CLI on Windows

Arduino CLI recursively compiles C/C++ files below a sketch's `src` directory. Pico2Seq's
`src` tree includes Git submodules with their own example source files, so the verified build
uses a disposable, correctly named `Pico2Seq/Pico2Seq.ino` staging directory and omits every
`examples` directory. This leaves the checkout unchanged while compiling only the firmware and
the submodules' library sources.

Run the following PowerShell from the repository root:

```powershell
$repoRoot = (Get-Location).Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stageRoot = Join-Path ([IO.Path]::GetTempPath()) "Pico2Seq-arduino-stage-$stamp"
$stageSketch = Join-Path $stageRoot 'Pico2Seq'
$buildPath = Join-Path $repoRoot "build\arduino-cli\Pico2Seq-current-$stamp"
New-Item -ItemType Directory -Path $stageSketch -Force | Out-Null

function Copy-StageTree {
    param([string]$Source, [string]$Destination)

    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
        if ($item.Name -in @('.git', 'build', 'build_test', 'build_fw', 'build_fw_on')) { continue }

        $target = Join-Path $Destination $item.Name
        if ($item.PSIsContainer) {
            if ($item.Name -eq 'examples') { continue }
            Copy-StageTree -Source $item.FullName -Destination $target
        } else {
            Copy-Item -LiteralPath $item.FullName -Destination $target -Force
        }
    }
}

Copy-StageTree -Source $repoRoot -Destination $stageSketch

$boardOptions = @(
    'flash=4194304_0'
    'arch=arm'
    'freq=300'
    'opt=Optimize3'
    'profile=Disabled'
    'rtti=Disabled'
    'stackprotect=Disabled'
    'exceptions=Disabled'
    'dbgport=Disabled'
    'dbglvl=None'
    'usbstack=tinyusb'
    'ipbtstack=ipv4only'
    'uploadmethod=default'
) -join ','

arduino-cli compile `
    --fqbn rp2040:rp2040:rpipico2 `
    --board-options $boardOptions `
    --warnings all `
    --clean `
    --build-property 'build.extra_flags=-ffast-math' `
    --build-path $buildPath `
    $stageSketch
```

The build is successful only when the foreground command finishes with exit code 0. Its `.uf2`,
`.elf`, `.bin`, and `.map` files are written to the timestamped directory under
`build/arduino-cli/`. The required `usbstack=tinyusb` option selects Adafruit TinyUSB; omitting it
causes the TinyUSB headers to reject the configuration.

This command compiles the firmware but does not upload it or validate the Pico 2, audio output,
MIDI, displays, sensors, or controls on physical hardware.

---

## Hardware Wiring Reference

| Peripheral | Interface / Bus | Pico 2 Pins | Notes |
|---|---|---|---|
| **I2S Audio DAC** | PIO I2S | GP10 (BCLK), GP11 (LRCK), GP12 (DATA) | 48kHz stereo DMA output |
| **MPR121 Touch Matrix** | `Wire` (I2C0) | GP4 (SDA), GP5 (SCL) | Address `0x5A` (32 dedicated step pads) |
| **SH1106G OLED** | `Wire` (I2C0) | GP4 (SDA), GP5 (SCL) | Address `0x3C` (128×64 monochrome) |
| **TMAG5273A Magnetic Encoder** | `Wire` (I2C0) | GP4 (SDA), GP5 (SCL) | Address `0x35` (`TMAG5273::ADDRESS_A`) |
| **VL53L1X Distance Sensor** | `Wire` (I2C0) | GP4 (SDA), GP5 (SCL) | Address `0x29` (TOF optical sensor) |
| **Alchemy Modular UI Tiles** | `Wire1` (I2C1) | GP14 (SDA), GP15 (SCL) | 100 kHz bus; SliderModule & ButtonModule8 |
| **Mode Strap Switch** | GPIO | GP7 | LOW = Param mode, HIGH = Utility mode |
| **WS2812B LED Matrix** | FastLED | GP1 | 8×4 RGB matrix data pin |

---

## Usage Guide

### Basic Operation

1. **Power on the device:** All 4 voices initialize with default presets.
2. **Start playback:** Press Play/Stop (ButtonModule8 in Utility mode, or Shift+Voice 1) to start `uClock`.
3. **Edit steps:** The 32 touch pads toggle gate states for the active voice pair (Low Bank = Voice 0/2; High Bank = Voice 1/3). Long-press enters Step Edit mode.
4. **Select a voice:** Press Voice 1–4 buttons on the SliderModule to switch active voices directly.
5. **Adjust parameters:** Rotate the TMAG5273 magnetic encoder or move physical faders to dial parameter values with live OLED/LED feedback.
6. **Real-time recording:** Hold (or Shift+tap to latch) a parameter button and touch step pads to record automation into the pattern.
7. **Switch function sets:** Toggle the GP7 mode strap between **Param** (Note, Velocity, Filter, Attack, Decay, Octave, Slide, Shift) and **Utility** (Play/Stop, Delay, Scale, Swing, Theme, Encoder Target, Randomize, Shift).

### Preset System

Each synthesizer voice supports 15 built-in sound presets (held as `constexpr` tables in flash):

1. **Analog** — Triple-saw classic subtractive synth with warm 24dB ladder filtering
2. **Digital** — Square + triangle hybrid with sharp 12dB lowpass cutoff
3. **Bass** — Deep sub-octave detuned sine/triangle bass
4. **Lead** — Dual-saw octave-harmony lead synth
5. **Square** — PWM pulse-width square wave with resonant bite
6. **Pad** — Atmospheric 3-oscillator chord pad with slow attack and release
7. **Percussion** — Fast-decaying noise-textured percussive transient
8. **SubFunk** — Sub-octave sine/triangle sub bass with warm overdrive grit
9. **RubberSub** — Rubbery sub bass: sub-octave square grind under a resonant band-pass honk, harder drive on transients
10. **WgPluck** — Classic Karplus-Strong plucked string (waveguide engine); bright burst, short natural tail
11. **WgNylon** — Dark, felt-soft nylon string; damped loop, gentle pick, long sympathetic tail
12. **WgBell** — Stiff dispersive waveguide string; inharmonic bell/kalimba partials, quick tail
13. **WgShimmer** — Wide-detuned two-string course with slow chorusing sustain and a very long, pad-like tail
14. **Hypersaw** — Three-saw stack (two ±21-cent detuned unisons plus an octave layer) glued with mild overdrive
15. **NoiseStorm** — Noise texture engine: pitch-tracked Lorenz chaos growl through a prime-tap diffuser and regenerative allpass swarm

---

## Dual-Core Architecture

Pico2Seq leverages the dual ARM Cortex-M33 cores of the RP2350:

```
+------------------------------------+    +------------------------------------+
|               CORE 0               |    |               CORE 1               |
|       (UI, Sensors & MIDI)         |    |       (Real-Time Audio DSP)        |
+------------------------------------+    +------------------------------------+
| • 1ms sensor poll (TMAG, VL53L1X)   |    | • fill_audio_buffer() loop         |
| • MPR121 32-pad touch matrix scan  |    | • VoiceManager::processAllVoices() |
| • Alchemy tile panel polling (I2C1)|    | • 4-voice synthesis chain          |
| • 50Hz OLED & WS2812B LED updates  |    | • FloatToPcm16() with __SSAT       |
| • uClock sequencer step ticking    |    | • Non-blocking I2S DMA @ 48kHz     |
| • USB CDC serial console            |    |                                    |
+------------------------------------+    +------------------------------------+
                   \                                /
                    +---[ Lock-Free Staging State ]-+
```

- **Core 1 (Audio Thread):** Strict real-time constraints. Never allocates heap memory, never performs blocking I2C transactions, and never touches USB endpoints.
- **Core 0 (System & Control):** Scans inputs, updates state machines, runs the internal note-lifecycle state machine and renders visual feedback. USB MIDI was removed 2026-09-06 — USB carries power and the CDC serial console only. Also hosts the uClock timer ISR (the stock library's alarm always fires on core 0), which is why audio lives on core 1.

---

## Host Unit Testing

Pico2Seq provides an automated host-side unit test suite powered by **Catch2 v3.5.2** and CMake:

```bash
# Configure and build test suite
cmake -B build_test -DCMAKE_BUILD_TYPE=Debug
cmake --build build_test --parallel

# Run the full suite via CTest
ctest --test-dir build_test/tests --output-on-failure

# Or run/filter the test binary directly
./build_test/tests/pico2seq_tests "[voice]"
```

For more details on test stubs and writing unit tests, see [`docs/testing.md`](docs/testing.md).

---

## Documentation Index

Comprehensive subsystem documentation is maintained in the [`docs/`](docs/) directory:

- [`docs/architecture.md`](docs/architecture.md) — System architecture, dual-core division, and component interactions
- [`docs/voice.md`](docs/voice.md) — Synthesizer voice DSP pipeline, VoiceOscillator, filters, ADSR, and preset definitions
- [`docs/VoiceSystem.md`](docs/VoiceSystem.md) — Centralized VoiceSystem data structures, accessor pattern, and voice routing
- [`docs/sequencer.md`](docs/sequencer.md) — 4-voice step sequencer engine, polymetric parameter tracks, and uClock integration
- [`docs/scales.md`](docs/scales.md) — 13 musical scales, semitone offsets, rank caching, and pitch mapping
- [`docs/matrix.md`](docs/matrix.md) — MPR121 32-pad touch input matrix, bank resolution, and Alchemy tile interaction
- [`docs/LEDMatrix.md`](docs/LEDMatrix.md) — WS2812B 8×4 RGB LED matrix visualizer, 10 themes, and pair-based voice indicators
- [`docs/oled.md`](docs/oled.md) — 128×64 SH1106G OLED display, 6-tier priority rendering hierarchy, and UI state
- [`docs/midi.md`](docs/midi.md) — MIDI subsystem (USB MIDI removed 2026-09-06; internal note lifecycle + CDC console)
- [`docs/sensors.md`](docs/sensors.md) — TMAG5273 magnetic encoder and VL53L1X TOF distance sensor integration
- [`docs/ButtonHandlers.md`](docs/ButtonHandlers.md) — UI button event dispatching and debounce logic
- [`docs/testing.md`](docs/testing.md) — Host-side Catch2 v3 unit testing guide, CMake/CTest workflow, and header stubs
- [`docs/alchemyui-tmag5273-migration.md`](docs/alchemyui-tmag5273-migration.md) — Migration and architectural transition notes for Alchemy tiles & TMAG5273
- [`docs/superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md`](docs/superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md) — Specification for Alchemy modular UI tile control surface
- [`docs/superpowers/specs/2026-09-02-modifier-layer-restoration.md`](docs/superpowers/specs/2026-09-02-modifier-layer-restoration.md) — Plan (not yet implemented) for restoring the modifier layer on the tile control surface

---

## License

MIT License — see `LICENSE` for details.
