# Voice Module Documentation

## 1. Overview

The voice module provides a comprehensive synthesizer voice system with multi-oscillator synthesis, ladder filtering, effects processing, lock-free parameter staging, and preset management. It is designed specifically for the dual-core Raspberry Pi Pico 2 (RP2350) architecture and integrates with the sequencer, UI, and MIDI systems.

### 1.1 Architecture Components

The voice system consists of several key components:

- **`Voice`**: Individual synthesizer voice encapsulating oscillators, ladder filter, high-pass filter, ADSR envelope, overdrive waveshaper, and lock-free parameter/pitch staging.
- **`VoiceManager`**: Manages multiple voices with allocation, deallocation, master volume scaling, per-voice mix levels, and unified per-sample audio processing.
- **`VoiceSystem`**: Centralized structure consolidating voice IDs, states, gates, and gate countdown timers into arrays for `MAX_VOICES = 4` voices.
- **`VoicePresets`**: Factory namespace providing 15 voice presets (Analog, Digital, Bass, Lead, Square, Pad, Percussion, SubFunk, RubberSub, WgPluck, WgNylon, WgBell, WgShimmer, Hypersaw, NoiseStorm).
- **`VoiceOscillator`**: Variant-based dispatcher decoupling numeric waveform IDs from `rpdsp` oscillator classes.
- **Supporting Classes**: `VoiceManagerBuilder` and `VoiceFactory` for builder-pattern and pre-configured voice setups.

### 1.2 VoiceSystem Centralization

The `VoiceSystem` struct provides centralized voice tracking:

```cpp
struct VoiceSystem {
    static constexpr uint8_t MAX_VOICES = 4;

    uint8_t voiceIds[MAX_VOICES] = {0, 0, 0, 0};
    VoiceState voiceStates[MAX_VOICES];

    // Gates and timers are strictly dedicated to Voices 0 and 1
    // (the two voices with MIDI gate support)
    volatile bool gates[2] = {false, false};
    GateTimer gateTimers[2];

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

---

## 2. Public Classes and APIs

### 2.1 `VoiceConfig` Structure

Defined in `src/voice/Voice.h`:

```cpp
enum VoiceEngine : uint8_t {
    ENGINE_OSC = 0,       // Up to 3 oscillators (or raw noise when oscillatorCount == 0)
    ENGINE_WAVEGUIDE = 1, // Karplus-Strong plucked string (rpdsp::PluckedStringVoice)
    ENGINE_NOISEFX = 2,   // Noise + chaos source through diffuser/swarm inserts
};

struct VoiceConfig {
    // Oscillator configuration
    uint8_t oscillatorCount = 3;                                            // Number of oscillators (1-3; 0 = noise fallback / alternate-engine presets)
    uint8_t oscWaveforms[3] = {WAVE_BSP_SAW, WAVE_BSP_SAW, WAVE_BSP_SAW};   // Waveform types (WAVE_* from VoiceOscillator.h)
    float oscAmplitudes[3] = {0.5f, 0.5f, 0.5f};                            // Oscillator amplitudes (0.0-1.0)
    float oscDetuning[3] = {0.0f, 0.0f, 0.0f};                              // Detuning in semitones (-12.0 to +12.0)
    float oscPulseWidth[3] = {0.5f, 0.5f, 0.5f};                            // Pulse width for square/pulse waves (0.0-1.0)
    int harmony[3] = {0, 0, 0};                                             // Harmony intervals in scale steps (-12 to +12)

    // Sound engine selection (VoiceEngine). Ignored fields stay at their defaults.
    uint8_t engine = ENGINE_OSC;                                            // ENGINE_OSC, ENGINE_WAVEGUIDE, or ENGINE_NOISEFX

    // Waveguide engine parameters (ENGINE_WAVEGUIDE only)
    float wgT60 = 2.5f;                                                     // String tail T60 in seconds (0.05-10.0)
    float wgBrightness = 0.7f;                                              // Loop damping: 0 dark nylon .. 1 glassy (0.0-1.0)
    float wgPickPosition = 0.25f;                                           // Pick point on string (0.02 bridge .. 0.5 middle)
    float wgPickHardness = 0.8f;                                            // Excitation burst: 0 soft felt .. 1 hard pick
    float wgStiffness = 0.0f;                                               // Inharmonic dispersion: 0 harmonic .. 1 bell-like
    float wgDetune = 6.0f;                                                  // Two-string course spread in cents (0.0-30.0)

    // Noise-FX engine parameters (ENGINE_NOISEFX only)
    float noiseDiffuseSize = 0.8f;                                          // Prime-tap diffuser smear (0.0-1.0)
    float noiseDiffuseMix = 0.7f;                                           // Diffuser wet amount (0.0-1.0)
    float noiseSwarmColor = 0.5f;                                           // Allpass swarm tone (0.0-1.0)
    float noiseSwarmRegen = 0.9f;                                           // Allpass swarm regeneration (0.0-1.2)
    float noiseChaosLevel = 0.35f;                                          // Pitch-tracked chaos_lorenz growl mix (0.0-1.0)

    // Filter settings
    float filterRes = 0.2f;                                                 // Filter resonance (0.0-1.0)
    float filterDrive = 1.8f;                                               // Filter drive amount (0.0-4.0)
    float filterPassbandGain = 0.23f;                                       // Passband gain compensation (0.0-0.5)
    rpdsp::LadderFilter::Mode filterMode = rpdsp::LadderFilter::Mode::LP24; // Ladder filter mode (LP24, LP12, BP24, BP12, HP24, HP12)

    // High-pass filter settings
    float highPassFreq = 80.0f;                                             // High-pass cutoff frequency in Hz (20.0-20000.0)
    float highPassRes = 0.1f;                                               // High-pass resonance (0.0-1.0)

    // Effects chain configuration
    bool hasOverdrive = false;                                              // Enable overdrive effect
    bool hasEnvelope = true;                                                // Enable envelope (recommended: true)
    float overdriveGain = 0.34f;                                            // Overdrive output gain (0.0-2.0)
    float overdriveDrive = 0.25f;                                           // Overdrive drive amount (0.0-1.0)

    // Envelope default settings
    float defaultAttack = 0.04f;                                            // Default attack time in seconds (0.001-10.0)
    float defaultDecay = 0.14f;                                             // Default decay time in seconds (0.001-10.0)
    float defaultSustain = 0.5f;                                            // Default sustain level (0.0-1.0)
    float defaultRelease = 0.1f;                                            // Default release time in seconds (0.001-10.0)

    // Voice mixing
    float outputLevel = 0.6f;                                               // Voice output level (0.0-1.0)
    bool enabled = true;                                                    // Voice enabled state
};
```

#### UI Filter Modes (`voiceui` namespace)
```cpp
namespace voiceui {
inline constexpr rpdsp::LadderFilter::Mode kFilterModes[] = {
    rpdsp::LadderFilter::Mode::LP24, rpdsp::LadderFilter::Mode::LP12,
    rpdsp::LadderFilter::Mode::BP24, rpdsp::LadderFilter::Mode::BP12,
    rpdsp::LadderFilter::Mode::HP24, rpdsp::LadderFilter::Mode::HP12
};
inline constexpr const char* kFilterModeNames[] = {"LP24", "LP12", "BP24", "BP12", "HP24", "HP12"};
inline constexpr int kFilterModeCount = 6;
}
```

---

### 2.2 `Voice` Class

Defined in `src/voice/Voice.h` and implemented in `src/voice/Voice.cpp`:

```cpp
class Voice {
public:
    Voice(uint8_t id, const VoiceConfig& config);
    ~Voice() = default;

    // Initialization and configuration
    void init(float sampleRate);
    void setConfig(const VoiceConfig& config);
    const VoiceConfig& getConfig() const noexcept;
    VoiceConfig& getConfig() noexcept;

    // Real-time audio processing (runs on Core 0 @ 48kHz)
    float process() noexcept;

    // Parameter updates (called on Core 1 control thread)
    void updateParameters(const VoiceState& newState);

    // Sequencer integration
    void setSequencer(std::unique_ptr<Sequencer> seq);
    void setSequencer(Sequencer* seq);
    Sequencer* getSequencer() noexcept;

    // Scale injection (removes global scale coupling)
    void setScaleTable(const int (*table)[48], size_t scaleCount);
    void setCurrentScalePointer(const uint8_t* currentScalePtr);

    // State and gate management
    VoiceState& getState() noexcept;
    const VoiceState& getState() const noexcept;
    void setGate(bool gateState);
    bool getGate() const noexcept;

    // Filter control
    void setFilterFrequency(float freq);
    float getFilterFrequency() const noexcept;

    // Voice identification and enable
    uint8_t getId() const noexcept;
    bool isEnabled() const noexcept;
    void setEnabled(bool enabled);

    // Frequency and slide control
    void setFrequency(float frequency);
    void setSlideTime(float slideTime);

    // Pitch optimization & modulation API
    void setPitchBend(float semitones);
    void setModulationDepth(float semitones);
    void markPitchDirty();
    void updateFrequencyIfNeeded();
    float getCachedFrequency(uint8_t oscIndex) const;
};
```

---

### 2.3 `VoiceState` Structure

Defined in `src/pico2seq-core/sequencer/SequencerDefs.h`:

```cpp
struct VoiceState {
    float noteIndex = 0.0f;                                                   // Scale step index (0-47)
    float velocityLevel = 0.8f;                                               // Voice amplitude (0.0-1.0)
    float filterCutoff = 0.37f;                                               // Filter cutoff frequency (0.0-1.0 normalized)
    float attackTimeSeconds = 0.01f;                                          // Envelope attack time (0.0-1.0s)
    float decayTimeSeconds = 0.01f;                                           // Envelope decay time (0.0-1.0s)
    float octaveOffset = 0.0f;                                                // Normalized octave offset (0.0=C2, 0.5=C3, 1.0=C4)
    uint16_t gateLengthTicks = SequencerConstants::DEFAULT_GATE_LENGTH_TICKS; // Gate duration (default 60 ticks @ 480 PPQN)
    bool isGateHigh = false;                                                  // Voice gate state (active note on)
    bool hasSlide = false;                                                    // Portamento / slide enable flag
    bool shouldRetrigger = false;                                             // Envelope restart flag
};
```

---

### 2.4 `VoiceOscillator` Class & Waveform Dispatch

Defined in `src/voice/VoiceOscillator.h`:

```cpp
inline constexpr uint8_t WAVE_SIN = 0;
inline constexpr uint8_t WAVE_TRI = 1;
inline constexpr uint8_t WAVE_SAW = 2;
inline constexpr uint8_t WAVE_SQUARE = 3;
inline constexpr uint8_t WAVE_BSP_SAW = 4;     // Band-limited 2nd-order B-spline saw
inline constexpr uint8_t WAVE_BSP_SQUARE = 5;  // Band-limited 2nd-order B-spline pulse (with PWM)
inline constexpr uint8_t WAVE_NOISE = 255;    // White noise generator marker

class VoiceOscillator {
public:
    void prepare(float sampleRate);
    void setWaveform(uint8_t waveform);
    void setFreq(float hz);
    void setPulseWidth(float width);
    float process();
    uint8_t waveform() const;

private:
    using Osc = std::variant<
        rpdsp::BSplineSawOsc,
        rpdsp::BSplineSquareOsc,
        rpdsp::SineOscillator,
        rpdsp::TriangleOscillator,
        rpdsp::SawOsc,
        rpdsp::SquareOsc,
        rpdsp::NoiseOscillator
    >;
    // ...
};
```

- **Band-Limited Oscillators**: Uses 2nd-order B-spline polynomial interpolation (`BSplineSawOsc` and `BSplineSquareOsc`) for alias-suppressed synthesis on ARM Cortex-M33.
- **Zero Heap Allocations**: Oscillator variants are stored in fixed-size `std::array<VoiceOscillator, 3>` members on the stack/struct.

---

### 2.5 `VoiceManager` Class

Defined in `src/voice/VoiceManager.h`:

```cpp
class VoiceManager {
public:
    using VoiceCountCallback = std::function<void(uint8_t voiceCount)>;
    using VoiceUpdateCallback = std::function<void(uint8_t voiceId, const VoiceState& state)>;

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

    // Sequencer Attachment
    bool attachSequencer(uint8_t voiceId, std::unique_ptr<Sequencer> sequencer);
    bool attachSequencer(uint8_t voiceId, Sequencer* sequencer);
    Sequencer* getSequencer(uint8_t voiceId);

    // Audio Processing
    void init(float sampleRate);
    float processAllVoices() noexcept;
    float processVoice(uint8_t voiceId);

    // Voice Control
    void enableVoice(uint8_t voiceId, bool enabled = true);
    void disableVoice(uint8_t voiceId);
    bool isVoiceEnabled(uint8_t voiceId) const;

    // Voice Information
    uint8_t getVoiceCount() const;
    uint8_t getMaxVoices() const;
    std::vector<uint8_t> getActiveVoiceIds() const;
    size_t getMemoryUsage() const;
    bool hasAvailableSlots() const;

    // Callbacks
    void setVoiceCountCallback(VoiceCountCallback callback);
    void setVoiceUpdateCallback(VoiceUpdateCallback callback);

    // Preset Management
    static std::vector<std::string> getAvailablePresets();
    static VoiceConfig getPresetConfig(const std::string& presetName);

    // Global & Per-Voice Mixing
    void setGlobalVolume(float volume);
    float getGlobalVolume() const;
    void setVoiceMix(uint8_t voiceId, float mix);
    float getVoiceMix(uint8_t voiceId) const;
    void setVoiceVolume(uint8_t voiceId, float volume);
    void setVoiceFrequency(uint8_t voiceId, float frequency);
    void setVoiceSlide(uint8_t voiceId, float slideTime);
};
```

---

## 3. Verified Preset System

Defined in `src/voice/VoicePresets.h` and implemented in `src/voice/VoicePresets.cpp`. All 15 presets are verified verbatim against firmware source code.

Each preset is built by a `constexpr VoiceConfig makeXxx() noexcept` factory function, and the factories are assembled into a `constexpr std::array<VoiceConfig, 15> kPresets` — the whole bank is compile-time data (.rodata, XIP flash on RP2350) and costs no SRAM; accessors hand out `const` references, and the caller's `VoiceConfig`/`stagedConfig_` copy is the only RAM instance. `static_assert`s keep the preset name table and preset table in sync. Index-based accessors: `getPresetName(uint8_t)` (returns `"Unknown"` out of range), `getPresetConfig(uint8_t)` (returns Analog out of range), and `getPresetCount()`, plus per-preset getters (`getAnalogVoice()` … `getNoiseStormVoice()`). `VoiceManager::getAvailablePresets()` / `setVoicePreset()` expose the same bank by lowercase name and fall back to Analog for unknown names.

| # | Preset Name | Engine | Oscillators | Amplitudes | Detune (Semis) | Harmony | Filter Mode | Filter Settings | Overdrive | Envelope (A/D/S/R) | Output Level |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **0** | **Analog** | osc | 3x `WAVE_BSP_SAW` | `[0.5, 0.25, 0.25]` | `[0.0, +0.08, -0.08]` | `[0, 0, 0]` | **LP24** | Res: 0.33, Drive: 3.1, Passband: 0.23, HPF: 150 Hz | Off (Gain: 0.8, Drive: 0.25) | `0.04s / 0.14s / 0.3 / 0.1s` | `0.5` |
| **1** | **Digital** | osc | 2x (`WAVE_BSP_SQUARE`, `WAVE_TRI`) | `[0.75, 1.0]` | `[0.0, +12.0]` | `[0, 0]` | **LP12** | Res: 0.40, Drive: 2.5, Passband: 0.25, HPF: 111 Hz (Res: 0.15) | Off (Gain: 0.7, Drive: 0.51) | `0.015s / 0.1s / 0.5 / 0.1s` | `0.5` |
| **2** | **Bass** | osc | 2x (`WAVE_SIN`, `WAVE_TRI`) | `[1.0, 1.0]` | `[-12.0, -12.0]` | `[0, 0]` | **LP12** | Res: 0.33, Drive: 2.0, Passband: 0.12, HPF: 85 Hz (Res: 0.4) | Off (Gain: 0.95, Drive: 0.16) | `0.01s / 0.3s / 0.55 / 0.2s` | `0.95` |
| **3** | **Lead** | osc | 2x `WAVE_BSP_SAW` | `[0.6, 0.4]` | `[0.0, 0.0]` | `[0, 3]` | **LP12** | Res: 0.40, Drive: 3.0, Passband: 0.23, HPF: 160 Hz | Off (Gain: 0.7, Drive: 0.45) | `0.02s / 0.2s / 0.5 / 0.15s` | `0.5` |
| **4** | **Square** | osc | 1x `WAVE_BSP_SQUARE` (PW: 0.2) | `[1.0]` | `[0.0]` | `[0]` | **LP24** | Res: 0.52, Drive: 3.3, Passband: 0.33, HPF: 150 Hz | Off (Gain: 0.75, Drive: 0.35) | `0.02s / 0.2s / 0.0 / 0.15s` | `0.5` |
| **5** | **Pad** | osc | 3x `WAVE_BSP_SAW` | `[0.33, 0.33, 0.33]` | `[0.0, 0.0, 0.0]` | `[0, -3, +2]` | **LP12** | Res: 0.30, Drive: 2.2, Passband: 0.23, HPF: 140 Hz (Res: 0.08) | Off (Gain: 0.85, Drive: 0.25) | `0.02s / 0.2s / 0.5 / 0.5s` | `0.5` |
| **6** | **Percussion** | osc | **0 oscs** (`WAVE_NOISE`, `NoiseOscillator`) | `[1.0]` | `[0.0]` | `[0]` | **LP24** | Res: 0.40, Drive: 2.3, Passband: 0.33, HPF: 200 Hz | Off (Gain: 0.45, Drive: 0.30) | `0.005s / 0.08s / 0.0 / 0.07s` | `0.5` |
| **7** | **SubFunk** | osc | (`WAVE_SIN`, `WAVE_TRI`, `WAVE_SIN`) | `[1.0, 0.4, 0.25]` | `[-12.0, -12.0, 0.0]` | `[0, 0, 0]` | **LP12** | Res: 0.45, Drive: 3.6, Passband: 0.2, HPF: 55 Hz | On (Gain: 0.9, Drive: 0.35) | `0.004s / 0.22s / 0.35 / 0.12s` | `0.9` |
| **8** | **RubberSub** | osc | (`WAVE_SIN`, `WAVE_BSP_SQUARE`, `WAVE_TRI`) | `[0.9, 0.3, 0.5]` | `[-12.0, -24.0, 0.0]` | `[0, 0, 0]` | **BP24** | Res: 0.62, Drive: 2.6, Passband: 0.3, HPF: 70 Hz | On (Gain: 1.0, Drive: 0.55) | `0.002s / 0.16s / 0.25 / 0.09s` | `0.85` |
| **9** | **WgPluck** | waveguide | — (wg: T60 1.8s, bright 0.78, pick 0.26/0.85, stiff 0.0, det 4c) | — | — | — | **LP12** | Res: 0.2, Drive: 1.8, Passband: 0.25, HPF: 120 Hz | Off | `0.001s / 0.5s / 0.65 / 0.25s` | `0.75` |
| **10** | **WgNylon** | waveguide | — (wg: T60 3.2s, bright 0.28, pick 0.42/0.22, stiff 0.05, det 9c) | — | — | — | **LP12** | Res: 0.15, Drive: 1.5, Passband: 0.25, HPF: 90 Hz | Off | `0.002s / 0.6s / 0.7 / 0.5s` | `0.8` |
| **11** | **WgBell** | waveguide | — (wg: T60 1.4s, bright 0.9, pick 0.08/1.0, stiff 0.88, det 0c) | — | — | — | **LP24** | Res: 0.3, Drive: 2.0, Passband: 0.23, HPF: 250 Hz | Off | `0.001s / 0.45s / 0.4 / 0.3s` | `0.65` |
| **12** | **WgShimmer** | waveguide | — (wg: T60 6.5s, bright 0.55, pick 0.35/0.6, stiff 0.15, det 26c) | — | — | — | **LP12** | Res: 0.2, Drive: 1.6, Passband: 0.23, HPF: 140 Hz | Off | `0.004s / 0.8s / 0.85 / 0.8s` | `0.7` |
| **13** | **Hypersaw** | osc | 3x `WAVE_BSP_SAW` | `[0.45, 0.3, 0.3]` | `[0.0, +0.21, -0.21]` | `[0, 0, +12]` | **LP24** | Res: 0.35, Drive: 2.8, Passband: 0.25, HPF: 180 Hz | On (Gain: 0.85, Drive: 0.28) | `0.012s / 0.3s / 0.8 / 0.25s` | `0.5` |
| **14** | **NoiseStorm** | noise-FX | — (nf: diffuse 0.85/0.65, swarm 0.6/0.95, chaos 0.4) | — | — | — | **LP24** | Res: 0.72, Drive: 3.2, Passband: 0.3, HPF: 220 Hz | On (Gain: 0.8, Drive: 0.4) | `0.003s / 0.5s / 0.55 / 0.45s` | `0.45` |

The eight presets added with the expansion bank: **SubFunk** — bouncy sub bass; a sine sub an octave down carries the weight, a triangle adds movement, and a driven LP12 plus warm overdrive grit gives the filtered-growl funk character. **RubberSub** — rubbery sub bass; a sub-octave square grinds under a sine through a resonant BP24 ("rubbery honk"), with harder overdrive that spits on transients. **WgPluck** — classic Karplus-Strong plucked string: bright burst, harmonic loop, short natural tail. **WgNylon** — dark felt-soft nylon: heavily damped loop, gentle pick, long sympathetic tail. **WgBell** — stiff dispersive string whose inharmonic upper partials read as bell/kalimba; hard bridge pick, quick tail, HPF at 250 Hz keeps the shimmer. **WgShimmer** — wide-detuned (26-cent) two-string course with a very long T60 tail; slow chorusing sustain turns the pluck into a ringing pad. **Hypersaw** — supersaw-style stack of three BSP saws (two ±21-cent detuned unisons plus an octave-up layer) glued with mild overdrive under a wide-open LP24. **NoiseStorm** — noise-based texture: noise plus a pitch-tracked Lorenz chaos growl feed a prime-tap diffuser and a regenerative allpass swarm, then a resonant LP24 pings with the envelope.

Preset 9-12 use `engine = ENGINE_WAVEGUIDE` (`rpdsp::PluckedStringVoice`, 2048-sample
delay): each gate rise (or retrigger) plucks the string at the current base pitch, and
the `wg*` config fields tune T60, loop brightness, pick position/hardness, stiffness
(inharmonic dispersion), and two-string course detune. Preset 14 uses
`engine = ENGINE_NOISEFX`: `NoiseOscillator` plus a pitch-tracked `chaos_lorenz` growl
feed `fx_diffuse` (prime-tap diffuser) and `fx_swarm` (regenerative allpass swarm)
from `rpdsp/DSPFunctions.h`, pre-filter so the ladder shapes the texture.

---

## 4. DSP Processing Pipeline & Signal Flow

Each call to `Voice::process()` on Core 0 executes the following stages:

```
[Sequencer / UI Parameters]
           │
           ▼ (Lock-Free Staging: paramsGen_, configPending_, pitchGen_)
┌─────────────────────────────────────────────────────────────────────────────────┐
│ Voice::process() (Core 0 @ 48kHz)                                               │
│                                                                                 │
│ 1. Apply Pending Updates                                                        │
│    - applyPendingConfig_(): Reconfigures filters, waveforms, detune multipliers  │
│    - applyPendingParams_(): Applies staged VoiceState, ADSR parameters, filter f │
│                                                                                 │
│ 2. Envelope Processing (computeEnvelope)                                        │
│    - rpdsp::ADSR: noteOn() on gate rise / retrigger; noteOff() on gate fall     │
│    - Computes envelope amplitude E in [0.0, 1.0]                                │
│                                                                                 │
│ 3. Filter Cutoff Smoothing (updateFilter)                                       │
│    - Target cutoff = (filterFrequency * E) + (filterFrequency * 0.1)            │
│    - One-pole smoother: cutoffCurrent += alpha * (targetCutoff - cutoffCurrent) │
│    - Throttled filter.setFreq() update every 8 samples (relative eps > 0.2%)    │
│                                                                                 │
│ 4. Source Stage & Slide Slew (mixOscillators)                                   │
│    - Silence short-circuit: If E <= 0.0005, return 0.0 immediately              │
│    - engine == ENGINE_WAVEGUIDE: pluck on gate rise; S_osc = waveguide_.process │
│    - engine == ENGINE_NOISEFX: S_osc = noise + chaos_lorenz (fx inserts at 5)   │
│    - ENGINE_OSC: commit pitch to hardware ONLY when isGateHigh == true          │
│    - If slide active: Exponential slew via fmaf(delta, slideAlpha, currentFreq) │
│    - If oscCount > 0: S_osc = Sum(osc[i].process() * oscAmplitudes[i])          │
│    - If oscCount == 0: S_osc = noise_.process()                                 │
│                                                                                 │
│ 5. Pre-Filter VCA & Effects Shaping (finalizeOutput)                            │
│    - S_vca = S_osc * E   (Pre-filter VCA makes overdrive response dynamic)      │
│    - If hasOverdrive: S_vca = overdrive.process(S_vca * overdriveGain)          │
│    - If ENGINE_NOISEFX: fx_diffuse -> fx_swarm inserts (rpdsp DSPFunctions)     │
│                                                                                 │
│ 6. Ladder & High-Pass Filtering                                                 │
│    - S_filt = filter.process(S_vca * velocityLevel)                             │
│    - S_hpf = highPassFilter.process(S_filt).highpass (or bypassed if <= 20Hz)   │
│                                                                                 │
│ 7. Output Scaling                                                               │
│    - S_out = S_hpf * outputLevel                                                │
└─────────────────────────────────────────────────────────────────────────────────┘
           │
           ▼
[VoiceManager::processAllVoices() -> Sum(S_out * mixLevel) * globalVolume]
           │
           ▼
[FloatToPcm16() -> Cortex-M33 __SSAT -> I2S DMA Buffer]
```

---

## 5. Musical Scale & Pitch Architecture

### 5.1 Static Precomputed Lookup Table
- `frequencyLookupTable[128]`: Initialized once via `std::call_once` covering MIDI notes 0–127 using `rpdsp::midiNoteToHz()`.

### 5.2 Scale Data Injection
Scale tables are injected via dependency injection, eliminating global couplings:
```cpp
extern int scale[SCALES_COUNT][SCALE_STEPS];  // 13 scales, 48 steps
extern uint8_t currentScale;

voice->setScaleTable(scale, SCALES_COUNT);
voice->setCurrentScalePointer(&currentScale);
```
- **Synthesis Pitch Offset**: Scale degrees are centered around C3 (+48) with octave offset:
  $$\text{midiNote} = \text{scale}[\text{scaleIndex}][\text{noteIndex} + \text{harmony}] + 48 + \text{static\_cast<int>}(\text{octaveOffset})$$
- **Scale Degree Unique-Rank Cache**: `setScaleTable()` precomputes `scaleUniqueCounts`, `scaleIndexToRank`, and `scaleUniqueIndexList` during non-realtime setup to enable $O(1)$ scale degree traversal without runtime search loops.

### 5.3 Gate-Controlled Pitch Commit
To prevent audible pitch clicks and glitches when release tails ring out after a sequencer step transition, pitch changes are **committed to oscillators only when `state.isGateHigh == true`**. When the gate is low, the active voice rings out at its last assigned frequency.

---

## 6. Usage Examples

### 6.1 Creating and Initializing Voices

```cpp
#include "src/voice/VoiceManager.h"
#include "src/pico2seq-core/scales/scales.h"

// Instantiate VoiceManager for 4 polyphonic voices
VoiceManager voiceManager(4);

// Add voices using factory presets
uint8_t v1 = voiceManager.addVoice("analog");
uint8_t v2 = voiceManager.addVoice("bass");
uint8_t v3 = voiceManager.addVoice("lead");
uint8_t v4 = voiceManager.addVoice("percussion");

// Initialize DSP with 48kHz audio sample rate
voiceManager.init(48000.0f);
```

### 6.2 Updating Voice State from Sequencer Step

```cpp
// Called on Core 1 when sequencer triggers a step
VoiceState newState;
newState.noteIndex = 12.0f;           // 12th step in scale
newState.velocityLevel = 0.85f;       // 85% velocity
newState.filterCutoff = 0.6f;         // 60% filter cutoff
newState.isGateHigh = true;           // Gate ON
newState.hasSlide = false;
newState.octaveOffset = 0.0f;
newState.gateLengthTicks = 60;        // 60 PPQN ticks

voiceManager.updateVoiceState(v1, newState);
```

### 6.3 Real-Time Per-Sample Audio Loop (Core 0)

```cpp
// Called per sample inside fill_audio_buffer() on Core 0
float sample = voiceManager.processAllVoices();
int16_t pcm16 = FloatToPcm16(sample);
```
