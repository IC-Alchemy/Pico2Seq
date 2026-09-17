# Voice Module Documentation

For adding sounds, start with the [voice and preset extension guide](../src/voice/README.md).

## 1. Overview

The voice module provides a comprehensive synthesizer voice system with multi-engine source synthesis, overdrive effects processing, lock-free parameter staging, and preset management. Since the drone build re-purposed the firmware as a Eurorack oscillator voice, voices carry no main filter and no amplitude envelope: every engine renders continuously (drone), and raw waveforms leave the voice for external shaping. It is designed specifically for the dual-core Raspberry Pi Pico 2 (RP2350) architecture and integrates with the sequencer, UI, and MIDI systems.

### 1.1 Architecture Components

The voice system consists of several key components:

- **`Voice`**: Individual synthesizer voice encapsulating the engine sources (oscillator bank, waveguide, noise-FX, Hypersaw, recipe), an overdrive waveshaper, a waveguide-only sub-shedding high-pass, and lock-free parameter/pitch staging. The per-voice main filter and ADSR envelope were removed with the drone build; voices render continuously, and gate edges only fire engine triggers (waveguide plucks, Hypersaw phase randomization, recipe resets) and commit pitch.
- **`VoiceManager`**: Manages multiple voices with allocation, deallocation, master volume scaling, per-voice mix levels, and unified block audio processing.
- **`VoiceSystem`**: Centralized structure consolidating voice IDs, states, gates, and gate countdown timers into arrays for `MAX_VOICES = 4` voices.
- **`VoicePresets`**: Registry of 29 presets, built from grouped preset headers and one `PresetBank.h` list. Fourteen recipe presets cover FM, phase distortion, DSF, formants, ring modulation, reversing sync and spectral/chaotic synthesis. See the [musical preset bank](../src/voice/README.md#musical-preset-bank) for the latest eight sounds and their controls.
- **`VoiceOscillator`**: Variant-based dispatcher decoupling numeric waveform IDs from `rpdsp` oscillator classes.
- **Supporting Classes**: `VoiceManagerBuilder` and `VoiceFactory` for builder-pattern and pre-configured voice setups.

### 1.2 VoiceSystem Centralization

The `VoiceSystem` struct provides centralized voice tracking:

```cpp
struct VoiceSystem {
    static constexpr uint8_t MAX_VOICES = 4;

    uint8_t voiceIds[MAX_VOICES] = {0, 0, 0, 0};
    VoiceState voiceStates[MAX_VOICES];

    // Gate states and duration countdown timers across all 4 voices (0-3)
    volatile bool gates[MAX_VOICES] = {false, false, false, false};
    GateTimer gateTimers[MAX_VOICES];

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

Defined in `src/voice/VoiceConfig.h`:

```cpp
enum VoiceEngine : uint8_t {
    // Engines share the source -> effects -> velocity -> output chain; drone
    // build: no per-voice filter or amplitude envelope. Only the source stage
    // (and, for the noise engine, the pre-output effect inserts) differs.
    ENGINE_OSC = 0,       // Up to 3 oscillators (or raw noise when oscillatorCount == 0)
    ENGINE_WAVEGUIDE = 1, // Karplus-Strong plucked string (rpdsp::PluckedStringVoice)
    ENGINE_NOISEFX = 2,   // Noise + chaos source through diffuser/swarm inserts
    ENGINE_HYPERSAW = 3,  // One rpdsp::Hypersaw (internally seven detuned saw voices)
    ENGINE_RECIPE = 4,    // Fixed-state rpdsp recipe selected by config.recipe
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
    uint8_t engine = ENGINE_OSC;                                            // ENGINE_OSC, ENGINE_WAVEGUIDE, ENGINE_NOISEFX, ENGINE_HYPERSAW, or ENGINE_RECIPE
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

    // Legacy main-filter and envelope settings. The drone build removed the
    // main filter and the amplitude envelope from the audio path; these fields
    // survive only so PatchCodec keeps round-tripping saved patches unchanged
    // (the binary PatchSnapshot layout is locked). Nothing in the firmware
    // reads them for sound.
    uint8_t filterType = FILTER_LADDER;                                     // Legacy: main filter topology (FILTER_LADDER or FILTER_SVF)
    float filterRes = 0.2f;                                                 // Legacy: filter resonance (0.0-1.0)
    float filterDrive = 1.8f;                                               // Legacy: ladder drive amount
    float filterPassbandGain = 0.23f;                                       // Legacy: ladder passband gain compensation
    VoiceFilterMode filterMode = VoiceFilterMode::LP24;                     // Legacy: filter response (LP24, LP12, BP24, BP12, HP24, HP12)
    float filterCutoffBase = 0.37f;                                         // Legacy: normalized static cutoff
    float filterEnvelopeAmount = 1.0f;                                      // Legacy: envelope-to-cutoff depth
    float filterEnvelopeFloor = 0.1f;                                       // Legacy: envelope-to-cutoff floor

    // High-pass filter settings. Rendered by the waveguide engines only
    // (sub-shedding for the Karplus tails); inert for every other engine.
    float highPassFreq = 80.0f;                                             // High-pass cutoff frequency in Hz (20.0-20000.0)
    float highPassRes = 0.1f;                                               // High-pass resonance (0.0-1.0)

    // Effects chain configuration
    bool hasOverdrive = false;                                              // Enable overdrive effect
    bool hasEnvelope = true;                                                // Legacy flag: drones ignore it (patch format compatibility)
    bool hasFilter = true;                                                  // Legacy flag: drones ignore it (patch format compatibility)
    float overdriveGain = 0.34f;                                            // Overdrive output gain (0.0-2.0)
    float overdriveDrive = 0.25f;                                           // Overdrive drive amount (0.0-1.0)

    // Legacy envelope defaults. Inert since the drone build removed the ADSR;
    // retained for the locked PatchSnapshot layout.
    float defaultAttack = 0.04f;                                            // Default attack time in seconds (0.001-10.0)
    float defaultDecay = 0.14f;                                             // Default decay time in seconds (0.001-10.0)
    float defaultSustain = 0.5f;                                            // Default sustain level (0.0-1.0)
    float defaultRelease = 0.2f;                                            // Default release time in seconds (0.001-10.0)

    // Voice mixing
    float outputLevel = 0.6f;                                               // Voice output level (0.0-1.0)
    bool enabled = true;                                                    // Voice enabled state
};
```

**Patch-format compatibility:** the legacy filter/envelope fields above (including the
`hasFilter`/`hasEnvelope` flags and the A/D/S/R defaults) exist purely so the locked binary
`PatchSnapshot` layout keeps round-tripping saved patches unchanged. Old sessions load
byte-for-byte as before; the fields are audio-inert.

#### Legacy filter enums (`VoiceFilterType`, `VoiceFilterMode`)
Retained only as the storage types of the legacy persisted fields. The former
`voiceui` mode-cycling table (`kFilterModes`/`kFilterModeNames`) and the ButtonHandlers
filter-mode/resonance handlers were removed with the main filter (drone build); no firmware
code selects a filter response anymore.

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

    // Real-time audio processing (runs on Core 1 @ 48kHz)
    float process() noexcept;

    // Parameter updates (called on the Core 0 control thread — uClock step drain
    // in processClockEvents(), or live recording)
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

    // Filter control (setFilterFrequency/getFilterFrequency) removed with
    // the main filter (drone build)

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
    float noteIndex = 0.0f;                                                   // Scale step index (0-21)
    float velocityLevel = 0.5f;                                               // Voice amplitude (0.0-1.0); hard-sync presets use this centered value as zero slave-frequency offset
    float filterCutoff = 0.37f;                                               // Filter lane value (macro input; inert on oscillator presets)
    float attackTimeSeconds = 0.01f;                                          // Attack lane value (macro input; inert on oscillator presets)
    float decayTimeSeconds = 0.01f;                                           // Decay lane value (macro input; inert on oscillator presets)
    float octaveOffset = 0.0f;                                                // Normalized octave offset (0.0=C2, 0.5=C3, 1.0=C4)
    uint16_t gateLengthTicks = SequencerConstants::DEFAULT_GATE_LENGTH_TICKS; // Gate duration (default 60 ticks @ 480 PPQN)
    bool isGateHigh = false;                                                  // Voice gate state (active note on)
    bool hasSlide = false;                                                    // Portamento / slide enable flag
    bool shouldRetrigger = false;                                             // Retrigger flag (fires the engine triggers)
};
```

Since the drone build removed the per-voice filter and ADSR, `filterCutoff`,
`attackTimeSeconds` and `decayTimeSeconds` no longer carry cutoff/envelope times. They
transport the Filter/Attack/Decay lane values; `Voice::applyParameters_()` routes them into
engine macros where a binding exists (see the param-set table in section 3) and ignores
them on unbound lanes. Gate fall never silences a voice — drones keep rendering through
gate-off at the last committed pitch.

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
    void removeAllVoices();

    // Voice Configuration
    bool setVoiceConfig(uint8_t voiceId, const VoiceConfig& config);
    bool setVoicePreset(uint8_t voiceId, const std::string& presetName);
    const VoiceConfig* getVoiceConfig(uint8_t voiceId);

    // Voice State Management
    bool updateVoiceState(uint8_t voiceId, const VoiceState& state);
    const VoiceState* getVoiceState(uint8_t voiceId);

    // Sequencer Attachment
    bool attachSequencer(uint8_t voiceId, std::unique_ptr<Sequencer> sequencer);
    bool attachSequencer(uint8_t voiceId, Sequencer* sequencer);
    Sequencer* getSequencer(uint8_t voiceId);

    // Audio Processing
    void init(float sampleRate);
    void processBlock(float *out, uint32_t n) noexcept;
    float processAllVoices() noexcept; // one-sample wrapper
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

| # | Preset Name | Engine | Oscillators | Amplitudes | Detune (Semis) | Harmony | F/A/D Lanes | High-pass | Overdrive | Output Level |
|---|---|---|---|---|---|---|---|---|---|---|
| **0** | **Analog** | osc | 1x `WAVE_HARDSYNC_SAW` | `[1.0]` | `[0.0]` | `[0]` | **unbound** (`--`; Note/Velocity are Master/Slave) | — | Off (Gain: 0.8, Drive: 0.25) | `0.5` |
| **1** | **Digital** | osc | 2x `WAVE_BSP_SQUARE` | `[0.75, 0.65]` | `[0.0, +0.01]` | `[0, 0]` | **unbound** (`--`) | — | Off (Gain: 0.7, Drive: 0.51) | `0.5` |
| **2** | **Bass** | osc | 2x (`WAVE_SIN`, `WAVE_TRI`) | `[1.0, 1.0]` | `[-12.0, 0.0]` | `[0, 0]` | **unbound** (`--`) | — | On (Gain: 0.95, Drive: 0.16) | `0.85` |
| **3** | **Lead** | osc | 2x `WAVE_BSP_SAW` | `[0.6, 0.4]` | `[0.0, 0.0]` | `[0, 3]` | **unbound** (`--`) | — | Off (Gain: 0.7, Drive: 0.45) | `0.5` |
| **4** | **Square** | osc | 1x `WAVE_BSP_SQUARE` (PW: 0.2) | `[1.0]` | `[0.0]` | `[0]` | **unbound** (`--`) | — | Off (Gain: 0.75, Drive: 0.35) | `0.56` |
| **5** | **Pad** | osc | 3x `WAVE_BSP_SAW` | `[0.33, 0.33, 0.33]` | `[0.0, 0.0, 0.0]` | `[0, +4, +9]` | **unbound** (`--`) | — | Off (Gain: 0.85, Drive: 0.25) | `0.5` |
| **6** | **Percussion** | osc | **0 oscs** (`WAVE_NOISE`, `NoiseOscillator`) | `[1.0]` | `[0.0]` | `[0]` | **unbound** (`--`) | — | Off (Gain: 0.45, Drive: 0.30) | `0.5` |
| **7** | **SubFunk** | osc | (`WAVE_SIN`, `WAVE_BSP_SQUARE`, `WAVE_SIN`) | `[1.0, 0.35, 0.65]` | `[-12.0, -12.0, 0.0]` | `[0, 0, 0]` | **unbound** (`--`) | — | On (Gain: 0.9, Drive: 0.45) | `0.9` |
| **8** | **RubberSub** | osc | (`WAVE_SIN`, `WAVE_BSP_SQUARE`, `WAVE_TRI`) | `[0.9, 0.3, 0.5]` | `[-12.0, -12.0, 0.0]` | `[0, 0, 0]` | **unbound** (`--`) | — | On (Gain: 1.0, Drive: 0.55) | `0.85` |
| **9** | **WgPluck** | waveguide | — (wg: T60 1.8s, bright 0.78, pick 0.26/0.85, stiff 0.0, det 4c) | — | — | — | **Bright / Pick / T60** | 55 Hz sub-shed | Off | `0.85` |
| **10** | **WgNylon** | waveguide | — (wg: T60 3.2s, bright 0.28, pick 0.42/0.22, stiff 0.05, det 9c) | — | — | — | **Bright / Pick / T60** | 66 Hz sub-shed | Off | `0.9` |
| **11** | **WgBell** | waveguide | — (wg: T60 1.4s, bright 0.9, pick 0.08/1.0, stiff 0.88, det 0c) | — | — | — | **Bright / Pick / T60** | bypassed | Off | `0.75` |
| **12** | **WgShimmer** | waveguide | — (wg: T60 6.5s, bright 0.55, pick 0.35/0.6, stiff 0.15, det 26c) | — | — | — | **Bright / Pick / T60** | bypassed | Off | `0.8` |
| **13** | **Hypersaw** | hypersaw | one `rpdsp::Hypersaw` (seven internal saws) | — | Detune: 0.2 (native 0–1) | — | **Detune / Mix** | — | Off | `0.5` |
| **14** | **NoiseStorm** | noise-FX | — (nf: diffuse 0.85/0.65, swarm 0.6/0.95, chaos 0.4) | — | — | — | **Color / Regen / Chaos** | — | On (Gain: 0.8, Drive: 0.4) | `0.45` |

F/A/D Lanes: the sequencer's Filter/Attack/Decay lanes. The lanes and the saved-pattern
format are unchanged, but since the drone build removed the per-voice main filter and ADSR
they are macro-only: **Analog** through **RubberSub** (0–8) leave them unbound — they shape
nothing and the OLED shows `--`; only waveform blend, overdrive and output level tune those
voices. Waveguide binds Bright/Pick/T60, Hypersaw binds Detune/Mix, NoiseStorm binds
Color/Regen/Chaos, and recipe presets 15–28 bind three engine macros each (FM
Index/Ratio/Feedback, Formant/Bloom/Body, ... — see the [voice and preset extension
guide](../src/voice/README.md)). The per-voice main filter — both the `rpdsp::LadderFilter`
and the TPT `rpdsp::StateVariableFilter` topology — was removed with the drone build; the
high-pass column survives only on the waveguide engine, sub-shedding rumble from the
Karplus tails (55/66 Hz on WgPluck/WgNylon, bypassed on WgBell/WgShimmer).

The eight presets added with the expansion bank: **SubFunk** — bouncy sub bass; a sine sub an octave down carries the weight, a triangle adds movement, and warm overdrive grit gives the growl funk character. **RubberSub** — rubbery sub bass; a sub-octave square grinds under a sine, with harder overdrive that spits on transients. **WgPluck** — classic Karplus-Strong plucked string: bright burst, harmonic loop, short natural tail. **WgNylon** — dark felt-soft nylon: heavily damped loop, gentle pick, long sympathetic tail. **WgBell** — stiff dispersive string whose inharmonic upper partials read as bell/kalimba; hard bridge pick, quick tail. **WgShimmer** — wide-detuned (26-cent) two-string course with a very long T60 tail; slow chorusing sustain turns the pluck into a ringing pad. **Hypersaw** — one native seven-voice `rpdsp::Hypersaw`; its Detune and Mix sequencer slots drive the engine directly. **NoiseStorm** — noise-based texture: noise plus a pitch-tracked Lorenz chaos growl feed a prime-tap diffuser and a regenerative allpass swarm.

Preset 9-12 use `engine = ENGINE_WAVEGUIDE` (`rpdsp::PluckedStringVoice`, 2048-sample
delay): each gate rise (or retrigger) plucks the string at the current base pitch, and
the `wg*` config fields tune T60, loop brightness, pick position/hardness, stiffness
(inharmonic dispersion), and two-string course detune. As with every drone voice, gate
fall never silences them: the string keeps sounding at the last committed pitch, and
velocity scales the pluck excitation itself (soft picks inject less energy, and the
sounding tail is never rescaled by later velocity changes). Gate edges still arm plucks
— see `handleGateEdges_()`.
WgPluck/WgNylon keep a gentle 55/66 Hz high-pass to shed subsonic rumble that
Karplus tails otherwise accumulate; WgBell/WgShimmer bypass the high-pass.
Preset 13 uses `engine = ENGINE_HYPERSAW`: one `rpdsp::Hypersaw` instance supplies
its internal seven saw voices. Its Attack and Decay sequencer slots are re-purposed
as normalized Detune and Mix controls, respectively; its Filter lane is unbound.

Preset 0, Analog, uses one `WAVE_HARDSYNC_SAW`. Its Note/Master track sets the
master frequency. Its Velocity/Slave track is centered at 0.5 and maps to a
slave offset of -24 to +24 semitones: the untouched/default value of 0.5 is a
zero offset, so the slave follows the master exactly. Hard-sync presets do not
apply that re-purposed lane as VCA velocity.

Preset 14 uses `engine = ENGINE_NOISEFX`: `NoiseOscillator` plus a pitch-tracked
`chaos_lorenz` growl feed `fx_diffuse` (prime-tap diffuser) and `fx_swarm` (regenerative
allpass swarm) from `rpdsp/DSPFunctions.h`.

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
| HARDSYNC | 0 | Master pitch / Slave offset (-24..+24 st; 0.5 = follow master) | unbound (`--`) | unbound (`--`) | unbound (`--`) |
| STANDARD | 1–8 | Note / velocity (amplitude) | unbound (`--`) | unbound (`--`) | unbound (`--`) |
| WAVEGUIDE | 9–12 | Note / velocity (pluck excitation) | Brightness (0–1) | Pick hardness (0–1) | T60 (0.05–10 s, EXP; `wgT60ToNormalized` seeding assumes the same curve) |
| HYPERSAW | 13 | Note / velocity | unbound (`--`) | Native seven-voice detune (0–1) | Native center/side mix (0–1) |
| NOISESTORM | 14 | Note / velocity | Swarm color | Swarm regen | Chaos level |

Since the drone build removed the per-voice filter and ADSR, the Filter/Attack/Decay lanes
are macro-only: an unbound slot shapes nothing and the OLED shows `--` for it, while a bound
slot is a macro control routed into a `VoiceConfig` member by `Voice::applyParameters_()`
(recipe presets bind three engine macros each, e.g. FM Index/Ratio/Feedback). The MIDI CC
transmission of the lane values (CC 74/73/72, 78/77/76) still runs and now carries those
macro values — or inert values on the oscillator presets. Live preset switches are
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

1. `handleGateEdges_()` consumes gate rise/fall and retrigger. Voices are drones:
   gate fall does not silence them. Rise/retrigger arms the engine triggers
   (waveguide pluck, Hypersaw phase randomization, recipe reset) and pitch
   commits happen while the gate is high.
2. Apply deferred structural configuration if the gate is low.
3. `renderSources_()` selects the engine once per span and renders every
   sample — there is no envelope silence gate. Oscillator-bank pitch commits
   require a high gate; slides advance every sounding sample. Each oscillator
   renders into the mix in its original order.
4. Apply pre-output effects in sample order (overdrive, plus the NoiseStorm
   diffuser/swarm inserts).
5. Apply velocity (amplitude remains 1 for layouts that repurpose velocity,
   e.g. hard-sync Master/Slave and waveguide pluck excitation).
6. Render the high-pass on waveguide engines only (sub-shedding for the
   Karplus tails), then apply the output level.

The quiet-span idle skip (256 silent samples) was removed with the drone build:
with no envelope there is no voice-level silence to detect, so every span
renders and waveguide/NoiseStorm tail state keeps evolving.

`VoiceManager::processBlock()` sums voice blocks in the original voice order.
Per-voice mix, master-volume and mute targets are read once per block of up to
256 frames (5.33 ms at 48 kHz). Master smoothing still advances every sample.
`AudioSamples::toPcm16()` clamps and truncates the mix into identical left/right
I2S samples. Block APIs overwrite their output and accept zero-length calls.

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
- **Single pitch lookup path**: `calculateNoteFrequency()` reads the **injected** table through `scaleTable[effectiveScaleIndex_()][noteIndex + harmony]`. With no table injected (`nullptr`), it falls back to **chromatic mapping** (scale step = semitone above C3).
- **Synthesis Pitch Offset**: Scale degrees are centered around C3 (+48) with octave offset:
  $$\text{midiNote} = \text{scaleTable}[\text{scaleIndex}][\text{noteIndex} + \text{harmony}] + 48 + \text{static\_cast<int>}(\text{octaveOffset})$$
- **Index clamping**: `noteIndex + harmony` is clamped to `0..47` and the resulting MIDI note is saturated to `0..127` before the lookup-table read, so extreme harmony/octave values cannot index out of bounds.
- **Live scale switches**: the effective scale row is part of the pitch snapshot (`PitchSnapshot::scaleIndex`); a runtime `currentScale` change invalidates the static base frequency on the next pitch recompute (repeated notes repitch too).
- No per-scale preprocessing happens at injection time — `setScaleTable()` only stores the pointer and marks the base frequency dirty (the former unique-rank caches were write-only and were removed 2026-09-05).

### 5.3 Gate-Controlled Pitch Commit
To prevent audible pitch clicks and glitches when voices keep sounding across a sequencer step transition, pitch changes are **committed to oscillators only when `state.isGateHigh == true`**. When the gate is low, the voice keeps droning at its last assigned frequency — and since the drone build this also holds through gate-off: gate edges fire engine triggers, never amplitude.

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
newState.filterCutoff = 0.6f;         // 60% Filter lane value (macro input; inert on oscillator presets)
newState.isGateHigh = true;           // Gate ON
newState.hasSlide = false;
newState.octaveOffset = 0.0f;
newState.gateLengthTicks = 60;        // 60 PPQN ticks

voiceManager.updateVoiceState(v1, newState);
```

### 6.3 Real-Time Block Audio Loop (Core 1)

```cpp
// Fixed Core 1 scratch, outside the stack.
static std::array<float, 256> mix;
voiceManager.processBlock(mix.data(), mix.size());
for (uint32_t i = 0; i < mix.size(); ++i) {
    const int16_t pcm16 = AudioSamples::toPcm16(mix[i]);
    out[2 * i] = pcm16;
    out[2 * i + 1] = pcm16;
}
```
