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
  // Bump detune version so pitch cache will recompute
  detuneVersion_++;
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
  // Static base pitch cache starts dirty to force initial compute
  baseFreqDirty_ = true;
  cachedBaseFreqHz_ = 440.0f;
  lastSentBaseFreqHz_ = -1.0f;
  // Initialize frequency lookup table once in a thread-safe manner
  initFrequencyLookupTable();
  // Initialize cached detune multipliers
  recomputeDetuneMultipliers();

  // Initialize runtime caches used by optimizations
  lastAppliedFilterCutoff = -1.0f;
  lastEnvelopeValue = 0.0f;

  // Pre-size oscillator container to a fixed maximum of 3 to avoid runtime allocations
  oscillators.reserve(3);
  oscillators.resize(3);

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
  // Changing sample rate can affect tuning in downstream modules; ensure base frequency recompute
  baseFreqDirty_ = true;

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
  cachedOscCount_ = static_cast<uint8_t>(std::min<size_t>(3, config.oscillatorCount));
  for (size_t i = 0; i < cachedOscCount_; i++)
  {
    oscillators[i].Init(sampleRate);
    oscillators[i].SetWaveform(config.oscWaveforms[i]);
    oscillators[i].SetAmp(config.oscAmplitudes[i]);

    // Set pulse width for square/pulse waves
    if (config.oscWaveforms[i] == daisysp::Oscillator::WAVE_POLYBLEP_SQUARE)
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
  // Control-thread: stage config and apply on audio thread to avoid races
  stagedConfig_ = cfg;
  configPending_.store(true, std::memory_order_release);
}



// Injected scale-data setters (defined out-of-line)
void Voice::setScaleTable(const int (*table)[48], size_t scaleCount)
{
  // Assign pointers first (external owner still owns memory).
  scaleTable = table;
  scaleTableCount = scaleCount;
  // Scale/tuning data impacts static pitch mapping; mark base cache dirty.
  baseFreqDirty_ = true;

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
  // Changing active scale changes static note->semitone mapping.
  baseFreqDirty_ = true;
}

float Voice::process() noexcept
{
  if (!config.enabled)
  {
    return 0.0f;
  }

  // Apply any pending cross-core parameter/config changes at the audio rate.
  applyPendingConfig_();
  applyPendingParams_();

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

  // Throttle SetFreq to avoid per-sample heavy work if change is tiny
  if (filterUpdateInterval == 0)
  {
    filter.SetFreq(filterCutoffCurrent);
    lastAppliedFilterCutoff = filterCutoffCurrent;
  }
  else
  {
    if (filterUpdateCounter == 0)
    {
      if (ShouldApplyFilterFreq_(filterCutoffCurrent, lastAppliedFilterCutoff))
      {
        filter.SetFreq(filterCutoffCurrent);
        lastAppliedFilterCutoff = filterCutoffCurrent;
      }
    }
    filterUpdateCounter = static_cast<uint8_t>((filterUpdateCounter + 1) % filterUpdateInterval);
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

  // Determine number of oscillators to process (max 3 pre-sized)
  const size_t oscCount = cachedOscCount_;

  // Audio-thread commit of frequency changes:
  // - Only when gate HIGH (no repitch during release)
  // - Lock-free via generation counter
  if (oscCount > 0 && state.isGateHigh)
  {
    const uint32_t gen = pitchGen_.load(std::memory_order_seq_cst);
    if (!state.hasSlide && gen != appliedPitchGen_)
    {
      // Commit immediate frequencies (no slide)
      for (size_t i = 0; i < oscCount; i++)
      {
        const float f = pitchCache_.finalFreq[i];
        if (ShouldApplyFreq_(f, lastAppliedOscFreq_[i]))
        {
          oscillators[i].SetFreq(f);
          lastAppliedOscFreq_[i] = f;
          // Keep slew state consistent
          freqSlew[i].currentFreq = f;
          freqSlew[i].targetFreq  = f;
        }
      }
      appliedPitchGen_ = gen;
    }
    else if (state.hasSlide && gen != appliedPitchGen_)
    {
      // On gen change, update targets; slewing occurs per-sample below
      for (size_t i = 0; i < oscCount; i++)
      {
        const float f = pitchCache_.finalFreq[i];
        freqSlew[i].targetFreq = f;
      }
      appliedPitchGen_ = gen;
    }
  }

  if (oscCount > 0)
  {
    // Update frequencies (slew when sliding) and process oscillators
    for (size_t i = 0; i < oscCount; i++)
    {
      if (state.hasSlide)
      {
        processFrequencySlew(i, freqSlew[i].targetFreq);
        const float fcur = freqSlew[i].currentFreq;
        if (ShouldApplyFreq_(fcur, lastAppliedOscFreq_[i]))
        {
          oscillators[i].SetFreq(fcur);
          lastAppliedOscFreq_[i] = fcur;
        }
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

  if (config.hasWavefolder)
  {
    signal = wavefolder.Process(signal);
  }
  if (config.hasOverdrive )
  {
    signal = overdrive.Process(signal* config.overdriveGain) ;
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
float preEffects =signal*envelopeValue;
    // Apply effects (pre-filter)
    //    VCA envelope is applied pre effects so that the Wavefolder/ overdrive sounds more dynamic
  applyEffects(preEffects);

  // Apply ladder filter
  float filteredSignal = filter.Process(preEffects*state.velocityLevel);

  // Apply optional high-pass filter
  float postHpf = filteredSignal;
  if (!hpfBypass_)
  {
    highPassFilter.Process(filteredSignal);
    postHpf = highPassFilter.High();
  }

  float finalOutput = postHpf * config.outputLevel ;

  return finalOutput;
}


void Voice::updateOscillatorFrequencies()
{
  // Deprecated path for direct control-thread commits; retained for backward compatibility.
  // New flow stages frequencies via updateFrequencyIfNeeded() and commits on audio thread.
  updateFrequencyIfNeeded();
}


inline void Voice::applyEnvelopeParameters() noexcept
{
  // Map normalized parameters to appropriate ranges
  float attack =
      daisysp::fmap(state.attackTimeSeconds, 0.002f, 0.75f, daisysp::Mapping::LINEAR);
  float decay =
      daisysp::fmap(state.decayTimeSeconds, 0.002f, 0.8f, daisysp::Mapping::LOG);
  //float release = decay; // Use decay for release in this implementation

  envelope.SetAttackTime(attack, .75f);
  envelope.SetDecayTime(0.075f + (decay * 0.32f));
  envelope.SetReleaseTime(decay);
}

inline float Voice::calculateNoteFrequency(float note, int8_t octaveOffset,
                                    int harmony) noexcept
{
  // Clamp input note to valid range [0, SCALE_STEPS-1]
  const int noteIndex =note;

  // Resolve semitone via direct scale lookup:
  // scaleIndex comes from currentScalePtr if present, otherwise 0.
  uint8_t scaleIndex = 0;
  if (currentScalePtr)
    scaleIndex = *currentScalePtr;

  int noteWithHarmony = noteIndex + harmony;

  // Lookup semitone directly from scale table (assumes scale[][] exists and is indexed as scale[scaleIndex][noteIndex])
  int scaleSemitone = scale[scaleIndex][noteWithHarmony];

  // Map to MIDI centered at 48 (C3) and apply octave offset
  int midiNote = scaleSemitone + 48 + static_cast<int>(octaveOffset);

  return frequencyLookupTable[midiNote];
}

// Static base recompute: includes ONLY static contributors (note, octave/transpose,
// scale/tuning mapping). Dynamic modulators (vibrato LFOs, envelopes, glide/portamento,
// and bend/mod depth) are applied later and are NOT baked into cachedBaseFreqHz_.
void Voice::recomputeBaseFreqIfDirty_()
{
  if (!baseFreqDirty_)
    return;

  cachedBaseFreqHz_ = calculateNoteFrequency(state.noteIndex, state.octaveOffset, 0);
  baseFreqDirty_ = false;
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
  // Control-thread: do not call SetFreq directly. Stage targets and cache only.
  for (uint8_t i = 0; i < config.oscillatorCount && i < oscillators.size() && i < 3; i++)
  {
    const float targetFreq = frequency * detuneMul[i];
    // Update slew state; audio thread will commit via mixOscillators()
    if (state.hasSlide)
    {
      freqSlew[i].targetFreq = targetFreq;
    }
    else
    {
      // Keep local state consistent but avoid SetFreq here
      freqSlew[i].currentFreq = targetFreq;
      freqSlew[i].targetFreq  = targetFreq;
    }
  }
  // Mark staging dirty so audio thread commits on next frame
  markPitchDirty();
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
  // Slide parameter affects commit behavior; mark pitch staging
  markPitchDirty();
}

void Voice::updateParameters(const VoiceState &newState)
{
  // Control-thread: stage state and bump generation counter for audio-thread application
  stagedState_ = newState;
  // Base pitch may change due to note/octave. Mark dirty based on previous staged state.
  if (stagedState_.noteIndex != state.noteIndex || stagedState_.octaveOffset != state.octaveOffset)
  {
    baseFreqDirty_ = true;
  }
  paramsGen_.fetch_add(1u, std::memory_order_seq_cst);
}

 // Voice Presets moved to src/voice/VoicePresets.cpp

// -------- Pitch optimization: change detection, cache, and API --------
// Fields watched: noteIndex, octaveOffset, harmony[0..oscCount-1], oscCount, detuneVersion_,
// Oscillator::GetSampleRateVersion(), hasSlide, pitch bend/mod in semitones.
// Generation-based staging: control thread recomputes cache and bumps pitchGen_;
// audio thread commits in mixOscillators() when pitchGen_ != appliedPitchGen_.
// SetFreq gating: ShouldApplyFreq_ uses kPitchRelEps and kPitchAbsEpsHz (≈0.017 cent minimum)
// to cut redundant oscillator.SetFreq calls, including during slide slews.

// Compare current/new state & dependencies to snapshot to decide if recompute needed.
// Note: also watches detuneVersion_ and global Oscillator sample-rate version.
bool Voice::pitchParamsChanged_(const VoiceState& newState) const
{
  const uint8_t oscCount = cachedOscCount_;
  if (pitchSnapshot_.oscCount != oscCount) return true;
  if (pitchSnapshot_.noteIndex != newState.noteIndex) return true;
  if (pitchSnapshot_.octaveOffset != newState.octaveOffset) return true;
  if (pitchSnapshot_.hasSlide != newState.hasSlide) return true;
  // Harmony
  for (uint8_t i = 0; i < oscCount; ++i)
  {
    if (pitchSnapshot_.harmony[i] != config.harmony[i]) return true;
  }
  // Detune version
  if (pitchSnapshot_.detuneVersion != detuneVersion_) return true;
  // Sample rate version (from Oscillator)
  if (pitchSnapshot_.srVersion != daisysp::Oscillator::GetSampleRateVersion()) return true;
  // Pitch bend/mod snapshots
  if (pitchSnapshot_.bendSemis != pitchBendSemitones_) return true;
  if (pitchSnapshot_.modSemis  != pitchModSemitones_) return true;

  return false;
}

// Recompute pitch cache using current state/config and pitch controls.
// Writes pitchCache_ then bumps generation.
void Voice::updatePitchCache_()
{
  const uint8_t oscCount = cachedOscCount_;

  // Ensure static base is up to date; avoids redoing static work when only dynamics change.
  recomputeBaseFreqIfDirty_();
  const float baseFreq = cachedBaseFreqHz_;

  // Combined dynamic pitch (bend + mod) in semitones (dynamic; not cached into base)
  const float pitchSemis = pitchBendSemitones_ + pitchModSemitones_;
  // Use standard exp2f(2^x) for portability instead of fastpow2f
  const float pitchMul   = (pitchSemis == 0.0f) ? 1.0f : exp2f(pitchSemis * (1.0f/12.0f));

  // Fill cache: base + per-osc harmony (static) then dynamic multipliers and static detune
  pitchCache_.baseFreq = baseFreq;
  for (uint8_t i = 0; i < 3; ++i)
  {
    float hfreq = baseFreq;
    if (i < oscCount)
    {
      const int h = config.harmony[i];
      hfreq = (h == 0) ? baseFreq : calculateNoteFrequency(state.noteIndex, state.octaveOffset, h);
      const float f = hfreq * pitchMul * detuneMul[i];
      pitchCache_.harmonyFreq[i] = hfreq;
      pitchCache_.finalFreq[i]   = f;
    }
    else
    {
      pitchCache_.harmonyFreq[i] = 0.0f;
      pitchCache_.finalFreq[i]   = 0.0f;
    }
  }

  // Update snapshot
  pitchSnapshot_.noteIndex     = state.noteIndex;
  pitchSnapshot_.octaveOffset  = state.octaveOffset;
  pitchSnapshot_.oscCount      = oscCount;
  pitchSnapshot_.hasSlide      = state.hasSlide;
  pitchSnapshot_.detuneVersion = detuneVersion_;
  pitchSnapshot_.srVersion     = daisysp::Oscillator::GetSampleRateVersion();
  pitchSnapshot_.bendSemis     = pitchBendSemitones_;
  pitchSnapshot_.modSemis      = pitchModSemitones_;
  for (uint8_t i = 0; i < 3; ++i) { pitchSnapshot_.harmony[i] = config.harmony[i]; }

  // Bump generation after fully writing cache/snapshot (seq_cst)
  pitchGen_.fetch_add(1u, std::memory_order_seq_cst);
}

// Public API
void Voice::setPitchBend(float semitones)
{
  pitchBendSemitones_ = semitones;
  // Dynamic change; do not mark base dirty. Recompute final frequencies only.
  markPitchDirty();
}

void Voice::setModulationDepth(float semitones)
{
  pitchModSemitones_ = semitones;
  // Dynamic change; do not mark base dirty. Recompute final frequencies only.
  markPitchDirty();
}

void Voice::markPitchDirty()
{
  // Force recompute now; audio thread will commit via generation counter.
  updatePitchCache_();
}

void Voice::updateFrequencyIfNeeded()
{
  const bool changed = pitchParamsChanged_(state);
  if (!changed)
    return;
  updatePitchCache_();
}

float Voice::getCachedFrequency(uint8_t oscIndex) const
{
  const uint8_t oscCount = cachedOscCount_;
  if (oscIndex >= oscCount) return 0.0f;
  return pitchCache_.finalFreq[oscIndex];
}

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


// Apply staged VoiceState updates from control thread (UI/Sequencer)
void Voice::applyPendingParams_() noexcept
{
  const uint32_t gen = paramsGen_.load(std::memory_order_seq_cst);
  if (gen != appliedParamsGen_)
  {
    // Copy staged state and apply changes that require immediate DSP updates
    state = stagedState_;

    // Synchronize ADSR gate
    setGate(state.isGateHigh);

    // Recompute filter base freq from normalized param
    filterFrequency = daisysp::fmap(state.filterCutoff, 150.0f, 8000.0f, daisysp::Mapping::EXP);

    // Update envelope segment times
    applyEnvelopeParameters();

    // Stage pitch recompute; audio thread will commit oscillator freq via mixOscillators
    updateFrequencyIfNeeded();

    appliedParamsGen_ = gen;
  }
}

// Apply staged VoiceConfig changes from control thread safely on audio thread
void Voice::applyPendingConfig_() noexcept
{
  if (configPending_.load(std::memory_order_acquire))
  {
    config = stagedConfig_;

    // Oscillator container pre-sized to 3 in ctor; only use up to config.oscillatorCount
    cachedOscCount_ =  config.oscillatorCount;
    for (size_t i = 0; i < cachedOscCount_; ++i)
    {
      // Re-init to ensure SR/version updates if needed
      oscillators[i].Init(sampleRate);
    }
    // Zero-out any unused oscillators to ensure silence and deterministic state
    for (size_t i = cachedOscCount_; i < 3; ++i)
    {
      oscillators[i].Init(sampleRate);
      oscillators[i].SetAmp(0.0f);
    }

    // Update oscillator params only for active oscillators
    for (size_t i = 0; i < cachedOscCount_; i++)
    {
      oscillators[i].SetWaveform(config.oscWaveforms[i]);
      oscillators[i].SetAmp(config.oscAmplitudes[i]);
      if (config.oscWaveforms[i] == daisysp::Oscillator::WAVE_POLYBLEP_SQUARE)
      {
        oscillators[i].SetPw(config.oscPulseWidth[i]);
      }
    }

    // Update filters
    filter.SetRes(config.filterRes);
    filter.SetInputDrive(config.filterDrive);
    filter.SetPassbandGain(config.filterPassbandGain);
    filter.SetFilterMode(config.filterMode);

    highPassFilter.SetFreq(config.highPassFreq);
    highPassFilter.SetRes(config.highPassRes);
    hpfBypass_ = (config.highPassFreq <= 20.0f && config.highPassRes <= 0.01f);

    // Update effects
    overdrive.SetDrive(config.overdriveDrive);
    wavefolder.SetGain(config.wavefolderGain);
    wavefolder.SetOffset(config.wavefolderOffset);

    // Smoothly transition wavefolder mix if toggled
    wavefolderMixTarget = config.hasWavefolder ? 1.0f : 0.0f;

    // Detune multipliers depend on config
    recomputeDetuneMultipliers();

    // Pitch depends on harmony, etc.
    updateFrequencyIfNeeded();

    configPending_.store(false, std::memory_order_release);
  }
}
