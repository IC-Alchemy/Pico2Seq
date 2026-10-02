# Voice Module Documentation

For adding sounds, start with the [voice and preset extension guide](../src/voice/README.md).

## 1. Overview

The voice module provides a comprehensive synthesizer voice system with multi-oscillator synthesis, selectable ladder/state-variable filtering, effects processing, lock-free parameter staging, and preset management. It is designed specifically for the dual-core Raspberry Pi Pico 2 (RP2350) architecture and integrates with the sequencer and UI systems.

### 1.1 Architecture Components

The voice system consists of several key components:

- **`Voice`**: Individual synthesizer voice encapsulating oscillators, a main filter (ladder or state-variable, per `filterType`), high-pass filter, ADSR envelope, overdrive waveshaper, and lock-free parameter/pitch staging.
- **`VoiceManager`**: Manages multiple voices with allocation, deallocation, per-voice mix levels and unified block audio processing. The summed bus passes through `MasterDelay`, `MasterReverb` (mono in, stereo out), master volume, then the linked stereo glue compressor (`rpdsp::Compressor::processStereo()`, last DSP before the DAC); see `VoiceManager::processStereoBlock()` (`processBlock()` is the mono compatibility wrapper). The compressor is driven by the master macro knob (Shift + fader 3): 0 = Warm/Glue/Leveler, 0.5 = Neutral/Mild Glue, 1 = Punch/Smash/Pump (`settingsForMacro()`); the audio thread eases toward the fader target so moves never step the output. Fader 1 is tempo (Shift: delay feedback) and fader 2 is delay mix (Shift: delay time). The macro position is performance state, not part of the session snapshot.
- **`VoiceSystem`**: Centralized structure consolidating voice IDs and control-core state snapshots into arrays for `MAX_VOICES = 4` voices.
- **`VoicePresets`**: Registry of 29 presets, built from grouped preset headers and one `PresetBank.h` list. Fourteen recipe presets cover FM, phase distortion, DSF, formants, ring modulation, reversing sync and spectral/chaotic synthesis. See the [musical preset bank](../src/voice/README.md#musical-preset-bank) for the latest eight sounds and their controls.
- **`VoiceOscillator`**: Variant-based dispatcher decoupling numeric waveform IDs from `rpdsp` oscillator classes.

### 1.2 VoiceSystem Centralization

The `VoiceSystem` struct provides centralized voice tracking. Each sequencer
owns note duration; PPQN expiry publishes gate-off through `VoiceManager`.
There are no separate gate timers or MIDI trackers in this structure. See
[VoiceSystem ownership](VoiceSystem.md#3-ownership-and-routing).


```cpp
struct VoiceSystem {
    static constexpr uint8_t MAX_VOICES = 4;

    uint8_t voiceIds[MAX_VOICES] = {0, 0, 0, 0};
    VoiceState voiceStates[MAX_VOICES];

    uint8_t getVoiceId(uint8_t voiceIndex) const;
    void setVoiceId(uint8_t voiceIndex, uint8_t voiceId);

    VoiceState& getVoiceState(uint8_t voiceIndex);
    const VoiceState& getVoiceState(uint8_t voiceIndex) const;

};

extern VoiceSystem voiceSystem;
```

---

## 2. Public Classes and APIs

### 2.1 `VoiceConfig` Structure

Defined in `src/voice/VoiceConfig.h`:

```cpp
enum VoiceEngine : uint8_t {
    ENGINE_OSC = 0,       // Up to 3 oscillators (or raw noise when oscillatorCount == 0)
    ENGINE_WAVEGUIDE = 1, // Karplus-Strong plucked string (rpdsp::PluckedStringVoice)
    ENGINE_NOISEFX = 2,   // Noise + chaos source through diffuser/swarm inserts
    ENGINE_HYPERSAW = 3,  // One rpdsp::Hypersaw (internally seven detuned saw voices)
    ENGINE_RECIPE = 4,    // Fixed-state rpdsp patch
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
    uint8_t engine = ENGINE_OSC;                                            // ENGINE_OSC, ENGINE_WAVEGUIDE, ENGINE_NOISEFX, or ENGINE_HYPERSAW
    uint8_t paramSet = PARAMSET_STANDARD;                                   // Sequencer-slot re-purposing (STANDARD/WAVEGUIDE/HYPERSAW/NOISESTORM/HARDSYNC)

    const VoiceParameterLayout *parameters = nullptr; // Immutable layout in flash
    const VoiceRecipe *recipe = nullptr;              // Immutable patch descriptor
    float macro1 = 0.5f, macro2 = 0.5f, macro3 = 0.5f; // Mapped recipe controls

    // Waveguide engine parameters (ENGINE_WAVEGUIDE only)
    float wgT60 = 2.5f;                                                     // String tail T60 in seconds (0.05-10.0)
    float wgBrightness = 0.7f;                                              // Loop damping: 0 dark nylon .. 1 glassy (0.0-1.0)
    float wgPickPosition = 0.25f;                                           // Pick point on string (0.02 bridge .. 0.5 middle)
    float wgPickHardness = 0.8f;                                            // Excitation burst: 0 soft felt .. 1 hard pick
    float wgStiffness = 0.0f;                                               // Inharmonic dispersion: 0 harmonic .. 1 bell-like
    float wgDetune = 6.0f;                                                  // Two-string course spread in cents (0.0-30.0)

    // Hypersaw engine parameters (ENGINE_HYPERSAW only)
    float hypersawDetune = 0.2f;                                            // Seven-voice detune amount (0.0-1.0)
    float hypersawMix = 0.5f;                                               // Center/side mix amount (0.0-1.0)

    // Noise-FX engine parameters (ENGINE_NOISEFX only)
    float noiseDiffuseSize = 0.8f;                                          // Prime-tap diffuser smear (0.0-1.0)
    float noiseDiffuseMix = 0.7f;                                           // Diffuser wet amount (0.0-1.0)
    float noiseSwarmColor = 0.5f;                                           // Allpass swarm tone (0.0-1.0)
    float noiseSwarmRegen = 0.9f;                                           // Allpass swarm regeneration (0.0-1.2)
    float noiseChaosLevel = 0.35f;                                          // Pitch-tracked chaos_lorenz growl mix (0.0-1.0)

    // Filter settings. filterType picks the topology; filterDrive and
    // filterPassbandGain only affect the ladder and are ignored by the SVF.
    // filterMode is voice-owned: ladder voices map it to a native ladder
    // mode, while SVF voices select the matching LP/BP/HP output.
    uint8_t filterType = FILTER_LADDER;                                     // Main filter topology (FILTER_LADDER or FILTER_SVF)
    float filterRes = 0.2f;                                                 // Filter resonance (0.0-1.0)
    float filterDrive = 1.8f;                                               // Ladder drive amount (0.0-4.0; SVF ignores)
    float filterPassbandGain = 0.23f;                                       // Ladder passband gain compensation (0.0-0.5; SVF ignores)
    VoiceFilterMode filterMode = VoiceFilterMode::LP24;                     // Filter mode (LP24, LP12, BP24, BP12, HP24, HP12)
    float filterCutoffBase = 0.37f;                                         // Normalized static cutoff used when paramSet re-purposes the Filter slot

    // High-pass filter settings
    float highPassFreq = 80.0f;                                             // High-pass cutoff frequency in Hz (20.0-20000.0)
    float highPassRes = 0.1f;                                               // High-pass resonance (0.0-1.0)

    // Effects chain configuration
    bool hasOverdrive = false;                                              // Enable overdrive effect
    bool hasEnvelope = true;                                                // Enable envelope (recommended: true)
    bool hasFilter = true;                                                  // Enable the main filter (false = bypass, velocity scales output)
    float overdriveGain = 0.34f;                                            // Overdrive output gain (0.0-2.0)
    float overdriveDrive = 0.25f;                                           // Overdrive drive amount (0.0-1.0)

    // Envelope default settings
    float defaultAttack = 0.04f;                                            // Default attack time in seconds (0.001-10.0)
    float defaultDecay = 0.14f;                                             // Default decay time in seconds (0.001-10.0)
    float defaultSustain = 0.5f;                                            // Default sustain level (0.0-1.0)
    float defaultRelease = 0.2f;                                            // Default release time in seconds (0.001-10.0)

    // Voice mixing
    float outputLevel = 0.6f;                                               // Voice output level (0.0-1.0)
    bool enabled = true;                                                    // Voice enabled state
};
```

#### UI Filter Modes (`voiceui` namespace)
```cpp
namespace voiceui {
inline constexpr VoiceFilterMode kFilterModes[] = {
    VoiceFilterMode::LP24, VoiceFilterMode::LP12,
    VoiceFilterMode::BP24, VoiceFilterMode::BP12,
    VoiceFilterMode::HP24, VoiceFilterMode::HP12
};
inline constexpr const char* kFilterModeNames[] = {"LP24", "LP12", "BP24", "BP12", "HP24", "HP12"};
inline constexpr int kFilterModeCount = 6;
}
// Mode cycling (ButtonHandlers/UIEventHandler) writes filterMode for both
// filter topologies: on SVF voices LP* selects the lowpass output, BP* the
// bandpass output, HP* the highpass output (12/24 dB suffix is ladder-only).
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

    // Real-time audio processing (runs on Core 1 @ 48kHz)
    float process() noexcept;

    // Parameter updates (called on the Core 0 control thread — uClock step drain
    // in processClockEvents(), or live recording). liveEnvelopeMask makes
    // deliberate live slider edits land on the running envelope stage.
    void updateParameters(const VoiceState& newState, uint8_t liveEnvelopeMask = 0);

    // Sequencer integration
    void setSequencer(std::unique_ptr<Sequencer> seq);
    void setSequencer(Sequencer* seq);
    Sequencer* getSequencer() noexcept;

    // Scale injection (removes global scale coupling). nativeScaleMask bit N
    // marks row N as holding tuning degrees instead of 12-EDO semitone slots.
    void setScaleTable(const int (*table)[48], size_t scaleCount,
                       uint64_t nativeScaleMask = 0);
    void setCurrentScalePointer(const uint8_t* currentScalePtr);

    // Global tuning injection (tuning/Tuning.h). The control thread samples the
    // selection and queues a PitchWorld; the default 12-EDO / tonic C / A4 440
    // selection keeps the historical pitch path bit for bit.
    void setTuningPointer(const tuning::Selection* selection);

    // State and gate management
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
    float noteIndex = 0.0f;                                                   // Scale degree (0-36) for the scale-table lookup
    float velocityLevel = 0.5f;                                               // Voice amplitude (0.0-1.0); hard-sync presets use this centered value as zero slave-frequency offset
    float filterCutoff = 0.37f;                                               // Filter cutoff frequency (0.0-1.0 normalized)
    float attackTimeSeconds = 0.01f;                                          // Envelope attack time (0.0-1.0s)
    float decayTimeSeconds = 0.2f;                                            // Envelope decay time: fall toward sustain
    float sustainLevel = 0.5f;                                                // Held level while the gate stays high (0.0-1.0)
    float releaseTimeSeconds = 0.3f;                                          // Ring-out after gate off (normalized)
    int8_t octaveOffset = 0;                                                  // Transpose in semitones from the Octave lane (-24..+24)
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
inline constexpr uint8_t WAVE_HARDSYNC_SAW = 6; // Band-limited master/slave hard-sync saw
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
        rpdsp::HardSyncSaw,
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

    // Voice Configuration
    bool setVoiceConfig(uint8_t voiceId, const VoiceConfig& config);
    bool setVoicePreset(uint8_t voiceId, const std::string& presetName);
    const VoiceConfig* getVoiceConfig(uint8_t voiceId);

    // Voice State Management
    bool updateVoiceState(uint8_t voiceId, const VoiceState& state,
                          uint8_t liveEnvelopeMask = 0);
    const VoiceState* getVoiceState(uint8_t voiceId);
    void flushControlUpdates(); // control thread, every loop including idle passes

    // Sequencer Attachment
    bool attachSequencer(uint8_t voiceId, std::unique_ptr<Sequencer> sequencer);
    bool attachSequencer(uint8_t voiceId, Sequencer* sequencer);
    Sequencer* getSequencer(uint8_t voiceId);

    // Audio Processing
    void init(float sampleRate);
    void processStereoBlock(float *left, float *right, uint32_t n) noexcept; // firmware path
    void processBlock(float *out, uint32_t n) noexcept; // mono: 0.5 * (left + right)
    float processAllVoices() noexcept; // one-sample wrapper

    // Master reverb (Core 0 setters; lock-free targets, audio-owned state)
    void setReverbMix(float value) noexcept;
    void setReverbDecaySeconds(float value) noexcept;
    void setReverbDampingHz(float value) noexcept;
    void setReverbLowCutHz(float value) noexcept;
    void setReverbDiffusion(float value) noexcept;
    void setReverbModDepth(float value) noexcept;
    void setReverbModRateHz(float value) noexcept;
    void setReverbWidth(float value) noexcept;
    void setReverbFreeze(bool frozen) noexcept;
    bool applyReverbSettings(const ReverbSettings &settings) noexcept; // coherent snapshot; false = ring full,
                                                                      // the values still arrive via the targets
    ReverbSettings getReverbSettings() const noexcept;                 // newest published

    // Master-bus delay and compressor macro (control-thread targets; the audio
    // thread reads them per block and eases per sample)
    void setDelayMix(float mix);
    void setDelayTime(float seconds);
    void setDelayFeedback(float feedback);
    void setDelaySynced(bool synced);
    void setDelayNoteIndex(uint8_t index);
    void setMasterMacro(float macro);
    float getMasterMacro() const;

    // Voice Control
    void enableVoice(uint8_t voiceId, bool enabled = true);
    void disableVoice(uint8_t voiceId);
    bool isVoiceEnabled(uint8_t voiceId) const;

    // Voice Information
    uint8_t getVoiceCount() const;
    uint8_t getMaxVoices() const;
    bool hasAvailableSlots() const;

    // Callbacks
    void setVoiceCountCallback(VoiceCountCallback callback);
    void setVoiceUpdateCallback(VoiceUpdateCallback callback);

    // Preset Management
    static std::vector<std::string> getAvailablePresets();
    static VoiceConfig getPresetConfig(const std::string& presetName);

    // Global Mixing
    void setGlobalVolume(float volume);
    float getGlobalVolume() const;
    void setVoiceSlide(uint8_t voiceId, float slideTime);
};
```

---

## 3. Verified Preset System

Declared in `src/voice/VoicePresets.h`; factories live in `src/voice/presets/`.
The compile-time registry in `VoicePresets.cpp` expands `PresetBank.h` to pair
names and configs in flash. Appending one bank entry updates count and lookups.
Unknown indices/config names fall back to Analog; unknown display indices return
"Unknown". Existing per-preset getters remain available. Name matching is
case-insensitive, and `VoiceManager::getAvailablePresets()` derives its list from
the same bank. The bank currently holds 29 presets (indices 0–28): the 15 original
presets are detailed below, followed by six recipe presets (15–20) and eight musical
presets (21–28) described in the [voice and preset extension guide](../src/voice/README.md)
and [musical preset bank](../src/voice/README.md#musical-preset-bank).

| # | Preset Name | Engine | Oscillators | Amplitudes | Detune (Semis) | Harmony | Filter Mode | Filter Settings | Overdrive | Envelope (A/D/S/R) | Output Level |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **0** | **Analog** | osc | 1x `WAVE_HARDSYNC_SAW` | `[1.0]` | `[0.0]` | `[0]` | **LP24** (ladder) | Res: 0.33, Drive: 2.1, Passband: 0.23, HPF: 120 Hz | Off (Gain: 0.8, Drive: 0.25) | `0.07s / 0.24s / 0.5 / 0.16s` | `0.5` |
| **1** | **Digital** | osc | 2x `WAVE_BSP_SQUARE` | `[0.75, 0.65]` | `[0.0, +0.01]` | `[0, 0]` | **LP12** (SVF) | Res: 0.40, SVF low-pass, HPF: 111 Hz (Res: 0.15) | Off (Gain: 0.7, Drive: 0.51) | `0.015s / 0.1s / 0.5 / 0.15s` | `0.5` |
| **2** | **Bass** | osc | 2x (`WAVE_SIN`, `WAVE_TRI`) | `[1.0, 1.0]` | `[-12.0, 0.0]` | `[0, 0]` | **LP12** (SVF) | Res: 0.45, SVF low-pass, HPF: 45 Hz (Res: 0.4) | On (Gain: 0.95, Drive: 0.16) | `0.01s / 0.3s / 0.85 / 0.2s` | `0.85` |
| **3** | **Lead** | osc | 2x `WAVE_BSP_SAW` | `[0.6, 0.55]` | `[0.0, +0.015]` | `[0, 0]` | **LP24** (ladder) | Res: 0.70, Drive: 3.5, Passband: 0.23, HPF: 160 Hz | Off (Gain: 0.7, Drive: 0.45) | `0.01s / 0.2s / 0.3 / 0.35s` | `0.5` |
| **4** | **Square** | osc | 1x `WAVE_BSP_SQUARE` (PW: 0.2) | `[1.0]` | `[0.0]` | `[0]` | **BP24** (SVF) | Res: 0.60, SVF band-pass, HPF: 150 Hz | Off (Gain: 0.75, Drive: 0.35) | `0.02s / 0.4s / 0.0 / 0.25s` | `0.56` |
| **5** | **Pad** | osc | 3x `WAVE_BSP_SAW` | `[0.33, 0.33, 0.33]` | `[0.0, 0.0, 0.0]` | `[0, +4, +9]` | **LP12** (SVF) | Res: 0.30, SVF low-pass, HPF: 140 Hz (Res: 0.08) | Off (Gain: 0.85, Drive: 0.25) | `0.4s / 0.2s / 0.5 / 0.5s` | `0.5` |
| **6** | **Percussion** | osc | **0 oscs** (`WAVE_NOISE`, `NoiseOscillator`) | `[1.0]` | `[0.0]` | `[0]` | **LP24** (SVF) | Res: 0.40, SVF low-pass, HPF: 200 Hz | Off (Gain: 0.45, Drive: 0.30) | `0.005s / 0.08s / 0.0 / 0.07s` | `0.5` |
| **7** | **SubFunk** | osc | (`WAVE_SIN`, `WAVE_BSP_SQUARE`, `WAVE_SIN`) | `[1.0, 0.35, 0.65]` | `[-12.0, -12.0, 0.0]` | `[0, 0, 0]` | **LP12** (SVF) | Res: 0.60, SVF low-pass, HPF: 25 Hz | On (Gain: 0.9, Drive: 0.45) | `0.004s / 0.22s / 0.35 / 0.12s` | `0.9` |
| **8** | **RubberSub** | osc | (`WAVE_SIN`, `WAVE_BSP_SQUARE`, `WAVE_TRI`) | `[0.9, 0.3, 0.5]` | `[-12.0, -12.0, 0.0]` | `[0, 0, 0]` | **BP24** (SVF) | Res: 0.70, SVF band-pass, HPF: 25 Hz | On (Gain: 1.0, Drive: 0.55) | `0.002s / 0.16s / 0.25 / 0.09s` | `0.85` |
| **9** | **WgPluck** | waveguide | — (wg: T60 1.8s, bright 0.78, pick 0.26/0.85, stiff 0.0, det 4c) | — | — | — | **none** (`hasFilter=false`) | ladder bypassed; sub-shed HPF 55 Hz | Off | **none** (`hasEnvelope=false`; natural T60 ring) | `0.85` |
| **10** | **WgNylon** | waveguide | — (wg: T60 3.2s, bright 0.28, pick 0.42/0.22, stiff 0.05, det 9c) | — | — | — | **none** (`hasFilter=false`) | ladder bypassed; sub-shed HPF 66 Hz | Off | **none** (`hasEnvelope=false`; natural T60 ring) | `0.9` |
| **11** | **WgBell** | waveguide | — (wg: T60 1.4s, bright 0.9, pick 0.08/1.0, stiff 0.88, det 0c) | — | — | — | **none** (`hasFilter=false`) | ladder + HPF bypassed | Off | **none** (`hasEnvelope=false`; natural T60 ring) | `0.75` |
| **12** | **WgShimmer** | waveguide | — (wg: T60 6.5s, bright 0.55, pick 0.35/0.6, stiff 0.15, det 26c) | — | — | — | **none** (`hasFilter=false`) | ladder + HPF bypassed | Off | **none** (`hasEnvelope=false`; natural T60 ring) | `0.8` |
| **13** | **Hypersaw** | hypersaw | one `rpdsp::Hypersaw` (seven internal saws) | — | Detune: 0.2 (native 0–1) | — | **LP24** (SVF) | Res: 0.35, SVF low-pass, HPF: 180 Hz | Off | `0.012s / 0.3s / 0.8 / 0.25s` | `0.5` |
| **14** | **NoiseStorm** | noise-FX | — (nf: diffuse 0.85/0.65, swarm 0.6/0.95, chaos 0.4) | — | — | — | **LP24** (SVF) | Res: 0.72, SVF low-pass, HPF: 220 Hz | On (Gain: 0.8, Drive: 0.4) | `0.003s / 0.5s / 0.55 / 0.45s` | `0.45` |

Filter topology: only **Analog** and **Lead** still run the `rpdsp::LadderFilter`
(one of the few invariants the host test suite pins by count). All other
filtered presets use the TPT `rpdsp::StateVariableFilter` — chosen for stability
under the envelope's cutoff sweeps and its resonant low-pass/band-pass character
(especially on the three bass presets); its response is selected by the same
`VoiceFilterMode` values (LP→lowpass, BP→bandpass, HP→highpass), and it ignores `filterDrive`/
`filterPassbandGain`.

The eight presets added with the expansion bank: **SubFunk** — bouncy sub bass; a sine sub an octave down carries the weight, a triangle adds movement, and a resonant SVF low-pass plus warm overdrive grit gives the filtered-growl funk character. **RubberSub** — rubbery sub bass; a sub-octave square grinds under a sine through a resonant SVF band-pass ("rubbery honk"), with harder overdrive that spits on transients. **WgPluck** — classic Karplus-Strong plucked string: bright burst, harmonic loop, short natural tail. **WgNylon** — dark felt-soft nylon: heavily damped loop, gentle pick, long sympathetic tail. **WgBell** — stiff dispersive string whose inharmonic upper partials read as bell/kalimba; hard bridge pick, quick tail. **WgShimmer** — wide-detuned (26-cent) two-string course with a very long T60 tail; slow chorusing sustain turns the pluck into a ringing pad. **Hypersaw** — one native seven-voice `rpdsp::Hypersaw`; its Detune and Mix sequencer slots drive the engine directly, under a wide-open SVF low-pass. **NoiseStorm** — noise-based texture: noise plus a pitch-tracked Lorenz chaos growl feed a prime-tap diffuser and a regenerative allpass swarm, then a resonant SVF low-pass pings with the envelope.

Preset 9-12 use `engine = ENGINE_WAVEGUIDE` (`rpdsp::PluckedStringVoice`, 2048-sample
delay): each gate rise (or retrigger) plucks the string at the current base pitch, and
the `wg*` config fields tune T60, loop brightness, pick position/hardness, stiffness
(inharmonic dispersion), and two-string course detune. The waveguide presets also set
`hasFilter = false` and `hasEnvelope = false`: the main filter and ADSR are both
bypassed, velocity scales the pluck excitation itself (soft picks inject less energy,
and the ringing tail is never rescaled by later velocity changes), and the string
rings past gate fall on its own T60 (gate edges still arm plucks — see
`handleGateEdges_()`). String tuning lands only on note starts: each gate
rise (or retrigger) re-pushes the `wg*` bases with a small per-note
humanization (±4% multiplicative, under the 5% musical ceiling; exact zeros
stay zero so unison/dry settings never drift), and edits made while a note
rings wait for the next gate-on instead of retuning the live loop.
WgPluck/WgNylon keep a gentle 55/66 Hz high-pass to shed subsonic rumble that
Karplus tails otherwise accumulate; WgBell/WgShimmer bypass the high-pass too.
Preset 13 uses `engine = ENGINE_HYPERSAW`: one `rpdsp::Hypersaw` instance supplies
its internal seven saw voices. Its Attack and Decay sequencer slots are re-purposed
as normalized Detune and Mix controls, respectively; Filter remains the live cutoff.

Preset 0, Analog, uses one `WAVE_HARDSYNC_SAW`. Its Note/Master track sets the
master frequency. Its Velocity/Slave track is centered at 0.5 and maps to a
slave offset of -24 to +24 semitones: the untouched/default value of 0.5 is a
zero offset, so the slave follows the master exactly. Hard-sync presets do not
apply that re-purposed lane as VCA velocity.

Preset 14 uses `engine = ENGINE_NOISEFX`: `NoiseOscillator` plus a pitch-tracked
`chaos_lorenz` growl feed `fx_diffuse` (prime-tap diffuser) and `fx_swarm` (regenerative
allpass swarm) from `rpdsp/DSPFunctions.h`, pre-filter so the SVF shapes the texture.

### Per-preset sequencer parameter sets

`VoiceParameters` owns the mapping, seeding and display metadata. Existing
`VoiceConfig::paramSet` values select compatible layouts; new recipe presets
provide an immutable `VoiceConfig::parameters` layout. `Voice::applyParameters_()`
applies bindings on the audio thread. `VoicePresets::getSequencerParamName()` and
`VoiceParameters::formatValue()` supply OLED labels/units. Startup and
`applyVoicePreset()` call `VoiceParameters::seedTracks()` so a preset's mapped
values survive the first sequencer update.

| Param set | Presets | Note / Velocity slots | Filter slot | Attack slot | Decay slot |
|---|---|---|---|---|---|
| HARDSYNC | 0 | Master pitch / Slave offset (-24..+24 st; 0.5 = follow master) | Cutoff | Attack | Decay |
| STANDARD | 1–8 | Note / velocity | Cutoff (120 Hz–5 kHz, EXP) | Attack (0.002–0.75 s) | Decay (0.01–0.5 s, LOG) |
| WAVEGUIDE | 9–12 | Note / velocity | Brightness (0–1) | Pick hardness (0–1) | T60 (0.05–10 s, EXP; seeded via `wgT60ToNormalized`) |
| HYPERSAW | 13 | Note / velocity | Cutoff (live) | Native seven-voice detune (0–1) | Native center/side mix (0–1) |
| NOISESTORM | 14 | Note / velocity | Swarm color | Swarm regen | Chaos level (the SVF keeps the preset's static `filterCutoffBase`) |

For HYPERSAW/NOISESTORM the ADSR times come from the preset defaults (`applyEnvelopeDefaults_()`),
since the Attack/Decay tracks no longer carry envelope times. Live preset switches are
gate-safe: scalar config applies immediately, but the oscillator rebuild and engine
reset are deferred until the gate falls (`applyStructuralConfig_()`), so swapping
presets while playing never clicks a held note or cuts a ringing tail.

---

## 4. DSP Processing Pipeline & Signal Flow

Control setters enqueue snapshots without reading or mutating applied DSP state.
While updates are queued, `processBlock()` consumes one update and renders one
sample, even when disabled. With an empty queue it renders spans of up to 32
samples before checking again. A control published during a span waits at most
0.67 ms at 48 kHz. `process()` remains a one-sample wrapper. Control code
calls `flushControlUpdates()` every loop to retry full queues and sample scale
selection. Only unpublished updates may coalesce under overload; published slots
cannot be overwritten until audio finishes copying them. See
[cross-core ownership](architecture.md#3-cross-core-ownership-and-bounded-queues)
for overflow policy, table lifetimes, and getter ownership. UI getters on
`VoiceManager` return const requested copies; applied getters on `Voice` are
restricted to the audio thread or quiescent tests.

Each span runs these stages on Core 1. All sample scratch belongs to the voice,
so no per-span arrays use Core 1's 2 KiB stack.

1. `handleGateEdges_()` consumes gate rise/fall and retrigger, including pluck
   triggers when the ADSR is bypassed. The ADSR then generates each sample.
2. Apply deferred structural configuration if the gate is low.
3. `planFilterUpdates_()` advances cutoff smoothing per sample. The target is
   `filterFrequency * 2^(filterEnvOctaves_ * (env - filterEnvelopeRest))` -
   exponential in pitch, like an analog VCF's V/oct input, recomputed at the
   setFreq rate and smoothed per sample in between. At `env == filterEnvelopeRest`
   the cutoff is exactly what the Filter lane dialed, so the sequenced cutoff
   stays audible instead of being replaced by the contour. `filterEnvOctaves_`
   is `filterEnvelopeOctaves` scaled +/-30% by the Filter lane, so one sweep
   both raises the cutoff and deepens the envelope.
   (Before 2026-09-19 this was linear: `env * amount + floor`, which dropped a
   released note to a tenth of its cutoff and made voices inaudible unless the
   gate was long.)
   Coefficient updates retain their every-eight-samples throttle and change
   threshold; their exact sample indices are recorded in fixed storage.
**The Filter lane is the envelope amount, not the cutoff** (2026-09-19). The
cutoff frequency comes from `filterCutoffBase` alone - the encoder's Filter
target - and the sequenced lane scales `filterEnvelopeOctaves` from 0 (cutoff
parked on the base) to the preset's full sweep. `filterEnvelopeRest` defaults to
0, so the contour only opens upward from the base, the way an analog VCF's
contour amount works. The OLED shows the lane as `<amount>% <peak Hz>` and the
patch base as plain Hz (`MusicalValues::format(..., baseView)`).

4. `renderSources_()` selects the engine once per span. With an envelope,
   samples at or below `0.001f` leave the source and pending pitch commit alone.
   Oscillator-bank pitch commits require a high gate; slides advance every
   sounding sample. Each oscillator renders into the mix in its original order.
5. Apply envelope gain, then overdrive/NoiseStorm effects when needed, then
   velocity (amplitude remains 1 for layouts that repurpose velocity).
6. `runMainFilter_()` processes segments between coefficient updates, applying
   each update before its sample. Then apply the optional HPF and output level.
7. Track consecutive output samples below `1e-6` for the silent skip.

A voice can skip DSP after 256 quiet samples only with an idle ADSR, released
gate, no pending gate edge/retrigger or structural change, and an envelope.
Waveguide and NoiseStorm engines stay active so their internal tail state keeps
evolving. Skipped spans still advance cutoff smoothing and its update counter;
every configuration change resets the quiet count. Set `P2S_VOICE_IDLE_SKIP=0`
to compare the full renderer. The skip can leave tiny frozen filter/HPF states;
the resulting next-note differences are checked by PCM16 null tests.

`VoiceManager::processBlock()` sums voice blocks in the original voice order.
Per-voice mix, master-volume and mute targets are read once per block of up to
256 frames (5.33 ms at 48 kHz). Master smoothing still advances every sample.
`processStereoBlock()` renders the voices into the left buffer, runs the delay in
place, then hands 64-frame quanta of that mono bus to `MasterReverb`, which returns
the stereo blend. Master gain and the compressor macro apply to both channels and the
compressor is linked (one detector on `max(|L|,|R|)`, one gain for both). With the
reverb mix at zero the two channels are identical, so the bus equals the old mono bus
bit for bit. `AudioSamples::interleavePcm16()` clamps and truncates left and right
separately into the I2S buffer. Block APIs overwrite their output and accept
zero-length calls; the mono `processBlock()` remains for callers and tests.

---

## 5. Musical Scale & Pitch Architecture

### 5.1 Static Precomputed Lookup Table
- `frequencyLookupTable[128]`: Initialized once via `std::call_once` covering MIDI notes 0–127 using `rpdsp::midiNoteToHz()`.

### 5.2 Scale Data Injection
Scale tables are injected via dependency injection, eliminating global couplings:
```cpp
extern int scale[SCALES_COUNT][SCALE_STEPS];  // 47 rows (semitone slots or tuning degrees), 48 steps
extern uint8_t currentScale;

voice->setScaleTable(scale, SCALES_COUNT, NATIVE_SCALE_MASK);
voice->setCurrentScalePointer(&currentScale);
voice->setTuningPointer(&tuningSelection);
```
- **Single pitch lookup path**: `calculateNoteFrequency()` reads the **injected** table through `scaleTable[effectiveScaleIndex_()][noteIndex + harmony]`. With no table injected (`nullptr`), it falls back to **chromatic mapping** (scale step = semitone above C3).
- **Synthesis Pitch Offset**: Scale degrees are centered around C3 (+48) with octave offset:
  $$\text{midiNote} = \text{scaleTable}[\text{scaleIndex}][\text{noteIndex} + \text{harmony}] + 48 + \text{static\_cast<int>}(\text{octaveOffset})$$
- **Index clamping**: `noteIndex + harmony` is clamped to `0..47` and the resulting MIDI note is saturated to `0..127` before the lookup-table read, so extreme harmony/octave values cannot index out of bounds.
- **Live scale switches**: the effective scale row is part of the pitch snapshot (`PitchSnapshot::scaleIndex`); a runtime `currentScale` change invalidates the static base frequency on the next pitch recompute (repeated notes repitch too).
- No per-scale preprocessing happens at injection time — `setScaleTable()` only stores the pointer and marks the base frequency dirty (the former unique-rank caches were write-only and were removed 2026-09-05).
- **Global tuning injection**: an additional injected pointer is the global tuning
  selection (`tuning/Tuning.h`; 29 tunings plus tonic and A4 reference, selected on
  the Tuning page). The control thread samples it in `flushControlUpdates()` and
  queues a `PitchWorld` exactly like a scale change. The default selection
  (12-EDO, tonic C, A4 440) sets `PitchWorld::standard` and keeps the MIDI-table
  path above bit for bit; any other selection makes `calculateNoteFrequency()`
  map the scale row's value to a tuning degree (`MusicalValues::tuningDegree()`)
  and compute the frequency from the tonic and A4 instead of the lookup table.
  Rows flagged in `NATIVE_SCALE_MASK` already hold tuning degrees (the All
  Degrees scale); every other row holds 12-EDO semitone slots mapped to
  degrees by the tuning's chroma table, or the nearest degree when the tuning
  has no chroma.

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
// Called on Core 0 when loop() drains a sequencer step (never the ISR)
VoiceState newState;
newState.noteIndex = 12.0f;           // 12th step in scale
newState.velocityLevel = 0.85f;       // 85% velocity
newState.filterCutoff = 0.6f;         // 60% filter cutoff
newState.isGateHigh = true;           // Gate ON
newState.hasSlide = false;
newState.octaveOffset = 0;            // Semitone transpose
newState.gateLengthTicks = 60;        // 60 PPQN ticks

voiceManager.updateVoiceState(v1, newState);
```

### 6.3 Real-Time Block Audio Loop (Core 1)

```cpp
// Fixed Core 1 scratch, outside the stack (one buffer per channel).
static std::array<float, 256> left, right;
voiceManager.processStereoBlock(left.data(), right.data(), left.size());
AudioSamples::interleavePcm16(left.data(), right.data(), out, left.size());
// = for each frame: out[2*i] = toPcm16(left[i]); out[2*i+1] = toPcm16(right[i]);
```
