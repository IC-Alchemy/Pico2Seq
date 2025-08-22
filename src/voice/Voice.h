#pragma once

#include "../dsp/oscillator.h"
#include "../dsp/ladder.h"
#include "../dsp/svf.h"
#include "../dsp/adsr.h"
#include "../dsp/overdrive.h"
#include "../dsp/wavefolder.h"
#include "../dsp/whitenoise.h"
#include "../sequencer/Sequencer.h"
#include "../sequencer/SequencerDefs.h"
#include <vector>
#include <memory>

#include <cstddef>
#include <cstdint>

/**
 * @brief Configuration structure for a voice
 * 
 * Defines the characteristics and behavior of a synthesizer voice including
 * oscillator settings, filter parameters, effects chain, and envelope configuration.
 */
struct VoiceConfig {
  // Custom waveform constants
  static constexpr uint8_t WAVE_NOISE = 255; // Custom noise waveform identifier

  // Oscillator configuration
  uint8_t oscillatorCount = 3; // Number of oscillators (1-3)
  uint8_t oscWaveforms[3] = { // Waveform types for each oscillator (DaisySP waveform constants)
    daisysp::Oscillator::WAVE_POLYBLEP_SAW,
    daisysp::Oscillator::WAVE_POLYBLEP_SAW,
    daisysp::Oscillator::WAVE_POLYBLEP_SAW
  };
  float oscAmplitudes[3] = {0.5f, 0.5f, 0.5f}; // Oscillator amplitudes (0.0-1.0)
  float oscDetuning[3] = {0.0f, 0.0f, 0.0f}; // Detuning in semitones (-12.0 to +12.0)
  float oscPulseWidth[3] = {0.5f, 0.5f, 0.5f}; // Pulse width for square/pulse waves (0.0-1.0)
  int harmony[3] = {0, 0, 0}; // Harmony intervals in scale steps (-12 to +12)

  // Filter settings
  float filterRes = 0.2f; // Filter resonance (0.0-1.0)
  float filterDrive = 1.8f; // Filter drive amount (0.0-10.0)
  float filterPassbandGain = 0.23f; // Passband gain compensation (0.0-1.0)
  daisysp::LadderFilter::FilterMode filterMode = daisysp::LadderFilter::FilterMode::LP24; // Filter mode

  // High-pass filter settings
  float highPassFreq = 80.0f; // High-pass cutoff frequency in Hz (20.0-20000.0)
  float highPassRes = 0.1f; // High-pass resonance (0.0-1.0)

  // Effects chain configuration
  bool hasOverdrive = false; // Enable overdrive effect
  bool hasWavefolder = false; // Enable wavefolder effect
  bool hasEnvelope = true; // Enable envelope (recommended: true)
  bool hasDalek = false; // Enable Dalek vocoder effect
  float overdriveGain = 0.34f; // Overdrive output gain (0.0-2.0)
  float overdriveDrive = 0.25f; // Overdrive drive amount (0.0-1.0)
  float wavefolderGain = 3.5f; // Wavefolder gain (0.0-10.0)
  float wavefolderOffset = 2.0f; // Wavefolder DC offset (0.0-5.0)


  // Envelope default settings
  float defaultAttack = 0.04f; // Default attack time in seconds (0.001-10.0)
  float defaultDecay = 0.14f; // Default decay time in seconds (0.001-10.0)
  float defaultSustain = 0.5f; // Default sustain level (0.0-1.0)
  float defaultRelease = 0.1f; // Default release time in seconds (0.001-10.0)

  // Voice mixing
  float outputLevel = 0.6f; // Voice output level (0.0-1.0)
  bool enabled = true; // Voice enabled state
};

/**
 * @brief Frequency slewing parameters for smooth slide transitions
 * 
 * Used to implement smooth frequency transitions between notes when slide is enabled.
 */
struct VoiceSlewParams {
  float currentFreq = 440.0f; // Current frequency in Hz (20.0-20000.0)
  float targetFreq = 440.0f; // Target frequency in Hz (20.0-20000.0)
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
class Voice {
public:
  /**
   * @brief Construct a new Voice object
   * @param id Unique identifier for this voice (0-7)
   * @param config Configuration structure defining voice characteristics
   */
  Voice(uint8_t id, const VoiceConfig& config);

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
  void setConfig(const VoiceConfig& config);

  /**
   * @brief Get the current voice configuration
   * @return const VoiceConfig& Current configuration
   */
  const VoiceConfig& getConfig() const { return config; }

  /**
   * @brief Get mutable reference to voice configuration
   * @return VoiceConfig& Reference to voice configuration
   */
  VoiceConfig& getConfig() { return config; }

  // Audio processing
  /**
   * @brief Process one sample of audio
   * @return float Processed audio sample (-1.0 to +1.0 range)
   */
  float process();

  /**
   * @brief Update voice parameters from sequencer state
   * @param newState New voice state from sequencer containing note, velocity, filter, envelope parameters
   */
  void updateParameters(const VoiceState& newState);

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
  void setSequencer(Sequencer* seq);

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
  void setCurrentScalePointer(const uint8_t* currentScalePtr);

  /**
   * @brief Get pointer to the voice's sequencer
   * @return Sequencer* Pointer to sequencer (nullptr if not set)
   */
  Sequencer* getSequencer() { return sequencer; }

  // State management
  /**
   * @brief Get reference to current voice state
   * @return VoiceState& Reference to voice state containing current parameters
   */
  VoiceState& getState() { return state; }

  /**
   * @brief Get const reference to current voice state
   * @return const VoiceState& Const reference to voice state
   */
  const VoiceState& getState() const { return state; }

  /**
   * @brief Set gate state for this voice
   * @param gateState True for gate on (note triggered), false for gate off (note released)
   */
  void setGate(bool gateState) { 
    gate = gateState; 
    state.isGateHigh = gateState; // Synchronize both gate variables
  }

  /**
   * @brief Get current gate state
   * @return bool Current gate state (true = on, false = off)
   */
  bool getGate() const { return gate; }

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
  float getFilterFrequency() const { return filterFrequency; }

  // Voice identification
  /**
   * @brief Get voice ID
   * @return uint8_t Voice identifier (0-7)
   */
  uint8_t getId() const { return voiceId; }

  /**
   * @brief Check if voice is enabled
   * @return bool True if voice is enabled and will process audio
   */
  bool isEnabled() const { return config.enabled; }

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
  const uint8_t* currentScalePtr = nullptr; // Pointer to externally managed current-scale index

  // Audio processing components
  std::vector<daisysp::Oscillator> oscillators;
  daisysp::WhiteNoise noise_;
  daisysp::LadderFilter filter;
  daisysp::Svf highPassFilter;
  daisysp::Adsr envelope;
  daisysp::Overdrive overdrive;
  daisysp::Wavefolder wavefolder;

  // Voice state and control
  VoiceState state;
  float filterFrequency;
  VoiceSlewParams freqSlew[3]; // For slide functionality
  volatile bool gate;

  // Slide/portamento control
  // slideTimeSeconds is the exponential time constant in seconds
  // slideAlpha is the per-sample coefficient computed from slideTimeSeconds and sampleRate
  float slideTimeSeconds = 0.06f;
  float slideAlpha = 0.00035f;

  // Sequencer integration
  Sequencer* sequencer;
  // Owns the sequencer when attached via unique_ptr overload
  std::unique_ptr<Sequencer> sequencerOwned;

  // Private helper methods
  /**
   * @brief Process the effects chain on the input signal
   * @param signal Reference to signal to process (modified in place, -1.0 to +1.0)
   */
  void processEffectsChain(float& signal);

  // Private DSP stages used by process()
  /**
   * @brief Compute envelope value for current sample
   * @return float Envelope amplitude (0.0-1.0)
   */
  float computeEnvelope();

  /**
   * @brief Update filter parameters based on envelope and voice state
   * @param envelopeValue Current envelope value (0.0-1.0)
   */
  void updateFilter(float envelopeValue);

  /**
   * @brief Mix all oscillator outputs
   * @return float Mixed oscillator signal (-1.0 to +1.0)
   */
  float mixOscillators();

  /**
   * @brief Apply effects chain to signal
   * @param signal Reference to signal to process (modified in place)
   */
  void applyEffects(float& signal);

  /**
   * @brief Finalize output with level and envelope
   * @param signal Input signal (-1.0 to +1.0)
   * @param envelopeValue Envelope amplitude (0.0-1.0)
   * @return float Final output signal (-1.0 to +1.0)
   */
  float finalizeOutput(float signal, float envelopeValue);

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
  void applyEnvelopeParameters();

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
  float calculateNoteFrequency(float note, int8_t octaveOffset, int harmony);

  /**
   * @brief Initialize frequency lookup table (called once)
   * 
   * Populates static lookup table covering all MIDI notes (0-127) for
   * performance optimization, avoiding repeated mtof() calculations.
   */
  static void initFrequencyLookupTable();

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