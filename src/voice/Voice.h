#pragma once

#include "VoiceConfig.h"
#include "VoiceParameters.h"
#include "engines/RecipeEngine.h"
#include "../rpdsp/src/rpdsp/ladder.h"
#include "../rpdsp/src/rpdsp/filter.h"
#include "../rpdsp/src/rpdsp/envelope.h"
#include "../rpdsp/src/rpdsp/effects.h"
#include "../rpdsp/src/rpdsp/hypersaw.h"
#include "../rpdsp/src/rpdsp/waveguide.h"
#include "../rpdsp/src/rpdsp/DSPFunctions.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include "../utils/SpscQueue.h"
#include <array>
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

  // Ownership: construct/init and attach sequencers before concurrent use.
  // Then only the control thread calls setters / flushControlUpdates(), and
  // only the audio thread calls process() or reads applied-state getters.
  // Never add/remove voices or reinitialize them while either core is using them.
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

  // Applied-state getters are audio-thread only (or tests while audio is stopped).
  // UI code reads these separate producer-owned copies instead.
  const VoiceConfig &getRequestedConfig() const noexcept { return controls_.config; }
  const VoiceState &getRequestedState() const noexcept { return controls_.state; }

  // Control thread: retry a full queue and sample the control-owned scale index.
  // Call every loop even when no new knob/note events arrive.
  bool flushControlUpdates() noexcept;
  bool hasPendingControlUpdates() const noexcept { return controls_.changes != 0; }
  static constexpr size_t CONTROL_QUEUE_CAPACITY = 8;

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
   * The control core samples this pointer in setters / flushControlUpdates().
   * Audio reads only the queued index. Null selects row 0; indices clamp to the
   * last row. The table must remain immutable and alive throughout playback.
   */
  void setCurrentScalePointer(const uint8_t *currentScalePtr);

  /**
   * @brief Get pointer to the voice's sequencer
   * @return Sequencer* Pointer to sequencer (nullptr if not set)
   */
  Sequencer *getSequencer() noexcept { return sequencer; }

  // Applied state is read only by the audio thread (or tests after it stops).
  /**
   * @brief Get const reference to current voice state
   * @return const VoiceState& Const reference to voice state
   */
  const VoiceState &getState() const noexcept { return state; }

  /**
   * @brief Set gate state for this voice
   * @param gateState True for gate on (note triggered), false for gate off (note released)
   */
  void setGate(bool gateState);

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
  void setFilterFrequency(float freq);

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
  bool isEnabled() const noexcept { return controls_.config.enabled; }

  /**
   * @brief Enable or disable the voice
   * @param enabled True to enable voice processing, false to disable
   */
  void setEnabled(bool enabled);

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
   * @brief Request pitch refresh on the audio thread
   */
  void updateFrequencyIfNeeded();

  /**
   * @brief Read-only accessor for cached final oscillator frequency
   */
  float getCachedFrequency(uint8_t oscIndex) const;

  /**
   * @brief Read the current slave pitch for a HardSyncSaw oscillator.
   *
   * Returns the master pitch for ordinary oscillators, allowing callers and
   * tests to inspect the cached routing without special-case reads.
   */
  float getCachedSlaveFrequency(uint8_t oscIndex) const;

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
  size_t audioScaleIndex_ = 0; // Copied through the queue; never reads UI globals

  // Audio processing components
  std::array<VoiceOscillator, 3> oscillators;
  rpdsp::NoiseOscillator noise_;
  rpdsp::LadderFilter filter;
  rpdsp::StateVariableFilter filterSvf_;  // Alternate main-filter topology (FILTER_SVF)
  rpdsp::StateVariableFilter highPassFilter;
  rpdsp::ADSR envelope;
  rpdsp::Waveshaper overdrive;

  // Alternate engines are fixed-storage members so Voice stays allocation-free;
  // they only run when config.engine selects them.
  // Waveguide capacity 2048 samples ≈ 24 Hz minimum pitch at 48 kHz.
  static constexpr size_t kWaveguideCapacity = 2048;
  rpdsp::PluckedStringVoice<kWaveguideCapacity> waveguide_;
  // A Hypersaw itself contains the seven saw voices. Keep exactly one instance
  // per Voice rather than building a second unison stack from VoiceOscillator.
  rpdsp::Hypersaw hypersaw_;
  RecipeEngine recipeEngine_;
  bool recipeTriggerPending_ = false;
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
  // Hypersaw randomizes its internal phases on each gate rise/retrigger.
  bool hypersawTriggerPending_ = false;
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
  bool gate; // audio thread only
  // Cached active oscillator count (0..3), updated on config apply to avoid per-sample min()
  uint8_t cachedOscCount_ = 0;
  // Bypass flags computed on config apply to avoid unnecessary DSP work
  bool hpfBypass_ = false;
  // Which StateVariableFilter output the main filter reads when
  // filterType == FILTER_SVF: 0 lowpass, 1 bandpass, 2 highpass (from
  // filterMode, cached on config apply).
  uint8_t svfOutputSel_ = 0;

  // Slide/portamento control
  // slideTimeSeconds is the exponential time constant in seconds
  // slideAlpha is the per-sample coefficient computed from slideTimeSeconds and sampleRate
  float slideTimeSeconds = 0.06f;
  float slideAlpha = 0.00035f;

  // Cached detune multipliers to avoid powf in the realtime path
  // detuneMul[i] = 2^(oscDetuning[i] / 12)
  float detuneMul[3] = {1.0f, 1.0f, 1.0f};

  // Pitch caches and dirty flags are entirely audio-thread-owned.
  // - cachedBaseFreqHz_ stores base from static pitch inputs (note, octave/transpose, scale/tuning).
  // - baseFreqDirty_ flags when base must be recomputed.
  // - lastSentBaseFreqHz_ reserved for micro-gating comparisons.
  // - updatePitchCache_ computes PitchCache/PitchSnapshot on the audio thread.
  // - mixOscillators() uses a local version to avoid redundant frequency commits.
  // - ShouldApplyFreq_ gates redundant per-sample SetFreq calls (eps ~= 0.017 cent via kPitchRelEps).
  float cachedBaseFreqHz_ = 440.0f;
  bool baseFreqDirty_ = true;
  float lastSentBaseFreqHz_ = -1.0f;

  enum ControlChange : uint32_t
  {
    ParametersChanged = 1u << 0, ConfigChanged = 1u << 1,
    GateChanged = 1u << 2, ScaleChanged = 1u << 3,
    SlideChanged = 1u << 4, BendChanged = 1u << 5,
    ModulationChanged = 1u << 6, FrequencyChanged = 1u << 7,
    FilterChanged = 1u << 8, PitchRefresh = 1u << 9
  };
  struct ControlUpdate
  {
    VoiceState state{};
    VoiceConfig config{};
    const int (*scaleTable)[48] = nullptr; // immutable, outlives queued use
    size_t scaleCount = 0;
    size_t scaleIndex = 0;
    float slideSeconds = 0.06f;
    float bendSemitones = 0.0f;
    float modulationSemitones = 0.0f;
    float frequency = 440.0f;
    float filterHz = 1000.0f;
    uint32_t changes = 0;
  };
  // Only the control thread touches controls_ / currentScalePtr_. If full,
  // pending changes coalesce here without touching any published queue slot.
  ControlUpdate controls_{};
  const uint8_t *currentScalePtr_ = nullptr;
  SpscQueue<ControlUpdate, CONTROL_QUEUE_CAPACITY> controlQueue_;

  // Structural config staging (oscillator bank rebuild + engine switch).
  // Applied only while the gate is low so a live preset swap never clicks a
  // held note or cuts a ringing tail; scalars still apply immediately.
  bool structuralPending_ = false;
  uint8_t stagedEngine_ = ENGINE_OSC;
  uint8_t stagedOscCount_ = 0;
  uint8_t stagedWaveforms_[3] = {WAVE_BSP_SAW, WAVE_BSP_SAW, WAVE_BSP_SAW};
  float stagedPulseWidth_[3] = {0.5f, 0.5f, 0.5f};

  // -------- Pitch change-detection & caching --------
  // Audio-thread-local change detection.
  struct PitchSnapshot
  {
    float noteIndex;
    int8_t octaveOffset;
    size_t scaleIndex; // effective row of the injected table at last recompute
    int harmony[3];
    uint8_t oscCount;
    bool usesPitchedEngine;
    bool usesHardSync;
    float hardSyncSlaveControl;
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
    float slaveFreq[3];
  };

  PitchSnapshot pitchSnapshot_{};
  PitchCache pitchCache_{};
  float lastAppliedOscFreq_[3] = {-1.0f, -1.0f, -1.0f};

  // Audio-thread-local version; no cross-core synchronization needed here.
  uint32_t pitchGen_{0};
  uint32_t appliedPitchGen_{0};
  // Hypersaw consumes the shared pitch cache independently of the regular
  // oscillator bank, which may be empty for ENGINE_HYPERSAW.
  uint32_t engineAppliedPitchGen_{0};

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

  // Cross-core application helpers (called on audio thread at start of process)
  void applyControlUpdate_() noexcept;
  void applyParameters_(const VoiceState &newState) noexcept;
  void applyConfig_(const VoiceConfig &newConfig) noexcept;
  void refreshPitch_();
  void applyFrequency_(float frequency);
  void applyStructuralConfig_() noexcept;

  // Pitch recompute helpers (audio thread only)
  // Detects changes in note/octave/harmony/osc count/detune version/sample-rate version/slide/bend/mod.
  bool pitchParamsChanged_(const VoiceState &newState) const;
  // Recomputes pitchCache_ and updates pitchSnapshot_, then increments its audio-local version.
  void updatePitchCache_();

  // Private DSP stages used by process()
  /**
   * @brief Compute envelope value for current sample
   * @return float Envelope amplitude (0.0-1.0)
   */
  PICO2SEQ_HOT_INLINE float computeEnvelope();

  /**
   * @brief Mark the static base cache dirty if the effective scale row changed
   *        since the last recompute (the control core queues scale changes).
   */
  void checkScaleIndexChanged_() noexcept;

  /**
   * @brief Update filter parameters based on envelope and voice state
   * @param envelopeValue Current envelope value (0.0-1.0)
   */
  PICO2SEQ_HOT_INLINE void updateFilter(float envelopeValue);

  /**
   * @brief Push topology-dependent filter config (resonance, SVF response)
   *        into the state-variable path. Control-rate: called from init() and
   *        applyConfig_(); cutoff itself is updated per-sample by
   *        updateFilter().
   */
  void configureMainFilterFromConfig_() noexcept;

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
   * @brief Apply engine-specific configuration (waveguide and Hypersaw tuning)
   *        Called from init() and applyConfig_() at control rate.
   */
  void applyEngineConfig_();

  /**
   * @brief Waveguide engine source stage: pluck on pending edges, process string
   * @return float Waveguide output
   */
  float processWaveguide_() noexcept;

  /**
   * @brief Process the single native seven-voice Hypersaw source
   * @return float Hypersaw output
   */
  float processPitchedEngine_() noexcept;

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
   * @brief Apply the config's default envelope segment times to the ADSR
   *
   * Used when paramSet re-purposes the Attack/Decay sequencer slots so they no
   * longer carry envelope times; the preset defaults then define the shape.
   */
  void applyEnvelopeDefaults_() noexcept;

  /**
   * @brief Calculate frequency for a given note with octave offset
   * @param note Scale-step index (clamped to 0..SCALE_STEPS-1)
   * @param octaveOffset Octave offset in semitones (-24 to +24)
   * @param harmony Harmony value in scale steps (-12 to +12)
   * @return float Frequency in Hz
   *
   * Single pitch lookup path: resolves the step via the injected scale table
   * (chromatic mapping when no table was injected) to a MIDI note centered at
   * C3 (48), clamped to the 128-entry frequency lookup table.
   */
  float calculateNoteFrequency(float note, int8_t octaveOffset, int harmony) noexcept;

  /**
   * @brief Effective row of the injected scale table the pitch path reads now
   *
   * Returns 0 when no table is injected (chromatic path). Clamps an
   * out-of-range queued scale index to the last row.
   */
  size_t effectiveScaleIndex_() const noexcept;

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
