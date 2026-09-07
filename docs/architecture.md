# Pico2Seq Architecture

This document provides a comprehensive technical overview of the Pico2Seq architecture, dual-core task distribution, data flow, DSP pipeline, concurrency model, and subsystem responsibilities on the Raspberry Pi Pico 2 (RP2350).

---

## 1. Top-Level Overview

Pico2Seq is a 4-voice polyphonic step sequencer and synthesizer running as an Arduino sketch on the dual-core ARM Cortex-M33 Raspberry Pi Pico 2 (RP2350).

```
Pico2Seq/
├── Pico2Seq.ino            # Main sketch entry point (setup/loop on Core 0, setup1/loop1 on Core 1)
├── includes.h              # Central aggregator of subsystem headers and pin definitions
├── diagnostic.h            # Structured diagnostic logging macros
├── docs/                   # System and subsystem documentation
├── src/                    # Firmware source code organized by subsystem
│   ├── audio/              # I2S DMA audio driver and producer buffer management (@48kHz)
│   ├── pico2seq-core/      # Portable, zero-dependency sequencer and musical scale core
│   │   ├── scales/         # Musical scale definitions (13 scales, 48 steps)
│   │   └── sequencer/      # Polymetric Sequencer, ParameterManager, ShuffleTemplates
│   ├── voice/              # Voice synthesis, VoiceManager, VoiceSystem, VoicePresets
│   ├── rpdsp/              # Submodule: header-only DSP library (IC-Alchemy/RPDSP)
│   ├── ui/                 # UIState, ControlSurfaceLogic, UIEventHandler, ButtonHandlers
│   ├── AlchemyUI/          # Modular I2C UI tile driver (Wire1 @ 100kHz)
│   ├── VelocityEncoder/    # Submodule: TMAG5273 / AS5600 magnetic encoder driver
│   ├── sensors/            # DistanceSensor (VL53L1X), EncoderManager, SensorConstants
│   ├── matrix/             # MPR121 32-pad capacitive touch matrix scanning
│   ├── LEDMatrix/          # 8x4 WS2812B FastLED matrix (mirrors the 4x8 touch matrix)
│   ├── OLED/               # 128x64 SH1106G I2C OLED display driver and view hierarchy
│   ├── midi/               # Internal MIDI note lifecycle (USB MIDI transmission removed 2026-09-06)
│   └── utils/              # Debug.h/.cpp lightweight logging utilities
└── tests/                  # Catch2 v3.5.2 host unit tests with hardware header stubs
```

---

## 2. Dual-Core Task Distribution & Lifecycle Model

The RP2350 processor features dual ARM Cortex-M33 cores. Pico2Seq assigns audio synthesis strictly to Core 1 and all user interaction, sensors, sequencing, and communication to Core 0. This split (flipped from the original audio-on-Core-0 layout on 2026-09-04) is shaped by a constraint of the stock `uClock` library: its repeating-alarm timer always fires on **Core 0**, regardless of which core calls `uClock.init()`. Running the control plane on Core 0 puts that 16th-note ISR burst (sequencer advance, MIDI sends) next to the UI that tolerates jitter, while audio stays isolated on Core 1.

```
+-----------------------------------------------------------------------------------------+
|                                    RP2350 DUAL-CORE SPLIT                               |
+----------------------------------------------------+------------------------------------+
|                      CORE 0                        |               CORE 1               |
|            UI, Sensors, MIDI & Sequencer           |      Real-Time Audio Synthesis     |
+----------------------------------------------------+------------------------------------+
| setup():                                           | setup1():                          |
|  - wait for voicesReady [acquire]                 |  - delay(100) [stabilize]          |
|  - usb_midi.begin()                                |  - initOscillators()               |
|  - Wire (I2C0 GP4/GP5): OLED,                      |      * VoiceManager(4) construction|
|    MPR121, TMAG5273, VL53L1X                       |      * Add 4 preset voices         |
|  - Wire1 (I2C1 GP14/GP15 @ 100kHz):                |      * Attach seq1..seq4 to voices |
|    Alchemy tile control panel                      |  - audio_new_producer_pool         |
|  - ledMatrix.begin(100) (GP1)                      |    (3 buffers, 256 samples)        |
|  - uClock.init(90 BPM, 480 PPQN)                   |  - audio_i2s_setup(48kHz, S16,     |
|  - seq1/seq2.start()                               |    GP10-12, DMA ch0, PIO SM0)      |
|                                                    |  - audio_i2s_set_enabled(true)     |
| loop():                                            |                                    |
|  - usb_midi.read()                                 | loop1():                           |
|  - pollUIHeldButtons(uiState, ...)                 |  - take_audio_buffer(pool, true)   |
|  - Drain PPQN ticks (uClock):                      |  - fill_audio_buffer(audioBuffer): |
|      * midiNoteManager.updateTiming()              |      for i = 0 .. 255:             |
|      * seq1..seq4.tickNoteDuration()               |        s = processAllVoices()      |
|      * voiceSystem.tickAllGateTimers()             |        pcm16 = FloatToPcm16(s)     |
|  - 1ms Loop (Control & Sensors):                   |        out[2i]=pcm16; out[2i+1]=.. |
|      * Matrix_scan() (32 step pads)                |  - give_audio_buffer(pool, buffer) |
|      * alchemyBridge.update()                      |                                    |
|      * magEncoder.update()                         |                                    |
|      * distanceSensor.update()                     |                                    |
|  - 20ms Loop (50Hz Displays):                      |                                    |
|      * updateStepLEDs()                            |                                    |
|      * display.update() (OLED)                     |                                    |
|      * ledMatrix.show()                            |                                    |
+----------------------------------------------------+------------------------------------+
```

### 2.1 Core 1: Real-Time Audio Engine
- **Dedicated Execution**: Runs standard Arduino `setup1()` and `loop1()`. No UI, serial processing, or sensor polling is ever executed on Core 1. `loop1()` blocks on `take_audio_buffer(producer_pool, true)` — that blocking *is* the pacing; never add anything else to this core.
- **Buffer Pool**: Configured with `audio_new_producer_pool(&bufferFormat, 3, 256)`:
  - 3 producer buffers in the pool.
  - 256 samples per buffer @ 48kHz ($\approx 5.33\text{ ms}$ real-time budget per buffer).
  - Format: 16-bit signed stereo (`AUDIO_BUFFER_FORMAT_PCM_S16`), sample stride of 4 bytes (2 channels $\times$ 2 bytes).
- **I2S Hardware Configuration**:
  - Data pin: GP12 (`PICO_AUDIO_I2S_DATA_PIN`).
  - Clock base pin: GP10 (`PICO_AUDIO_I2S_CLOCK_PIN_BASE` for BCLK GP10 and LRCK GP11).
  - DMA channel: Channel 0; PIO state machine: SM 0.
- **Fast Saturation (`FloatToPcm16`)**:
  ```cpp
  static inline int16_t FloatToPcm16(float s) noexcept {
      s = fminf(1.0f, fmaxf(-1.0f, s));
      const float scaled = s * 32768.0f;
      const int32_t i = (int32_t)lrintf(scaled);
      return (int16_t)__SSAT(i, 16);
  }
  ```
  Uses the ARM Cortex-M33 hardware saturation instruction `__SSAT` to clamp the scaled 32-bit integer into signed 16-bit range in a single cycle.
- **Mono-to-Stereo Replication**: The mono voice mix is duplicated across both channels: `out[2*i] = pcm16; out[2*i+1] = pcm16;`.

### 2.2 Core 0: System Control, UI, Sensors, MIDI & Clock
- **Execution**: Runs Arduino `setup()` and `loop()`.
- **Clock Engine (`uClock`)**:
  - Library: the stock `<uClock.h>` (2.2.1 installed from the library manager; upstream
    2.3.0 changed the callback API — re-verify before upgrading). An earlier alarm-pool
    fork (bound to core 1) was removed 2026-09-06: with the control plane on core 0, the
    stock default alarm pool already puts the ISR where it belongs.
  - Default tempo: 90 BPM; resolution: 480 PPQN (`PPQN_480`); shuffle on via
    `uClock.setShuffle(true)` using templates from `ShuffleTemplates.h`.
  - Initialized in `setup()` on Core 0, so the timer ISR fires on Core 0.
  - ISR-context callbacks stage events only — no work (2026-09-05 deferral refactor):
    `onStepCallback` enqueues the 16th-note step number into a 16-deep SPSC ring
    `stepQueue`, a `SpscQueue<uint32_t, 16>` from `src/utils/`; a full ring drops the
    new step and counts into `droppedStepCount`); `onOutputPPQNCallback` increments
    `ppqnTicksPending`. The firmware sends **no MIDI realtime clock output** — no Clock,
    Start, or Stop bytes go out over USB MIDI.
  - Thread-context callbacks: `onClockStart` / `onClockStop` keep their full bodies
    inline — `uClock.start()/stop()` are called from `setup()`/UI handlers (thread),
    never the ISR (uClock is master, no external clock input), so nothing there
    preempts.
- **Clock Event Drain** (in `loop()`, same core as the ISR):
  - `processClockEvents()` runs first in the timing section: dequeues steps into
    `processSequencerStep()` (the full 16th-note work — advances all four sequencers,
    routes sensor values, pushes `VoiceState`s into `voiceSystem`, stages voice
    parameters, sends gate/MIDI note events).
  - With this, all `usb_midi.send*` traffic lives in thread context: TinyUSB's MIDI
    endpoint has exactly one producer, closing the old ISR-vs-`tud_task` packet-drop
    and FIFO-race sharp edge.
  - Sequencer/midiNoteManager state is also single-context now (the old same-core
    ISR-preempts-thread reentrancy hazard on these structures is gone; a step can
    only be processed between `loop()` iterations, adding at most one loop-cycle of
    latency to note timing).
- **PPQN Drain Loop** (in `loop()`, same core as the ISR):
  - Drains `ppqnTicksPending`.
  - Advances `midiNoteManager.updateTiming(globalTickCounter)`.
  - Advances sequencer note durations (`seq1/seq2.tickNoteDuration()`).
  - Ticks gate countdown timers (`voiceSystem.tickAllGateTimers()`).
- **1ms Sensor and Control Loop**:
  - `Matrix_scan()`: Scans MPR121 32 capacitive touch step pads over I2C0 (Wire: GP4/GP5 @ 0x5A).
  - `alchemyBridge.update()`: Polls SliderModule (4 faders) and ButtonModule8 on dedicated I2C1 (Wire1: GP14/GP15 @ 100kHz) and reads the GP7 hardware mode strap.
  - `magEncoder.update()`: Reads the TMAG5273A magnetic encoder on Wire @ 0x35 and updates base values via `updateEncoderBaseValues(uiState)`.
  - `distanceSensor.update()`: Non-blocking VL53L1X distance sensor update on Wire @ 0x29 (74–1400mm range).
  - `pollUIHeldButtons()`: Processes long-press events across all four sequencers (`seq1..seq4`).
- **20ms (50Hz) Display Refresh Loop**:
  - OLED Display: `display.update(uiState, seq1..seq4, voiceManager)` refreshes the 128x64 SH1106G display on Wire @ 0x3C.
  - LED Matrix: `updateStepLEDs()` and `ledMatrix.show()` refresh the 8x4 WS2812B FastLED array on GPIO 1; control indicators moved to the OLED (transient notices + encoder line).
- **Freeze Forensics (`src/utils/FreezeWatchdog.h`, added 2026-09-05)**:
  - `freezeWatchdogArm()` (in `setup()`, after `Wire.begin()`) arms the hardware
    watchdog (2s — worst `loop()` iteration is ~0.5s) and installs a hard-fault
    handler; every `setup()` stage and every `loop()` slice feeds it with the
    phase that is *about to run* (`freezeWatchdogFeed`).
  - On a Core-0 hang or fault the board reboots within ~2s and
    `freezeWatchdogBootCheck()` prints a `[FREEZE] POST-MORTEM` on Serial (phase,
    uptime at freeze, processed-step count, and fault PC/LR for hard faults),
    decoded from the watchdog scratch registers (which survive a warm reset, not
    power-on). Scratch[4] is reserved by pico-sdk's `watchdog_enable` marker.
  - Coverage gap by design: a Core-1-only hang is not caught (Core 0 keeps
    feeding). The audio-path hang class was closed separately in 2026-09-05 by
    masking interrupts around the `src/audio/audio.cpp` buffer-list spin locks
    (the audio DMA IRQ and `loop1()` share those locks on Core 1, and
    `spin_lock_blocking` does not mask interrupts).

---

## 3. Cross-core ownership and bounded queues

Each `Voice` has one control-core producer and one audio-core consumer.
`SpscQueue<ControlUpdate, 8>` holds eight complete updates in FIFO order, using
fixed storage and lock-free 32-bit indices. Neither side blocks or allocates.

1. Core 0 updates its own requested config, parameters, or scalar controls.
2. It copies the update into a free slot, then publishes the write index with
   release ordering. A published slot belongs to Core 1.
3. Core 1 acquires the write index and copies one update locally at the start
   of `Voice::process()`.
4. Only after copying does Core 1 release the read index. Core 0 acquires that
   index before reusing the slot.
5. Core 1 applies the local copy to its DSP state, then renders a sample.

One update per sample bounds the drain and lets each queued gate edge reach the
envelope. Updates still drain while disabled, so queued re-enables can take
effect. Config, pitch caches, dirty flags, filter/slide coefficients, and gates
are audio-owned. The pitch version is now ordinary single-core state.

### Full queues

The producer never overwrites occupied slots. Setters retain unpublished changes
in a producer-owned pending update when full. `loop()` calls
`VoiceManager::flushControlUpdates()` every pass, including idle passes, to retry.
Unpublished values coalesce to the latest value; a pending retrigger stays set
while the gate remains high, and a later gate-off wins. Published events remain
FIFO. Sustained overload can coalesce intermediate unpublished notes/edges; this
is bounded latest-state recovery, not an unlimited event history. Final gate-off
and preset changes are retried even when no new input arrives.

### Readers and lifecycle

`VoiceManager::getVoiceConfig()` and `getVoiceState()` return const pointers to
control-owned requested copies, so consecutive UI edits include earlier edits
before audio consumes them. They are not audio telemetry. Applied-state getters
on `Voice` are restricted to the audio thread or quiescent tests.

Scale selection is sampled on Core 0 by setters and `flushControlUpdates()`;
audio reads only the copied index. Injected scale tables must stay immutable and
alive until both cores stop using the voice. Mixer gain scalars use lock-free
atomics. Construct/init, attach, add/remove, and destruction require both cores
to be quiescent. `voicesReady` publishes the complete boot-time collection with
release/acquire ordering before Core 0 accesses it.

Clock ISR callbacks still stage clock events only. They must never call voice
setters: the producer is Core 0's ordinary `loop()` context. Existing clock and
diagnostic flags are separate from the voice queue protocol.

---

## 4. Voice Subsystem & VoiceSystem Architecture

### 4.1 `VoiceSystem` Data Structure

Defined in `src/voice/VoiceSystem.h`:

```cpp
struct VoiceSystem {
    static constexpr uint8_t MAX_VOICES = 4;

    // Voice IDs assigned by VoiceManager
    uint8_t voiceIds[MAX_VOICES] = {0, 0, 0, 0};

    // Active voice states for synthesis and sequencing
    VoiceState voiceStates[MAX_VOICES];

    // Software gate flags + gate timers (strictly Voices 0 and 1)
    volatile bool gates[2] = {false, false};
    GateTimer gateTimers[2];

    // Array accessors with bounds checking
    uint8_t getVoiceId(uint8_t voiceIndex) const;
    void setVoiceId(uint8_t voiceIndex, uint8_t voiceId);

    VoiceState& getVoiceState(uint8_t voiceIndex);
    const VoiceState& getVoiceState(uint8_t voiceIndex) const;

    volatile bool& getGate(uint8_t voiceIndex);
    GateTimer& getGateTimer(uint8_t voiceIndex);

    void stopAllGates();
    void tickAllGateTimers();
};

extern VoiceSystem voiceSystem;
```

### 4.2 Voice Count & Hardware Asymmetry
- **4 Polyphonic Voices (`MAX_VOICES = 4`)**: All 4 voices are fully synthesized in real time on Core 1 via `voiceManager->processAllVoices()`.
- **2-Channel Hardware Gate / MIDI Asymmetry**:
  - **Voices 0 and 1**: Fully equipped with USB MIDI note on/off and CC transmission, and `GateTimer` duration countdowns.
  - **Voices 2 and 3**: Audio-only synthesis voices. They are driven by `seq3` and `seq4` and synthesized by `VoiceManager`, but have no USB MIDI output routing.
- **Safe Dummy Returns**:
  - `getGate(voiceIndex)` for `voiceIndex >= 2` returns a reference to an internal `static volatile bool dummy = false`.
  - `getGateTimer(voiceIndex)` for `voiceIndex >= 2` returns a reference to an internal `static GateTimer dummy`.
  - `getVoiceState(voiceIndex)` clamps index `< MAX_VOICES ? voiceIndex : 0`.

---

## 5. DSP Processing Pipeline & RPDSP Integration

All DSP components reside in the `rpdsp` namespace from `src/rpdsp/` (tracked as a Git submodule from `IC-Alchemy/RPDSP`).

```
                              Voice::process() (Core 1 @ 48kHz)
                                              │
                    ┌─────────────────────────┴─────────────────────────┐
                    │ 1. applyConfig_() & applyParameters_()  │
                    └─────────────────────────┬─────────────────────────┘
                                              │
                                              ▼
                    ┌───────────────────────────────────────────────────┐
                    │ 2. computeEnvelope()                              │
                    │    - rpdsp::ADSR (Gate rise/fall or retrigger)    │
                    │    - Returns envelope amplitude E in [0.0, 1.0]   │
                    └─────────────────────────┬─────────────────────────┘
                                              │
                                              ▼
                    ┌───────────────────────────────────────────────────┐
                    │ 3. updateFilter(E)                                │
                    │    - One-pole cutoff smoothing (4ms tau)          │
                    │    - Throttled filter.setFreq() every 8 samples   │
                    └─────────────────────────┬─────────────────────────┘
                                              │
                                              ▼
                    ┌───────────────────────────────────────────────────┐
                    │ 4. mixOscillators()                               │
                    │    - Silence short-circuit (if E <= 0.0005, ret 0)│
                    │    - Slew pitch if slide active                   │
                    │    - Commit pitch if gate HIGH                    │
                    │    - VoiceConfig.engine dispatch: osc bank        │
                    │      sum / waveguide / noise-FX source            │
                    └─────────────────────────┬─────────────────────────┘
                                              │
                                              ▼
                    ┌───────────────────────────────────────────────────┐
                    │ 5. finalizeOutput()                               │
                    │    - S_vca = S_osc * E (Pre-filter VCA envelope)  │
                    │    - Overdrive: rpdsp::Waveshaper (if enabled)    │
                    │    - Main filter per filterType:                  │
                    │      Ladder (Analog/Lead) or StateVariableFilter  │
                    │      (SVF: LP, BP or HP output per filterMode —   │
                    │       12 dB; the 24 dB modes are ladder-only)     │
                    │    - High-Pass: rpdsp::StateVariableFilter (HPF)  │
                    │    - Scaling: S_out = S_hpf * outputLevel         │
                    └─────────────────────────┬─────────────────────────┘
                                              │
                                              ▼
                    ┌───────────────────────────────────────────────────┐
                    │ VoiceManager::processAllVoices()                  │
                    │   Sum(voice[i] * mixLevel[i]) * globalVolume      │
                    └─────────────────────────┬─────────────────────────┘
                                              │
                                              ▼
                             FloatToPcm16() (Cortex-M33 __SSAT)
                                              │
                                              ▼
                                 I2S DMA Stereo Output @ 48kHz
```

### 5.1 `VoiceOscillator` Class Dispatch
`src/voice/VoiceOscillator.h` decouples numeric waveform identifiers from `rpdsp`'s class-per-waveform architecture using `std::variant`:

```cpp
using Osc = std::variant<
    rpdsp::BSplineSawOsc,        // WAVE_BSP_SAW (4): Band-limited 2nd-order B-spline saw
    rpdsp::BSplineSquareOsc,     // WAVE_BSP_SQUARE (5): Band-limited 2nd-order B-spline pulse (PWM)
    rpdsp::SineOscillator,       // WAVE_SIN (0): Pure sine wave
    rpdsp::TriangleOscillator,   // WAVE_TRI (1): Triangle wave
    rpdsp::SawOsc,               // WAVE_SAW (2): Naive saw wave
    rpdsp::SquareOsc,            // WAVE_SQUARE (3): Naive square wave
    rpdsp::NoiseOscillator       // WAVE_NOISE (255): White noise generator
>;
```

---

## 6. Subsystem Breakdown & Source Modules

### 6.1 `src/audio/`
- `audio.h`, `audio_i2s.h`, `audio.cpp`: Low-level I2S driver using RP2040/RP2350 PIO and DMA.
- `buffer.h`: Audio buffer structures (`audio_buffer_t`, `audio_buffer_pool_t`).
- `sample_conversion.h`: Conversion utilities between PCM representations.

### 6.2 `src/pico2seq-core/`
Portable core with **no hardware, UI, or Arduino dependencies**:
- `sequencer/Sequencer.h/.cpp`: 4-voice independent step sequencers.
- `sequencer/SequencerDefs.h`: Polymetric `ParameterTrack<N>`, `ParamId` enum, `VoiceState`, `GateTimer`.
- `sequencer/ParameterManager.h/.cpp`: Thread-safe parameter validation and clamping.
- `sequencer/ShuffleTemplates.h`: Groove and shuffle timing templates.
- `scales/scales.h/.cpp`: 13 musical scales across 48 steps, scale degree ranking, and frequency conversion.

### 6.3 `src/voice/`
- `Voice.h/.cpp`: Synthesizer voice DSP chain with lock-free staging and gate-controlled pitch commits.
- `VoiceManager.h/.cpp`: Multi-voice lifecycle management, master mixing, and preset attachment.
- `VoiceSystem.h`: Centralized `VoiceSystem` struct (`MAX_VOICES = 4`).
- `VoicePresets.h/.cpp`: Verified factory presets (Analog, Digital, Bass, Lead, Square, Pad, Percussion, SubFunk, RubberSub, WgPluck, WgNylon, WgBell, WgShimmer, Hypersaw, NoiseStorm); `constexpr` factories build a compile-time `std::array<VoiceConfig, 15>` table that lives in flash (.rodata), and `VoiceConfig.engine` selects the osc / waveguide / noise-FX source stage.
- `VoiceOscillator.h`: Variant-based oscillator class dispatcher.

### 6.4 `src/ui/` & `src/AlchemyUI/`
- `UIState.h`: Unified UI state struct (replaces loose globals; holds mode flags, debounce timestamps, preset arrays).
- `ControlSurfaceLogic.h/.cpp`: Unit-tested decision logic (`ModeStabilizer`, `PadBank`, `ShiftLatch`, `FaderMap`).
- `UIEventHandler.h/.cpp`: Event routing for MPR121 pads and control surface actions.
- `AlchemyControlBridge.h/.cpp`: Hardware bridge polling the Alchemy tile panel on Wire1 @ 100kHz. Frames are decoded per tile TYPE (`AlchemyProto.h` `buttonBlockOffset()`: button bytes at DATA 8..10 on slider tiles, DATA 0..2 on button tiles), and the slider/button roles are resolved by tile `TYPE_ID` (`sliderSlot()` / `firstSlotOfType(kTypeButton4)`), not by fixed bus slots.
- `ButtonHandlers.h/.cpp`: Button behavior implementations (play/stop, randomize, parameter cycling).

### 6.5 `src/matrix/`, `src/LEDMatrix/`, `src/OLED/`, `src/sensors/`
- `matrix/`: MPR121 driver scanning 32 capacitive touch pads.
- `LEDMatrix/`: 8x4 WS2812B FastLED matrix controller providing real-time visual feedback; the grid mirrors the 4x8 touch matrix pad-for-pad via `ControlSurface::LedLayout`.
- `OLED/`: 128x64 SH1106G display manager with hierarchical view rendering.
- `sensors/`: `EncoderManager` (TMAG5273 magnetic encoder) and `DistanceSensor` (VL53L1X laser ToF).

### 6.6 `src/utils/`
- `Debug.h/.cpp`: Zero-allocation, lightweight logging system with runtime toggle and level control (`DBG_ERROR`, `DBG_WARN`, `DBG_INFO`, `DBG_VERBOSE`).

---

## 7. Logging & Diagnostics (`src/utils/Debug.h/.cpp`)

Pico2Seq includes a lightweight, microcontroller-safe debugging utility designed to operate without dynamic memory allocations:

```cpp
#include "src/utils/Debug.h"

// Runtime level selection: Error (1), Warn (2), Info (3), Verbose (4)
Debug::setLevel(Debug::Level::Info);
Debug::setEnabled(true);

DBG_ERROR("Critical audio buffer underrun detected!");
DBG_WARN("VoiceManager: addVoice failed - no slots available");
DBG_INFO("Voice %u triggered note %.1f", voiceIndex, state.noteIndex);
DBG_VERBOSE("Sensor distance: %u mm", distanceMm);
```

- **Zero Cost When Disabled**: Set `AUG_DEBUG_COMPILED 0` to compile out all logging calls to `(void)0;`.
- **Fixed-Buffer Formatting**: Uses an internal 160-byte stack buffer with `vsnprintf()` to eliminate heap fragmentation.

---

## 8. Data Flow Architecture

```
Physical Inputs (Core 0)
├── MPR121 32 Step Pads (Wire: GP4/GP5 @ 0x5A)
├── Alchemy Tile Panel: 4 Faders + 12 Buttons (Wire1: GP14/GP15 @ 100kHz)
├── TMAG5273A Velocity Encoder (Wire @ 0x35)
├── VL53L1X Distance Sensor (Wire @ 0x29)
└── USB MIDI In (TinyUSB)
         │
         ▼
UIEventHandler / ControlSurfaceLogic / AlchemyControlBridge
         │
         ▼
UIState (Single Source of Truth)
         │
         ▼
4x Polymetric Sequencers (seq1..seq4)
         │
         ▼ (onStepCallback @ 16th notes)
VoiceSystem (voiceStates[4], gates[2], gateTimers[2])
         │
         ├──────────────────────────────────────────┐
         ▼ (Lock-Free Staging)                      ▼
VoiceManager / 4x Voice DSP Chains (Core 1)    USB MIDI Out & Gate Timing (Core 0)
         │
         ▼ (fill_audio_buffer @ 48kHz)
FloatToPcm16() [ARM Cortex-M33 __SSAT]
         │
         ▼
I2S DMA Buffer Pool (3x 256 samples)
         │
         ▼
I2S Stereo Audio Out (GP10 / GP11 / GP12)
```

---

## 9. Gate Sequence Length Mode

- **Activation**: Long-hold the encoder control button (Utility mode bit 5) to enter Gate Sequence Length Mode.
- **Behavior**: Step pads 1–16 set the Gate track length (2–16 steps) for the active voice via `Sequencer::setParameterStepCount(ParamId::Gate, ...)`.
- **Visual Feedback**:
  - **LED Matrix**: Renders a blinking band along the selected voice row up to the active Gate length; non-selected rows dim.
  - **OLED**: Displays "Gate Len Mode", the active voice number, length value, and a proportional horizontal gauge.
- **Exit**: Release the encoder button or toggle slide mode.

---

## 10. Performance & Verification Summary

| Constraint / Metric | Specification | Verification Method |
|---|---|---|
| Audio Sample Rate | 48,000 Hz, 16-bit stereo | Hardware I2S clock configuration (`Pico2Seq.ino`) |
| Audio Buffer Size | 256 samples ($5.33\text{ ms}$) $\times$ 3 buffers | `audio_new_producer_pool` inspection |
| Audio Latency | $\approx 10.66\text{ ms}$ (2 buffers) | DMA producer pool sizing |
| Core 1 Allocation | 0 bytes dynamic allocation in `loop1()` | Static buffer and fixed array audit |
| Core 0 Control Scan | 1,000 Hz (1 ms interval) | `loop()` timing loop verification |
| Core 0 Display Refresh | 50 Hz (20 ms interval) | `loop()` timing loop verification |
| Sequencer Resolution | 480 PPQN @ 90 BPM default | `uClock.init()` verification |
| Unit Test Coverage | Catch2 v3.5.2 host test suite | `ctest --test-dir build_test` |
