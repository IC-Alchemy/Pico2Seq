#pragma once

#include "VoiceOscillator.h"
#include "../rpdsp/src/rpdsp/ladder.h"
#include "../rpdsp/src/rpdsp/filter.h"
#include "../rpdsp/src/rpdsp/envelope.h"
#include "../rpdsp/src/rpdsp/effects.h"
#include "../rpdsp/src/rpdsp/waveguide.h"
#include "../rpdsp/src/rpdsp/DSPFunctions.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include <array>
#include <vector>
#include <memory>

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <cmath>

// Force-inline the per-sample stages of Voice::process(). Even at -O3 GCC
// left computeEnvelope/updateFilter/mixOscillators and the two pending-change
// checks as out-of-line calls: 4-6 call/return pairs per voice per sample on
// the RP2350. The bodies are private and only ever called from process().
#if defined(__GNUC__)
#define PICO2SEQ_HOT_INLINE inline __attribute__((always_inline))
#else
#define PICO2SEQ_HOT_INLINE inline
#endif

// Sound-generation engine selected by VoiceConfig::engine. Engines other than
// the default still run the shared envelope -> filter -> output chain; only
// the source stage (and, for the noise engine, the pre-filter effect inserts)
// differs.
enum VoiceEngine : uint8_t
{
  ENGINE_OSC = 0,       // Up to 3 oscillators (or raw noise when oscillatorCount == 0)
  ENGINE_WAVEGUIDE = 1, // Karplus-Strong plucked string (rpdsp::PluckedStringVoice)
  ENGINE_NOISEFX = 2,   // Noise + chaos source through diffuser/swarm inserts
};

/**
 * @brief Configuration structure for a voice
 *
 * Defines the characteristics and behavior of a synthesizer voice including
 * oscillator settings, filter parameters, effects chain, and envelope configuration.
 */
struct VoiceConfig
{
  // Oscillator configuration
  uint8_t oscillatorCount = 3; // Number of oscillators (1-3)
  uint8_t oscWaveforms[3] = {  // Waveform types for each oscillator (WAVE_* ids from VoiceOscillator.h)
      WAVE_BSP_SAW,
      WAVE_BSP_SAW,
      WAVE_BSP_SAW};
  float oscAmplitudes[3] = {0.5f, 0.5f, 0.5f}; // Oscillator amplitudes (0.0-1.0)
  float oscDetuning[3] = {0.0f, 0.0f, 0.0f};   // Detuning in semitones (-12.0 to +12.0)
  float oscPulseWidth[3] = {0.5f, 0.5f, 0.5f}; // Pulse width for square/pulse waves (0.0-1.0)
  int harmony[3] = {0, 0, 0};                  // Harmony intervals in scale steps (-12 to +12)

  // Sound engine selection (VoiceEngine). Ignored fields stay at their defaults.
  uint8_t engine = ENGINE_OSC;

  // Waveguide engine parameters (ENGINE_WAVEGUIDE only)
  float wgT60 = 2.5f;          // String tail T60 in seconds (0.05-10.0)
  float wgBrightness = 0.7f;   // Loop damping: 0 dark nylon .. 1 glassy (0.0-1.0)
  float wgPickPosition = 0.25f; // Pick point on string, 0.02 bridge .. 0.5 middle
  float wgPickHardness = 0.8f; // Excitation burst: 0 soft felt .. 1 hard pick
  float wgStiffness = 0.0f;    // Inharmonic dispersion: 0 harmonic .. 1 bell-like
  float wgDetune = 6.0f;       // Two-string course spread in cents (0.0-30.0)

  // Noise-FX engine parameters (ENGINE_NOISEFX only)
  float noiseDiffuseSize = 0.8f; // Prime-tap diffuser smear (0.0-1.0)
  float noiseDiffuseMix = 0.7f;  // Diffuser wet amount (0.0-1.0)
  float noiseSwarmColor = 0.5f;  // Allpass swarm tone (0.0-1.0)
  float noiseSwarmRegen = 0.9f;  // Allpass swarm regeneration (0.0-1.2)
  float noiseChaosLevel = 0.35f; // Pitch-tracked chaos_lorenz growl mix (0.0-1.0)

  // Filter settings
  float filterRes = 0.2f;            // Filter resonance (0.0-1.0)
  float filterDrive = 1.8f;          // Filter drive amount (0.0-4.0)
  float filterPassbandGain = 0.23f;  // Passband gain compensation (0.0-0.5)
  rpdsp::LadderFilter::Mode filterMode = rpdsp::LadderFilter::Mode::LP24; // Filter mode

  // High-pass filter settings
  float highPassFreq = 80.0f; // High-pass cutoff frequency in Hz (20.0-20000.0)
  float highPassRes = 0.1f;   // High-pass resonance (0.0-1.0)

  // Effects chain configuration
  bool hasOverdrive = false;     // Enable overdrive effect
  bool hasEnvelope = true;       // Enable envelope (recommended: true)
  float overdriveGain = 0.34f;   // Overdrive output gain (0.0-2.0)
  float overdriveDrive = 0.25f;  // Overdrive drive amount (0.0-1.0)

  // Envelope default settings
  float defaultAttack = 0.04f; // Default attack time in seconds (0.001-10.0)
  float defaultDecay = 0.14f;  // Default decay time in seconds (0.001-10.0)
  float defaultSustain = 0.5f; // Default sustain level (0.0-1.0)
  float defaultRelease = 0.1f; // Default release time in seconds (0.001-10.0)

  // Voice mixing
  float outputLevel = 0.6f; // Voice output level (0.0-1.0)
  bool enabled = true;      // Voice enabled state
};

// Filter modes exposed to the UI, in cycle order, with matching display names.
// Cycling code must use these tables together so labels and enum values can
// never disagree (the old UI hardcoded a name list that mismatched the enum).
namespace voiceui {
inline constexpr rpdsp::LadderFilter::Mode kFilterModes[] = {
    rpdsp::LadderFilter::Mode::LP24, rpdsp::LadderFilter::Mode::LP12,
    rpdsp::LadderFilter::Mode::BP24, rpdsp::LadderFilter::Mode::BP12,
    rpdsp::LadderFilter::Mode::HP24, rpdsp::LadderFilter::Mode::HP12};
inline constexpr const char *kFilterModeNames[] = {"LP24", "LP12", "BP24",
                                                   "BP12", "HP24", "HP12"};
inline constexpr int kFilterModeCount = 6;
} // namespace voiceui

/**
 * @brief Frequency slewing parameters for smooth slide transitions
 *
 * Used to implement smooth frequency transitions between notes when slide is enabled.
 */
struct VoiceSlewParams
{
  float currentFreq = 440.0f; // Current frequency in Hz (20.0-20000.0)
  float targetFreq = 440.0f;  // Target frequency in Hz (20.0-20000.0)
};

/**
 * @brief A complete synthesizer voice with oscillators, filter, envelope, and effects
 *
 * This class encapsulates all the audio processing components needed for a single voice,
 * making it easy to create multiple independent voices with different characteristics.
 *
 * Scale data access and testability:
 * - Voice no longer reads global scale variables directly. Instead, scale data is injected
 *   via setter methods (see setScaleTable and setCurrentScalePointer).
 * - This reduces global-state coupling and makes the class easier to unit test: tests can
 *   provide a mock scale table and a fixed/current scale index without relying on externs.
 * - If no scale data is injected, Voice falls back to chromatic mapping for note calculation.
 */
class Voice
{
public:
  /**
   * @brief Construct a new Voice object
   * @param id Unique identifier for this voice (0-7)
   * @param config Configuration structure defining voice characteristics
   */
  Voice(uint8_t id, const VoiceConfig &config);

  /**
   * @brief Destroy the Voice object
   */
  ~Voice() = default;

  // Initialization and configuration
  /**
   * @brief Initialize the voice with the given sample rate
   * @param sampleRate Audio sample rate in Hz (typically 48000.0)
   */
  void init(float sampleRate);

  /**
   * @brief Update the voice configuration
   * @param config New configuration to apply
   */
  void setConfig(const VoiceConfig &config);

  /**
   * @brief Get the current voice configuration
   * @return const VoiceConfig& Current configuration
   */
  const VoiceConfig &getConfig() const noexcept { return config; }

  /**
   * @brief Get mutable reference to voice configuration
   * @return VoiceConfig& Reference to voice configuration
   */
  VoiceConfig &getConfig() noexcept { return config; }

  // Audio processing
  /**
   * @brief Process one sample of audio
   * @return float Processed audio sample (-1.0 to +1.0 range)
   */
  float process() noexcept;

  /**
   * @brief Update voice parameters from sequencer state
   * @param newState New voice state from sequencer containing note, velocity, filter, envelope parameters
   */
  void updateParameters(const VoiceState &newState);

  // Sequencer integration
  /**
   * @brief Set the sequencer for this voice (takes ownership)
   * @param seq Unique pointer to sequencer object
   */
  void setSequencer(std::unique_ptr<Sequencer> seq);

  /**
   * @brief Set the sequencer for this voice (raw pointer, no ownership transfer)
   * @param seq Raw pointer to sequencer object
   */
  void setSequencer(Sequencer *seq);

  /**
   * @brief Inject scale data (48-step per-scale tables) to remove global dependencies
   * @param table Pointer to a 2D array of shape [scaleCount][48] containing semitone offsets
   * @param scaleCount Number of scales available in the table (0-255)
   *
   * The Voice will use this table to map scale step indices (0-47) to semitone offsets.
   * Pass nullptr to disable and fall back to chromatic mapping.
   */
  void setScaleTable(const int (*table)[48], size_t scaleCount);

  /**
   * @brief Inject a pointer to the current scale index used with the injected table
   * @param currentScalePtr Pointer to an externally managed current-scale index (0-scaleCount-1)
   *
   * The pointed value is read at note-calculation time. If nullptr (or out of bounds),
   * Voice falls back to chromatic mapping.
   */
  void setCurrentScalePointer(const uint8_t *currentScalePtr);

  /**
   * @brief Get pointer to the voice's sequencer
   * @return Sequencer* Pointer to sequencer (nullptr if not set)
   */
  Sequencer *getSequencer() noexcept { return sequencer; }

  // State management
  /**
   * @brief Get reference to current voice state
   * @return VoiceState& Reference to voice state containing current parameters
   */
  VoiceState &getState() noexcept { return state; }

  /**
   * @brief Get const reference to current voice state
   * @return const VoiceState& Const reference to voice state
   */
  const VoiceState &getState() const noexcept { return state; }

  /**
   * @brief Set gate state for this voice
   * @param gateState True for gate on (note triggered), false for gate off (note released)
   */
  void setGate(bool gateState)
  {
    gate = gateState;
    state.isGateHigh = gateState; // Synchronize both gate variables
  }

  /**
   * @brief Get current gate state
   * @return bool Current gate state (true = on, false = off)
   */
  bool getGate() const noexcept { return gate; }

  // Filter control
  /**
   * @brief Set filter cutoff frequency
   * @param freq Frequency in Hz (20.0-20000.0)
   */
  void setFilterFrequency(float freq) { filterFrequency = freq; }

  /**
   * @brief Get current filter frequency
   * @return float Current filter frequency in Hz
   */
  float getFilterFrequency() const noexcept { return filterFrequency; }

  // Voice identification
  /**
   * @brief Get voice ID
   * @return uint8_t Voice identifier (0-7)
   */
  uint8_t getId() const noexcept { return voiceId; }

  /**
   * @brief Check if voice is enabled
   * @return bool True if voice is enabled and will process audio
   */
  bool isEnabled() const noexcept { return config.enabled; }

  /**
   * @brief Enable or disable the voice
   * @param enabled True to enable voice processing, false to disable
   */
  void setEnabled(bool enabled) { config.enabled = enabled; }

  /**
   * @brief Set the base frequency for all oscillators
   * @param frequency Frequency in Hz (20.0-20000.0)
   */
  void setFrequency(float frequency);

  /**
   * @brief Set slide time for frequency transitions
   * @param slideTime Slide time in seconds (0.001-10.0)
   */
  void setSlideTime(float slideTime);

  // Pitch optimization API
  /**
   * @brief Set pitch bend in semitones and mark pitch dirty
   */
  void setPitchBend(float semitones);

  /**
   * @brief Set modulation depth in semitones and mark pitch dirty
   */
  void setModulationDepth(float semitones);

  /**
   * @brief Force pitch cache to recompute and mark for audio-thread commit
   */
  void markPitchDirty();

  /**
   * @brief Consolidated recompute entry; called from control thread after state changes
   */
  void updateFrequencyIfNeeded();

  /**
   * @brief Read-only accessor for cached final oscillator frequency
   */
  float getCachedFrequency(uint8_t oscIndex) const;

private:
  // Voice identification and configuration
  uint8_t voiceId;
  VoiceConfig config;
  float sampleRate;

  // Frequency lookup table for performance optimization
  // Covers all possible MIDI notes (0-127) to avoid mtof() calculations
  static float frequencyLookupTable[128];
  static bool lookupTableInitialized;

  // Injected scale data (optional). When null, Voice uses chromatic mapping.
  // scaleTable is a pointer to an array of 48-step scales; scaleTableCount is number of scales.
  const int (*scaleTable)[48] = nullptr;
  size_t scaleTableCount = 0;
  const uint8_t *currentScalePtr = nullptr; // Pointer to externally managed current-scale index

  // Cached per-scale preprocessing to accelerate unique-degree traversal.
  // These are populated by setScaleTable() (non-realtime) and used in
  // calculateNoteFrequency() to replace the iterative advanceDegrees loop
  // with constant-time arithmetic and a few indexed lookups.
  //
  // Layout:
  // - scaleUniqueCounts[s]                        => number of unique degree entries for scale s
  // - scaleIndexToRank[s * 48 + i]               => maps original scale index i (0..47) to its unique-rank (0..uniqueCount-1)
  // - scaleUniqueIndexList[s * 48 + r]           => original scale index (0..47) of the r-th unique degree for scale s
  std::vector<uint8_t> scaleUniqueCounts;    // size == scaleCount (populated on setScaleTable)
  std::vector<uint8_t> scaleIndexToRank;     // size == scaleCount * 48
  std::vector<uint8_t> scaleUniqueIndexList; // size == scaleCount * 48 (padded)

  // Audio processing components
  std::array<VoiceOscillator, 3> oscillators;
  rpdsp::NoiseOscillator noise_;
  rpdsp::LadderFilter filter;
  rpdsp::StateVariableFilter highPassFilter;
  rpdsp::ADSR envelope;
  rpdsp::Waveshaper overdrive;

  // Alternate engines. Both are fixed-storage members so Voice stays
  // allocation-free; they only run when config.engine selects them.
  // Waveguide capacity 2048 samples ≈ 24 Hz minimum pitch at 48 kHz.
  static constexpr size_t kWaveguideCapacity = 2048;
  rpdsp::PluckedStringVoice<kWaveguideCapacity> waveguide_;
  // Noise-FX scratch (caller-owned state for the rpdsp DSPFunctions free
  // functions): 2048-tap diffuser ring plus diffuser/swarm/chaos state.
  static constexpr int kNoiseFxBufferSize = 2048;
  std::array<float, kNoiseFxBufferSize> noiseDiffuseBuf_{};
  float noiseDiffuseState_[1] = {0.0f};
  float noiseSwarmState_[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  float noiseChaosState_[4] = {0.0f, 0.0f, 0.0f, 0.0f};

  // Gate edge tracking for the event-style ADSR (noteOn on rise, noteOff on fall)
  bool gateHighPrev_ = false;
  // Set on gate rise/retrigger so the waveguide engine plucks with the pitch
  // already committed for this frame; consumed by mixOscillators().
  bool wgPluckPending_ = false;
  // Cached engine (clamped config.engine), updated on config apply.
  uint8_t cachedEngine_ = ENGINE_OSC;

  // Voice state and control
  VoiceState state;
  float filterFrequency;
  // Smoothed filter cutoff to avoid zipper noise when envelope modulates cutoff.
  // filterCutoffCurrent is the per-sample smoothed cutoff (Hz).
  // filterCutoffAlpha is the per-sample exponential smoothing coefficient (0..1).
  float filterCutoffCurrent = 1000.0f;
  float filterCutoffAlpha = 0.0f;
  // Cache of last applied cutoff to avoid redundant filter.SetFreq calls in the hotpath.
  // Initialized to -1.0f in ctor/init to guarantee first SetFreq occurs.
  float lastAppliedFilterCutoff = -1.0f;
  // Throttle expensive filter.setFreq() updates: coefficients are recomputed
  // at most once every kFilterUpdateInterval samples. Power of two so the
  // rolling counter wraps with a mask instead of a per-sample UDIV.
  static constexpr uint8_t kFilterUpdateInterval = 8;
  static_assert((kFilterUpdateInterval & (kFilterUpdateInterval - 1)) == 0,
                "kFilterUpdateInterval must be a power of two");
  uint8_t filterUpdateCounter = 0;               // rolling counter
  static constexpr float kFilterRelEps = 2e-3f;  // 0.2% relative change
  static constexpr float kFilterAbsEpsHz = 1.0f; // or at least 1 Hz change
  inline static bool ShouldApplyFilterFreq_(float f_new, float f_last)
  {
    const float maxf = (f_new > f_last ? f_new : f_last);
    const float rel = kFilterRelEps * maxf;
    const float thr = (rel > kFilterAbsEpsHz ? rel : kFilterAbsEpsHz);
    return (f_last < 0.0f) || (fabsf(f_new - f_last) > thr);
  }
  // Cached envelope value updated each frame to allow a very cheap silence short-circuit.
  float lastEnvelopeValue = 0.0f;
  VoiceSlewParams freqSlew[3]; // For slide functionality
  volatile bool gate;
  // Cached active oscillator count (0..3), updated on config apply to avoid per-sample min()
  uint8_t cachedOscCount_ = 0;
  // Bypass flags computed on config apply to avoid unnecessary DSP work
  bool hpfBypass_ = false;

  // Slide/portamento control
  // slideTimeSeconds is the exponential time constant in seconds
  // slideAlpha is the per-sample coefficient computed from slideTimeSeconds and sampleRate
  float slideTimeSeconds = 0.06f;
  float slideAlpha = 0.00035f;

  // Cached detune multipliers to avoid powf in the realtime path
  // detuneMul[i] = 2^(oscDetuning[i] / 12)
  float detuneMul[3] = {1.0f, 1.0f, 1.0f};

  // Static pitch cache (control-thread staging + audio-thread commit):
  // - cachedBaseFreqHz_ stores base from static pitch inputs (note, octave/transpose, scale/tuning).
  // - baseFreqDirty_ flags when base must be recomputed.
  // - lastSentBaseFreqHz_ reserved for micro-gating comparisons.
  // Generation-based staging:
  // - updatePitchCache_ computes PitchCache/PitchSnapshot on the control thread and bumps pitchGen_.
  // - mixOscillators() commits staged frequencies on the audio thread only, comparing pitchGen_ vs appliedPitchGen_.
  // - ShouldApplyFreq_ gates redundant per-sample SetFreq calls (eps ~= 0.017 cent via kPitchRelEps).
  float cachedBaseFreqHz_ = 440.0f;
  bool baseFreqDirty_ = true;
  float lastSentBaseFreqHz_ = -1.0f;

  // -------- Parameter & config staging (lock-free cross-core) --------
  VoiceState stagedState_{};           // control-thread writes
  std::atomic<uint32_t> paramsGen_{0}; // bump on stagedState_ update
  uint32_t appliedParamsGen_{0};       // audio-thread applied version

  VoiceConfig stagedConfig_{};             // control-thread writes
  std::atomic<bool> configPending_{false}; // set true when a new config is staged

  // -------- Pitch change-detection & caching --------
  // Staging cache computed on control thread, committed on audio thread.
  struct PitchSnapshot
  {
    float noteIndex;
    int8_t octaveOffset;
    int harmony[3];
    uint8_t oscCount;
    uint32_t detuneVersion;
    bool hasSlide;
    // Snapshotted pitch controls to detect changes
    float bendSemis;
    float modSemis;
  };

  struct PitchCache
  {
    float baseFreq;
    float harmonyFreq[3];
    float finalFreq[3];
  };

  PitchSnapshot pitchSnapshot_{};
  PitchCache pitchCache_{};
  float lastAppliedOscFreq_[3] = {-1.0f, -1.0f, -1.0f};

  // Generation counter: bumped after cache write; audio thread copies appliedPitchGen_
  std::atomic<uint32_t> pitchGen_{0};
  uint32_t appliedPitchGen_{0};

  // Detune version: incremented when detune multipliers are recomputed
  uint32_t detuneVersion_{0};

  // Additional pitch controls
  float pitchBendSemitones_{0.0f};
  float pitchModSemitones_{0.0f};

  // Frequency change thresholding to avoid redundant SetFreq calls.
  // kPitchRelEps ~= 1e-5 => ~0.017 cent at full-scale; combined with kPitchAbsEpsHz.
  static constexpr float kPitchRelEps = 1e-5f;
  static constexpr float kPitchAbsEpsHz = 1e-5f;
  inline static bool ShouldApplyFreq_(float f_new, float f_last)
  {
    const float maxf = (f_new > f_last ? f_new : f_last);
    const float rel = kPitchRelEps * maxf;
    const float thr = (rel > kPitchAbsEpsHz ? rel : kPitchAbsEpsHz);
    return (f_last < 0.0f) || (fabsf(f_new - f_last) > thr);
  }

  // Sequencer integration
  Sequencer *sequencer;
  // Owns the sequencer when attached via unique_ptr overload
  std::unique_ptr<Sequencer> sequencerOwned;

  // Private helper methods
  /**
   * @brief Process the effects chain on the input signal
   * @param signal Reference to signal to process (modified in place, -1.0 to +1.0)
   */
  void processEffectsChain(float &signal);

  // Cross-core application helpers (called on audio thread at start of
  // process). The per-sample checks are inline so the common "nothing
  // pending" case costs one load and one branch; the bodies stay in Voice.cpp.
  PICO2SEQ_HOT_INLINE void applyPendingParams_() noexcept
  {
    const uint32_t gen = paramsGen_.load(std::memory_order_seq_cst);
    if (gen != appliedParamsGen_)
    {
      applyPendingParamsSlow_(gen);
    }
  }
  PICO2SEQ_HOT_INLINE void applyPendingConfig_() noexcept
  {
    if (configPending_.load(std::memory_order_acquire))
    {
      applyPendingConfigSlow_();
    }
  }
  void applyPendingParamsSlow_(uint32_t gen) noexcept;
  void applyPendingConfigSlow_() noexcept;

  // Pitch recompute helpers (staging on control thread; commit on audio thread)
  // Detects changes in note/octave/harmony/osc count/detune version/sample-rate version/slide/bend/mod.
  bool pitchParamsChanged_(const VoiceState &newState) const;
  // Recomputes pitchCache_ and updates pitchSnapshot_, then bumps pitchGen_ atomically.
  void updatePitchCache_();

  // Private DSP stages used by process()
  /**
   * @brief Compute envelope value for current sample
   * @return float Envelope amplitude (0.0-1.0)
   */
  PICO2SEQ_HOT_INLINE float computeEnvelope();

  /**
   * @brief Update filter parameters based on envelope and voice state
   * @param envelopeValue Current envelope value (0.0-1.0)
   */
  PICO2SEQ_HOT_INLINE void updateFilter(float envelopeValue);

  /**
   * @brief Recompute cached detune multipliers from configuration
   *        Uses fast exp2f with 1/12 factor to avoid powf in realtime code.
   */
  void recomputeDetuneMultipliers();

  /**
   * @brief Mix and process oscillator outputs
   * @return float Mixed oscillator signal (-1.0 to +1.0)
   */
  PICO2SEQ_HOT_INLINE float mixOscillators();

  /**
   * @brief Apply engine-specific configuration (waveguide tuning, noise state)
   *        Called from init() and applyPendingConfig_() at control rate.
   */
  void applyEngineConfig_();

  /**
   * @brief Waveguide engine source stage: pluck on pending edges, process string
   * @return float Waveguide output
   */
  float processWaveguide_() noexcept;

  /**
   * @brief Noise-FX engine source stage: noise + pitch-tracked chaos growl
   * @return float Mixed noise source
   */
  float processNoiseFxSource_() noexcept;

  /**
   * @brief Reset the alternate-engine DSP state (called on engine switches)
   */
  void resetAlternateEngines_() noexcept;

  /**
   * @brief Apply effects chain to signal
   * @param signal Reference to signal to process (modified in place)
   */
  void applyEffects(float &signal);

  /**
   * @brief Finalize output with level and envelope
   * @param signal Input signal (-1.0 to +1.0)
   * @param envelopeValue Envelope amplitude (0.0-1.0)
   * @return float Final output signal (-1.0 to +1.0)
   */
  PICO2SEQ_HOT_INLINE float finalizeOutput(float signal, float envelopeValue) noexcept;

  /**
   * @brief Update oscillator frequencies based on current state
   *
   * Implements gate-controlled frequency updates to prevent audio artifacts.
   * Only updates frequencies when gate is HIGH.
   */
  void updateOscillatorFrequencies();

  /**
   * @brief Apply envelope parameters to the ADSR envelope
   *
   * Updates attack, decay, sustain, and release values from voice state.
   */
  void applyEnvelopeParameters() noexcept;

  /**
   * @brief Calculate frequency for a given note with octave offset
   * @param note Note value (0-21 for scale array lookup, 0-127 for chromatic)
   * @param octaveOffset Octave offset in semitones (-24 to +24)
   * @param harmony Harmony value in scale steps (-12 to +12)
   * @return float Frequency in Hz (20.0-20000.0)
   *
   * Uses MIDI_BASE_OFFSET (36) to center around C2. Implements gate-controlled
   * architecture to prevent audio glitches during parameter changes.
   */
  float calculateNoteFrequency(float note, int8_t octaveOffset, int harmony) noexcept;

  // Recompute cached base frequency (static pitch only). No dynamic modulators.
  void recomputeBaseFreqIfDirty_();

  /**
   * @brief Initialize frequency lookup table (called once)
   *
   * Populates static lookup table covering all MIDI notes (0-127) for
   * performance optimization, avoiding repeated mtof() calculations.
   */
  static void initFrequencyLookupTable() noexcept;

  /**
   * @brief Process frequency slewing for slide functionality
   * @param oscIndex Oscillator index (0-2)
   * @param targetFreq Target frequency for slewing in Hz (20.0-20000.0)
   *
   * Implements smooth frequency transitions using configurable slew rate
   * to create slide/portamento effects between notes.
   */
  void processFrequencySlew(uint8_t oscIndex, float targetFreq);
};
