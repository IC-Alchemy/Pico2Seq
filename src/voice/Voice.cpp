#include "Voice.h"
#include "../dsp/dsp.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include "../scales/scales.h" // Use centralized SCALES_COUNT / SCALE_STEPS
#include "VoicePresets.h"

// Constants
static constexpr float FREQ_SLEW_RATE = 0.00035f; // Slide speed
static constexpr float BASE_FREQ =
    110.0f; // Base frequency for note calculations

// Thread-safe one-time init guard for frequency table
namespace
{
  static std::once_flag g_freqTableOnce;
}

// Static member initialization
float Voice::frequencyLookupTable[128];
bool Voice::lookupTableInitialized = false;

// Initialize frequency lookup table covering MIDI 0..127
inline void Voice::initFrequencyLookupTable() noexcept
{
  std::call_once(g_freqTableOnce, []() noexcept
                 {
    // Use daisysp::mtof once per MIDI note value
    for (int midi = 0; midi < 128; ++midi)
    {
      frequencyLookupTable[midi] = daisysp::mtof(static_cast<float>(midi));
    } });
}

// Recompute cached detune multipliers using exp2f for efficiency
inline void Voice::recomputeDetuneMultipliers()
{
  // Precompute factor = 1/12 for semitone to octave conversion
  constexpr float kInv12 = 1.0f / 12.0f;
  // Limit to first 3 oscillators (design maximum)
  for (uint8_t i = 0; i < 3; ++i)
  {
    // detuneMul = 2^(semitones/12) = exp2f(semitones * (1/12))
    detuneMul[i] = exp2f(config.oscDetuning[i] * kInv12);
  }
}

// Helper to compute smoothing alpha for a one-pole smoother with time constant tau (seconds):
// alpha = 1 - exp(-1/(tau*fs)). Returns 1.0f when tau or sampleRate are non-positive.
static inline float makeSmoothingAlpha(float tauSeconds, float sampleRate) noexcept
{
  if (tauSeconds <= 0.0f || sampleRate <= 0.0f)
    return 1.0f;
  const float invTauFs = 1.0f / (tauSeconds * sampleRate);
  return 1.0f - std::exp(-invTauFs);
}

Voice::Voice(uint8_t id, const VoiceConfig &cfg)
    : voiceId(id), config(cfg), sampleRate(48000.0f), filterFrequency(1000.0f),
      gate(false),
      sequencer(nullptr)
{
  // Initialize frequency lookup table once in a thread-safe manner
  initFrequencyLookupTable();
  // Initialize cached detune multipliers
  recomputeDetuneMultipliers();

  // Initialize runtime caches used by optimizations
  lastAppliedFilterCutoff = -1.0f;
  lastEnvelopeValue = 0.0f;

  // Initialize oscillators vector
  oscillators.resize(config.oscillatorCount);

  // Initialize frequency slewing
  for (int i = 0; i < 3; i++)
  {
    freqSlew[i].currentFreq = 440.0f;
    freqSlew[i].targetFreq = 440.0f;
  }

  // Initialize voice state with defaults
  state.noteIndex = 0.0f;
  state.velocityLevel = 0.8f;
  state.filterCutoff = 0.37f;
  state.attackTimeSeconds = 0.01f;
  state.decayTimeSeconds = 0.1f;
  state.octaveOffset = 0;
  state.gateLengthTicks = 12; // Default gate length
  state.isGateHigh = false;
  state.hasSlide = false;
  state.shouldRetrigger = false;
}

void Voice::init(float sr)
{
  sampleRate = sr;

  // Compute per-sample slide coefficient from time constant
  slideAlpha = makeSmoothingAlpha(slideTimeSeconds, sampleRate);

  // Initialize wavefolder wet/dry smoothing (about 5ms time constant)
  {
    const float tau = 0.005f; // seconds
    wavefolderMixAlpha = makeSmoothingAlpha(tau, sampleRate);
    wavefolderMixTarget = config.hasWavefolder ? 1.0f : 0.0f;
    wavefolderMix = wavefolderMixTarget; // start with no transition
  }

  // Initialize oscillators
  for (size_t i = 0; i < oscillators.size() && i < config.oscillatorCount;
       i++)
  {
    oscillators[i].Init(sampleRate);
    oscillators[i].SetWaveform(config.oscWaveforms[i]);
    oscillators[i].SetAmp(config.oscAmplitudes[i]);

    // Set pulse width for square/pulse waves
    if (config.oscWaveforms[i] == daisysp::Oscillator::WAVE_POLYBLEP_SAW)
    {
      oscillators[i].SetPw(config.oscPulseWidth[i]);
    }
  }

  // Initialize noise generator
  noise_.Init();
  noise_.SetSeed(1);
  noise_.SetAmp(1.0f);

  // Initialize filter
  filter.Init(sampleRate);
  filter.SetFreq(filterFrequency);
  filter.SetRes(config.filterRes);
  filter.SetInputDrive(config.filterDrive);
  filter.SetPassbandGain(config.filterPassbandGain);
  filter.SetFilterMode(config.filterMode);
  // Initialize high-pass filter
  highPassFilter.Init(sampleRate);
  highPassFilter.SetFreq(config.highPassFreq);
  highPassFilter.SetRes(config.highPassRes);
  // Initialize filter cutoff smoothing state (reduces zipper noise from abrupt SetFreq calls).
  // Use a short time-constant (4 ms) to remain responsive while smoothing envelope-modulation.
  filterCutoffCurrent = filterFrequency;
  {
    const float tau = 0.004f; // seconds
    filterCutoffAlpha = makeSmoothingAlpha(tau, sampleRate);
  }

  // Initialize runtime caches used by optimizations
  lastAppliedFilterCutoff = -1.0f;
  lastEnvelopeValue = 0.0f;

  // Initialize envelope
  envelope.Init(sampleRate);
  envelope.SetAttackTime(config.defaultAttack);
  envelope.SetDecayTime(config.defaultDecay);
  envelope.SetSustainLevel(config.defaultSustain);
  envelope.SetReleaseTime(config.defaultRelease);

  // Initialize effects
  overdrive.Init();
  overdrive.SetDrive(config.overdriveDrive);

  // Always initialize wavefolder so it's safe to process even when toggled at runtime
  wavefolder.Init();
  wavefolder.SetGain(config.wavefolderGain);
  wavefolder.SetOffset(config.wavefolderOffset);

  // Update detune multipliers in case config changed before init
  recomputeDetuneMultipliers();
}

void Voice::setConfig(const VoiceConfig &cfg)
{
  // Update configuration without full reinitialization to avoid audio dropouts
  const bool oscCountChanged = (cfg.oscillatorCount != config.oscillatorCount);
  config = cfg;

  // Do not resize oscillators here to avoid reallocations in the audio thread
  // If the oscillator count changed, we defer vector resizing until a safe point
  if (!oscCountChanged)
  {
    // Update oscillator params non-destructively
    const size_t oscCount = std::min(static_cast<size_t>(3), oscillators.size());
    for (size_t i = 0; i < oscCount; i++)
    {
      oscillators[i].SetWaveform(config.oscWaveforms[i]);
      oscillators[i].SetAmp(config.oscAmplitudes[i]);
      if (config.oscWaveforms[i] == daisysp::Oscillator::WAVE_POLYBLEP_SQUARE)
      {
        oscillators[i].SetPw(config.oscPulseWidth[i]);
      }
    }
  }

  // Update filter and high-pass parameters
  filter.SetRes(config.filterRes);
  filter.SetInputDrive(config.filterDrive);
  filter.SetPassbandGain(config.filterPassbandGain);
  filter.SetFilterMode(config.filterMode);
  highPassFilter.SetFreq(config.highPassFreq);
  highPassFilter.SetRes(config.highPassRes);

  // Update overdrive params (module is stateless aside from drive/gain)
  if (config.hasOverdrive)
  {
    overdrive.SetDrive(config.overdriveDrive);
  }

  // Wavefolder gain/offset are applied per-sample in applyEffects();
  // update the mix target here to crossfade smoothly after a toggle
  wavefolderMixTarget = config.hasWavefolder ? 1.0f : 0.0f;

  // Envelope defaults are handled by applyEnvelopeParameters() from state,
  // so no need to touch ADSR timings here.

  // Refresh cached detune multipliers for semitone detuning
  recomputeDetuneMultipliers();
}



// Injected scale-data setters (defined out-of-line)
void Voice::setScaleTable(const int (*table)[48], size_t scaleCount)
{
  // Assign pointers first (external owner still owns memory).
  scaleTable = table;
  scaleTableCount = scaleCount;

  // Precompute per-scale unique-degree caches used by calculateNoteFrequency.
  // This is intentionally done outside the realtime path and may allocate/free.
  scaleUniqueCounts.clear();
  scaleIndexToRank.clear();
  scaleUniqueIndexList.clear();

  if (scaleTable == nullptr || scaleTableCount == 0)
  {
    // Nothing to cache
    return;
  }

  // Reserve memory for deterministic layout: scaleCount * 48 entries each
  scaleUniqueCounts.resize(scaleCount);
  scaleIndexToRank.resize(scaleCount * 48);
  scaleUniqueIndexList.resize(scaleCount * 48);

  for (size_t s = 0; s < scaleCount; ++s)
  {
    const int *row = scaleTable[s];

    // Build list of unique starting indices (first occurrence of each semitone step)
    uint8_t uniquePos[48];
    uint8_t uniqueCount = 0;

    // First unique is always index 0
    uniquePos[uniqueCount++] = 0;
    for (int i = 1; i < static_cast<int>(SCALE_STEPS); ++i)
    {
      if (row[i] != row[i - 1])
      {
        uniquePos[uniqueCount++] = static_cast<uint8_t>(i);
      }
    }

    // Store unique count
    scaleUniqueCounts[s] = uniqueCount;

    // Write unique index list padded into the per-scale slot (first uniqueCount entries valid)
    const size_t base = s * 48;
    for (uint8_t u = 0; u < uniqueCount; ++u)
    {
      scaleUniqueIndexList[base + u] = uniquePos[u];
    }
    // Pad remaining with last value (safe, won't be referenced)
    for (uint8_t u = uniqueCount; u < 48; ++u)
    {
      scaleUniqueIndexList[base + u] = uniquePos[uniqueCount - 1];
    }

    // Build index->rank mapping: for each original index j, find which unique segment it belongs to.
    for (uint8_t u = 0; u < uniqueCount; ++u)
    {
      const uint8_t start = uniquePos[u];
      const uint8_t end = (u + 1 < uniqueCount) ? static_cast<uint8_t>(uniquePos[u + 1] - 1) : static_cast<uint8_t>(SCALE_STEPS - 1);
      for (uint8_t j = start; j <= end; ++j)
      {
        scaleIndexToRank[base + j] = u;
      }
    }
  }
}

void Voice::setCurrentScalePointer(const uint8_t *ptr)
{
  currentScalePtr = ptr;
}

float Voice::process() noexcept
{
  if (!config.enabled)
  {
    return 0.0f;
  }

  // 1) Envelope
  float envelopeValue = computeEnvelope();
  // Cache envelope value for cheap per-voice checks in hot paths (avoids reprocessing ADSR)
  lastEnvelopeValue = envelopeValue;

  // 2) Filter cutoff update (uses envelope)
  updateFilter(envelopeValue);

  // 3) Oscillator/engine mixing (+ slide updates)
  float mixed = mixOscillators();

  // 4) Effects and gain shaping pre-filter

  // 5) Filter processing, HPF, and final scaling
  return finalizeOutput(mixed, envelopeValue);
}

float Voice::computeEnvelope()
{
  // Handle envelope retrigger
  if (state.shouldRetrigger)
  {
    envelope.Retrigger(false);
    state.shouldRetrigger = false;
  }

  // Process envelope (or bypass)
  float envelopeValue = config.hasEnvelope ? envelope.Process(gate) : 1.0f;
  return envelopeValue;
}

void Voice::updateFilter(float envelopeValue)
{
  // Compute the intended (instantaneous) cutoff target using previous logic
  const float targetCutoff = (filterFrequency * envelopeValue) + (filterFrequency * 0.1f);

  // Exponential smoothing to prevent zipper noise when targetCutoff jumps.
  // filterCutoffAlpha was initialized in init() (per-sample coefficient).
  filterCutoffCurrent += filterCutoffAlpha * (targetCutoff - filterCutoffCurrent);

  // Thresholded SetFreq to avoid unnecessary work when cutoff changes are tiny.
  const float cutoffChangeThreshold = 0.5f; // Hz absolute threshold
  if (fabsf(filterCutoffCurrent - lastAppliedFilterCutoff) > cutoffChangeThreshold)
  {
    filter.SetFreq(filterCutoffCurrent);
    lastAppliedFilterCutoff = filterCutoffCurrent;
  }
}

float Voice::mixOscillators()
{

  float mixedOscillators = 0.0f;

  // Very cheap per-voice silence short-circuit: if envelope is enabled and the cached
  // envelope value is effectively zero, skip oscillator/noise processing entirely.
  if (config.hasEnvelope && lastEnvelopeValue <= 0.0005f)
  {
    return 0.0f;
  }

  // Determine number of oscillators to process (max 3)
  const size_t oscCount = std::min(static_cast<size_t>(3), oscillators.size());

  if (oscCount > 0)
  {
    // Update frequencies (slew when sliding) and process oscillators
    for (size_t i = 0; i < oscCount; i++)
    {
      if (state.hasSlide)
      {
        processFrequencySlew(i, freqSlew[i].targetFreq);
        oscillators[i].SetFreq(freqSlew[i].currentFreq);
      }
      mixedOscillators += oscillators[i].Process();
    }
  }
  else
  {
    // Special case for percussion voices (no oscillators, only noise)
    mixedOscillators = noise_.Process();
  }

  return mixedOscillators;
}

void Voice::applyEffects(float &signal)
{
  // Apply effects chain with cheap bypass guards to avoid calling
  // heavy Process() methods when they will produce no audible output.

  // Overdrive: only process when enabled AND the configured output gain is non-zero.
  if (config.hasOverdrive )
  {
    signal = overdrive.Process(signal) * config.overdriveGain;
  }

  // Wavefolder: only process when enabled AND the current wet mix is significant.
  if (config.hasWavefolder )
  {
    signal = wavefolder.Process(signal);
  }

  // Level adjustments removed from here; handled in finalizeOutput
}

// Provide a wrapper to maintain API compatibility
void Voice::processEffectsChain(float &signal)
{
  applyEffects(signal);
}

inline float Voice::finalizeOutput(float signal, float envelopeValue) noexcept
{

    // Apply effects (pre-filter)
  applyEffects(signal);
  // Apply ladder filter
  float filteredSignal = filter.Process(signal);



  // Apply high-pass filter
  highPassFilter.Process(filteredSignal);

  // Get high-passed signal (no envelope or level applied here yet)
  float highPassedSignal = highPassFilter.High();

  // Apply envelope multiplication and final output scaling (including velocity-based level)
  float finalOutput = highPassedSignal * envelopeValue;
  const float outputScale = config.outputLevel * state.velocityLevel;
  finalOutput *= outputScale;
  return finalOutput;
}


void Voice::updateOscillatorFrequencies()
{
  // Only update oscillator frequencies if the gate is high.
  // This prevents re-pitching during the release phase of the envelope.
  if (!state.isGateHigh)
  {
return;
  }
    // Always update frequencies based on current state so parameter changes apply immediately
    // Calculate base frequency once and cache it (used when harmony offset is 0)
    const float baseFreq = calculateNoteFrequency(state.noteIndex, state.octaveOffset, 0);

    // Limit oscillator loop to max 3
    const size_t oscCount = std::min(static_cast<size_t>(3), oscillators.size());

    for (size_t i = 0; i < oscCount; i++)
    {
      // Calculate frequency for this oscillator using harmony interval
      float harmonyFreq;
      const int harmonyInterval = config.harmony[i];

      if (harmonyInterval == 0)
      {
        harmonyFreq = baseFreq;
      }
      else
      {
        harmonyFreq = calculateNoteFrequency(state.noteIndex, state.octaveOffset, harmonyInterval);
      }

      // Apply semitone-based detuning using cached multiplier
      const float targetFreq = harmonyFreq * detuneMul[i];

      if (state.hasSlide)
      {
        // Set target for slewing
        freqSlew[i].targetFreq = targetFreq;
      }
      else
      {
        // Set frequency directly
        oscillators[i].SetFreq(targetFreq);
        freqSlew[i].currentFreq = targetFreq;
        freqSlew[i].targetFreq = targetFreq;
      }
    }
  }


inline void Voice::applyEnvelopeParameters() noexcept
{
  // Map normalized parameters to appropriate ranges
  float attack =
      daisysp::fmap(state.attackTimeSeconds, 0.002f, 0.75f, daisysp::Mapping::LINEAR);
  float decay =
      daisysp::fmap(state.decayTimeSeconds, 0.002f, 0.8f, daisysp::Mapping::LOG);
  //float release = decay; // Use decay for release in this implementation

  envelope.SetAttackTime(attack, 1.6f);
  envelope.SetDecayTime(0.1f + (decay * 0.32f));
  envelope.SetReleaseTime(decay);
}

inline float Voice::calculateNoteFrequency(float note, int8_t octaveOffset,
                                    int harmony) noexcept
{
  // Clamp note to valid range
  const int noteIndex = std::max(0, std::min(static_cast<int>(note), static_cast<int>(SCALE_STEPS - 1)));

  int harmonyNoteIndex = noteIndex;
  int scaleSemitone;

  // Resolve scale step to semitone offset using injected scale table if available
  if (scaleTable && scaleTableCount > 0 && currentScalePtr)
  {
    const uint8_t idx = *currentScalePtr;
    if (idx < scaleTableCount)
    {
      // Use pre-calculated LUTs for harmony resolution
      const int uniqueCount = scaleUniqueCounts[idx];
      if (uniqueCount > 0)
      {
        // These are flat arrays; calculate pointer to the start of the current scale's data.
        const auto *rankLut = &scaleIndexToRank[idx * 48];
        const auto *uniqueIndexLut = &scaleUniqueIndexList[idx * 48];

        // 1. Find the rank of the starting note in its scale
        int startRank = rankLut[noteIndex];

        // 2. Add harmony offset to get the target rank
        int targetRank = startRank + harmony;

        // 3. Clamp the rank to the valid range of unique notes for the scale
        targetRank = std::max(0, std::min(targetRank, uniqueCount - 1));

        // 4. Find the index in the original scale table corresponding to the target rank
        harmonyNoteIndex = uniqueIndexLut[targetRank];
      }
      // else: if uniqueCount is 0, something is wrong, but we'll just use noteIndex.

      // Final semitone lookup from the scale table
      scaleSemitone = scaleTable[idx][harmonyNoteIndex];
    }
    else
    {
      // Invalid scale index; fall back to chromatic
      harmonyNoteIndex = std::max(0, std::min(noteIndex + harmony, static_cast<int>(SCALE_STEPS - 1)));
      scaleSemitone = harmonyNoteIndex;
    }
  }
  else
  {
    // No scale table; fall back to chromatic
    harmonyNoteIndex = std::max(0, std::min(noteIndex + harmony, static_cast<int>(SCALE_STEPS - 1)));
    scaleSemitone = harmonyNoteIndex;
  }

  // Base MIDI mapping: center around 48 (C3) then add octave offset
  int midiNote = scaleSemitone + 48 + static_cast<int>(octaveOffset);

  // Clamp to valid MIDI range
  midiNote = std::max(0, std::min(midiNote, 127));

  return frequencyLookupTable[midiNote];
}

void Voice::processFrequencySlew(uint8_t oscIndex, float targetFreq)
{
  if (oscIndex >= 3)
    return;

  // Exponential slewing for smooth frequency transitions
  const float delta = freqSlew[oscIndex].targetFreq - freqSlew[oscIndex].currentFreq;
  freqSlew[oscIndex].currentFreq = std::fmaf(delta, slideAlpha, freqSlew[oscIndex].currentFreq);
}

void Voice::setFrequency(float frequency)
{
  // Always set oscillator frequencies so pitch-related changes are reflected immediately
  for (uint8_t i = 0; i < config.oscillatorCount && i < oscillators.size() && i < 3; i++)
  {
    float targetFreq;

    // Apply cached semitone-based detuning multiplier
    targetFreq = frequency * detuneMul[i];

    if (state.hasSlide)
    {
      // Set target for slewing (slide functionality)
      freqSlew[i].targetFreq = targetFreq;
    }
    else
    {
      // Set frequency directly
      oscillators[i].SetFreq(targetFreq);
      freqSlew[i].currentFreq = targetFreq;
      freqSlew[i].targetFreq = targetFreq;
    }
  }
}

void Voice::setSlideTime(float slideTime)
{
  // Clamp to reasonable range [0, 10] seconds; 0 means instantaneous
  if (slideTime < 0.0f)
    slideTime = 0.0f;
  if (slideTime > 10.0f)
    slideTime = 10.0f;

  slideTimeSeconds = slideTime;

  // Recompute per-sample coefficient using current sample rate
  if (slideTimeSeconds <= 0.0f || sampleRate <= 0.0f)
  {
    slideAlpha = 1.0f; // jump to target in one sample when slide enabled
  }
  else
  {
    const float invTauFs = 1.0f / (slideTimeSeconds * sampleRate);
    slideAlpha = 1.0f - std::exp(-invTauFs);
  }
}

void Voice::updateParameters(const VoiceState &newState)
{
  // Update voice state
  state = newState;

  // Ensure the internal gate variable used by the ADSR is synchronized with the
  // incoming sequencer/MIDI state. Previously this was commented out which left
  // `gate` stale and allowed audio to continue when it shouldn't.
  setGate(state.isGateHigh);

  // Calculate filter frequency from normalized filter parameter (0.0-1.0)
  // Using exponential mapping like PicoMudrasSequencer.ino: 100Hz to 9710Hz
  filterFrequency = daisysp::fmap(state.filterCutoff, 250.0f, 8000.0f, daisysp::Mapping::EXP);

  // Apply envelope parameters (attack, decay/release)
  applyEnvelopeParameters();

  // Update oscillator frequencies based on new note/octave/slide state
  updateOscillatorFrequencies();
}

// Voice Presets moved to src/voice/VoicePresets.cpp

void Voice::setSequencer(std::unique_ptr<Sequencer> seq)
{
  // Take ownership and store raw pointer for quick access
  sequencerOwned = std::move(seq);
  sequencer = sequencerOwned.get();
}

void Voice::setSequencer(Sequencer *seq)
{
  // Release any previously owned sequencer and set raw pointer (no ownership)
  sequencerOwned.reset();
  sequencer = seq;
}
