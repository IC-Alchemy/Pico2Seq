#include "Voice.h"
#include "../utils/DspMapping.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include "../pico2seq-core/scales/scales.h" // Use centralized SCALES_COUNT / SCALE_STEPS
#include "VoicePresets.h"

// Constants
static constexpr float FREQ_SLEW_RATE = 0.00035f; // Slide speed
static constexpr float BASE_FREQ =
    110.0f; // Base frequency for note calculations

// The injected scale rows are int[48]; keep the centralized constant in sync.
static_assert(SCALE_STEPS == 48, "Voice expects 48-step scale rows");

// Thread-safe one-time init guard for frequency table
namespace
{
  static std::once_flag g_freqTableOnce;

  // The former Velocity lane is centered at 0.5 for hard-sync presets.
  // Around that center it offsets the slave by +/- 24 semitones from the
  // master, giving an untouched/default track an exact 1:1 sync ratio.
  constexpr float kHardSyncSlaveOffsetRangeSemitones = 24.0f;

  rpdsp::LadderFilter::Mode ladderModeFromVoiceMode(VoiceFilterMode mode) noexcept
  {
    switch (mode)
    {
    case VoiceFilterMode::LP12:
      return rpdsp::LadderFilter::Mode::LP12;
    case VoiceFilterMode::BP24:
      return rpdsp::LadderFilter::Mode::BP24;
    case VoiceFilterMode::BP12:
      return rpdsp::LadderFilter::Mode::BP12;
    case VoiceFilterMode::HP24:
      return rpdsp::LadderFilter::Mode::HP24;
    case VoiceFilterMode::HP12:
      return rpdsp::LadderFilter::Mode::HP12;
    case VoiceFilterMode::LP24:
    default:
      return rpdsp::LadderFilter::Mode::LP24;
    }
  }
}

// Static member initialization
float Voice::frequencyLookupTable[128];
bool Voice::lookupTableInitialized = false;

// Initialize frequency lookup table covering MIDI 0..127
inline void Voice::initFrequencyLookupTable() noexcept
{
  std::call_once(g_freqTableOnce, []() noexcept
                 {
    // Use rpdsp::midiNoteToHz once per MIDI note value
    for (int midi = 0; midi < 128; ++midi)
    {
      frequencyLookupTable[midi] = rpdsp::midiNoteToHz(static_cast<float>(midi));
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

  // Oscillator slots are fixed-size members; nothing to allocate.

  // Initialize frequency slewing
  for (int i = 0; i < 3; i++)
  {
    freqSlew[i].currentFreq = 440.0f;
    freqSlew[i].targetFreq = 440.0f;
  }

  // Initialize voice state with defaults
  state.noteIndex = 0.0f;
  state.velocityLevel = 0.5f;
  state.filterCutoff = 0.37f;
  state.attackTimeSeconds = 0.01f;
  state.decayTimeSeconds = 0.1f;
  state.octaveOffset = 0;
  state.gateLengthTicks = 12; // Default gate length
  state.isGateHigh = false;
  state.hasSlide = false;
  state.shouldRetrigger = false;
  controls_.config = cfg;
  controls_.state = state;
}

void Voice::init(float sr)
{
  // Setup only: neither core may access this Voice concurrently with init().
  // Fold any pre-init setters into the initial state without running DSP early.
  ControlUpdate unused;
  while (controlQueue_.tryPop(unused)) {}
  controls_.scaleIndex = currentScalePtr_ ? *currentScalePtr_ : 0;
  controls_.changes = 0;
  config = controls_.config;
  state = controls_.state;
  gate = state.isGateHigh;
  scaleTable = controls_.scaleTable;
  scaleTableCount = controls_.scaleCount;
  audioScaleIndex_ = controls_.scaleIndex;
  slideTimeSeconds = controls_.slideSeconds;
  pitchBendSemitones_ = controls_.bendSemitones;
  pitchModSemitones_ = controls_.modulationSemitones;
  filterFrequency = controls_.filterHz;
  sampleRate = sr;
  // Changing sample rate can affect tuning in downstream modules; ensure base frequency recompute
  baseFreqDirty_ = true;

  // Compute per-sample slide coefficient from time constant
  slideAlpha = makeSmoothingAlpha(slideTimeSeconds, sampleRate);

  // Initialize oscillators
  cachedOscCount_ = static_cast<uint8_t>(std::min<size_t>(3, config.oscillatorCount));
  for (size_t i = 0; i < cachedOscCount_; i++)
  {
    oscillators[i].prepare(sampleRate);
    oscillators[i].setWaveform(config.oscWaveforms[i]);
    // Ignored by non-pulse waveforms; keeps square/pulse pulse width in sync
    oscillators[i].setPulseWidth(config.oscPulseWidth[i]);
  }

  // Initialize noise generator (distinct seed per voice so percussion voices differ)
  noise_.reseed(0x12345678u + static_cast<uint32_t>(voiceId) + 1u);

  // Initialize filter
  filter.prepare(sampleRate);
  filter.setFreq(filterFrequency);
  filter.setRes(config.filterRes);
  filter.setInputDrive(config.filterDrive);
  filter.setPassbandGain(config.filterPassbandGain);
  filter.setMode(ladderModeFromVoiceMode(config.filterMode));
  // Alternate main-filter topology: kept prepared even when unused so a live
  // config swap can switch filterType without re-preparing mid-gate.
  filterSvf_.prepare(sampleRate);
  filterSvf_.setCutoff(filterFrequency);
  configureMainFilterFromConfig_();
  // Initialize high-pass filter
  highPassFilter.prepare(sampleRate);
  highPassFilter.setCutoff(config.highPassFreq);
  highPassFilter.setResonance(config.highPassRes);
  hpfBypass_ = (config.highPassFreq <= 20.0f && config.highPassRes <= 0.01f);
  // Initialize filter cutoff smoothing state (reduces zipper noise from abrupt setFreq calls).
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
  envelope.prepare(sampleRate);
  applyEnvelopeDefaults_();
  gateHighPrev_ = false;

  // Initialize effects
  overdrive.setDrive(1.0f + (config.overdriveDrive * 3.0f)); // map 0-1 drive to 1-4
  overdrive.setOutputGain(1.0f);

  // Alternate engines: prepare + seed state, then apply tuning from config.
  // init() is setup-time, so the engine cache is set directly here.
  cachedEngine_ = (config.engine <= static_cast<uint8_t>(ENGINE_RECIPE))
                      ? config.engine
                      : static_cast<uint8_t>(ENGINE_OSC);
  recipeEngine_.prepare(sampleRate);
  recipeEngine_.select(config.recipe);
  waveguide_.prepare(sampleRate);
  hypersaw_.prepare(sampleRate);
  hypersaw_.reseed(0x9e3779b9u + static_cast<uint32_t>(voiceId) + 1u);
  resetAlternateEngines_();
  applyEngineConfig_();

  // Populate the pitch cache unconditionally so alternate engines (which read
  // baseFreq even without an oscillator bank) start from a valid frequency.
  updatePitchCache_();

  // Update detune multipliers in case config changed before init
  recomputeDetuneMultipliers();
}

void Voice::setConfig(const VoiceConfig &cfg)
{
  controls_.config = cfg;
  controls_.changes |= ConfigChanged;
  flushControlUpdates();
}

void Voice::setScaleTable(const int (*table)[48], size_t scaleCount)
{
  controls_.scaleTable = table;
  controls_.scaleCount = scaleCount;
  controls_.changes |= ScaleChanged;
  flushControlUpdates();
}

void Voice::setCurrentScalePointer(const uint8_t *ptr)
{
  currentScalePtr_ = ptr; // dereferenced by the control thread only
  controls_.changes |= ScaleChanged;
  flushControlUpdates();
}

bool Voice::flushControlUpdates() noexcept
{
  const size_t scaleIndex = currentScalePtr_ ? *currentScalePtr_ : 0;
  if (scaleIndex != controls_.scaleIndex)
  {
    controls_.scaleIndex = scaleIndex;
    controls_.changes |= ScaleChanged;
  }
  if (controls_.changes == 0)
    return true;
  if (!controlQueue_.tryPush(controls_))
    return false; // keep the producer-owned update for the next control pass
  controls_.changes = 0;
  controls_.state.shouldRetrigger = false; // event belongs to the published copy
  return true;
}

void Voice::setGate(bool gateState)
{
  controls_.state.isGateHigh = gateState;
  if (!gateState)
    controls_.state.shouldRetrigger = false;
  controls_.changes |= GateChanged;
  flushControlUpdates();
}

void Voice::setFilterFrequency(float frequency)
{
  controls_.filterHz = frequency;
  controls_.changes |= FilterChanged;
  flushControlUpdates();
}

void Voice::setEnabled(bool enabled)
{
  controls_.config.enabled = enabled;
  controls_.changes |= ConfigChanged;
  flushControlUpdates();
}

void Voice::applyControlUpdate_() noexcept
{
  ControlUpdate update;
  if (!controlQueue_.tryPop(update))
    return;

  // Work is bounded to one update per sample. FIFO gate changes therefore
  // each reach envelope processing, even when several arrived between samples.
  const uint32_t changes = update.changes;
  if (changes & ScaleChanged)
  {
    scaleTable = update.scaleTable;
    scaleTableCount = update.scaleCount;
    audioScaleIndex_ = update.scaleIndex;
    baseFreqDirty_ = true;
  }
  if (changes & SlideChanged)
  {
    slideTimeSeconds = update.slideSeconds;
    slideAlpha = makeSmoothingAlpha(slideTimeSeconds, sampleRate);
  }
  if (changes & BendChanged)
    pitchBendSemitones_ = update.bendSemitones;
  if (changes & ModulationChanged)
    pitchModSemitones_ = update.modulationSemitones;
  if (changes & ConfigChanged)
    applyConfig_(update.config);
  if (changes & ParametersChanged)
    applyParameters_(update.state);
  else if (changes & GateChanged)
  {
    gate = update.state.isGateHigh;
    state.isGateHigh = gate;
    state.shouldRetrigger = false;
  }
  if (changes & (ScaleChanged | BendChanged | ModulationChanged | PitchRefresh))
    updatePitchCache_();
  if (changes & FrequencyChanged)
    applyFrequency_(update.frequency);
  if (changes & FilterChanged)
    filterFrequency = update.filterHz;
}

float Voice::process() noexcept
{
  // Consume controls even while disabled, so queued re-enables can take effect.
  applyControlUpdate_();
  if (!config.enabled)
    return 0.0f;

  // 1) Envelope
  float envelopeValue = computeEnvelope();
  // Cache envelope value for cheap per-voice checks in hot paths (avoids reprocessing ADSR)
  lastEnvelopeValue = envelopeValue;

  // Apply a staged structural config as soon as the gate allows it (nothing
  // sounding), so live preset swaps never click a held note or cut a tail.
  if (structuralPending_ && !gate)
  {
    applyStructuralConfig_();
  }

  // 2) Filter cutoff update (uses envelope)
  if (config.hasFilter)
  {
    updateFilter(envelopeValue);
  }

  // 3) Oscillator/engine mixing (+ slide updates)
  float mixed = mixOscillators();

  // 4) Effects and gain shaping pre-filter

  // 5) Filter processing, HPF, and final scaling
  return finalizeOutput(mixed, envelopeValue);
}

float Voice::computeEnvelope()
{
  // Track gate edges for the event-style ADSR (noteOn on rise, noteOff on fall).
  // This must happen even when the ADSR is bypassed: hasEnvelope == false means
  // "ring naturally" (waveguide pluck still arms on edges), not "ignore gates".
  const bool gateHigh = gate;
  const bool rising = gateHigh && !gateHighPrev_;
  const bool falling = !gateHigh && gateHighPrev_;
  gateHighPrev_ = gateHigh;

  // Retrigger restarts the attack from zero, matching the old soft-retrigger
  // behavior while gated. Consumed even when ungated so a stale flag cannot
  // arm a surprise attack later.
  if (state.shouldRetrigger)
  {
    state.shouldRetrigger = false;
    if (gateHigh)
    {
      if (config.hasEnvelope)
        envelope.noteOn();
      wgPluckPending_ = true; // waveguide engine re-plucks on retriggers
      hypersawTriggerPending_ = true;
      recipeTriggerPending_ = true;
    }
  }
  else if (rising)
  {
    if (config.hasEnvelope)
      envelope.noteOn();
    wgPluckPending_ = true;
    hypersawTriggerPending_ = true;
    recipeTriggerPending_ = true;
  }
  else if (falling)
  {
    if (config.hasEnvelope)
      envelope.noteOff();
  }

  if (!config.hasEnvelope)
  {
    return 1.0f;
  }

  return envelope.process();
}

void Voice::updateFilter(float envelopeValue)
{
  // Compute the intended (instantaneous) cutoff target using previous logic
  const float targetCutoff = (filterFrequency * envelopeValue) + (filterFrequency * 0.1f);

  // Exponential smoothing to prevent zipper noise when targetCutoff jumps.
  // filterCutoffAlpha was initialized in init() (per-sample coefficient).
  filterCutoffCurrent += filterCutoffAlpha * (targetCutoff - filterCutoffCurrent);

  // Throttle setFreq to avoid per-sample work if change is tiny (setFreq is
  // polynomial in rpdsp, but the throttle also caps coefficient churn)
  if (filterUpdateCounter == 0)
  {
    if (ShouldApplyFilterFreq_(filterCutoffCurrent, lastAppliedFilterCutoff))
    {
      if (config.filterType == FILTER_SVF)
        filterSvf_.setCutoff(filterCutoffCurrent);
      else
        filter.setFreq(filterCutoffCurrent);
      lastAppliedFilterCutoff = filterCutoffCurrent;
    }
  }
  // Mask wrap: kFilterUpdateInterval is a power of two (static_assert in Voice.h)
  filterUpdateCounter = static_cast<uint8_t>((filterUpdateCounter + 1) & (kFilterUpdateInterval - 1));
}

void Voice::configureMainFilterFromConfig_() noexcept
{
  filterSvf_.setResonance(config.filterRes);
  switch (config.filterMode)
  {
  case VoiceFilterMode::BP24:
  case VoiceFilterMode::BP12:
    svfOutputSel_ = 1;
    break;
  case VoiceFilterMode::HP24:
  case VoiceFilterMode::HP12:
    svfOutputSel_ = 2;
    break;
  default:
    svfOutputSel_ = 0;
    break;
  }
}

float Voice::mixOscillators()
{
  float mixedOscillators = 0.0f;

  // Very cheap per-voice silence short-circuit: if envelope is enabled and the cached
  // envelope value is effectively zero, skip oscillator/noise processing entirely.
  if (config.hasEnvelope && lastEnvelopeValue <= 0.001f)
  {
    return 0.0f;
  }

  // Alternate engines replace the oscillator bank entirely; the shared
  // envelope -> filter -> output chain still runs in finalizeOutput().
  if (cachedEngine_ == static_cast<uint8_t>(ENGINE_WAVEGUIDE))
  {
    return processWaveguide_();
  }
  if (cachedEngine_ == ENGINE_HYPERSAW || cachedEngine_ == ENGINE_RECIPE)
  {
    return processPitchedEngine_();
  }
  if (cachedEngine_ == static_cast<uint8_t>(ENGINE_NOISEFX))
  {
    return processNoiseFxSource_();
  }

  // Determine number of oscillators to process (max 3 pre-sized)
  const size_t oscCount = cachedOscCount_;

  // Audio-thread commit of frequency changes:
  // - Only when gate HIGH (no repitch during release)
  // - Audio-local cache version prevents redundant commits
  if (oscCount > 0 && state.isGateHigh)
  {
    const uint32_t gen = pitchGen_;
    if (!state.hasSlide && gen != appliedPitchGen_)
    {
      // Commit immediate frequencies (no slide)
      for (size_t i = 0; i < oscCount; i++)
      {
        const float f = pitchCache_.finalFreq[i];
        if (ShouldApplyFreq_(f, lastAppliedOscFreq_[i]))
        {
          oscillators[i].setFreq(f);
          lastAppliedOscFreq_[i] = f;
          // Keep slew state consistent
          freqSlew[i].currentFreq = f;
          freqSlew[i].targetFreq = f;
        }
        if (config.oscWaveforms[i] == WAVE_HARDSYNC_SAW)
        {
          oscillators[i].setSlaveFrequency(pitchCache_.slaveFreq[i]);
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
    // Update frequencies (slew when sliding) and process oscillators.
    // rpdsp oscillators have no amp parameter, so oscAmplitudes[] scales at mix time.
    for (size_t i = 0; i < oscCount; i++)
    {
      if (state.hasSlide)
      {
        processFrequencySlew(i, freqSlew[i].targetFreq);
        const float fcur = freqSlew[i].currentFreq;
        if (ShouldApplyFreq_(fcur, lastAppliedOscFreq_[i]))
        {
          oscillators[i].setFreq(fcur);
          lastAppliedOscFreq_[i] = fcur;
        }
        if (config.oscWaveforms[i] == WAVE_HARDSYNC_SAW)
        {
          const float targetMaster = pitchCache_.finalFreq[i];
          const float slaveRatio = (targetMaster > 0.0f)
                                       ? (pitchCache_.slaveFreq[i] / targetMaster)
                                       : 1.0f;
          oscillators[i].setSlaveFrequency(fcur * slaveRatio);
        }
      }
      mixedOscillators += oscillators[i].process() * config.oscAmplitudes[i];
    }
  }
  else
  {
    // Special case for percussion voices (no oscillators, only noise)
    mixedOscillators = noise_.process();
  }

  return mixedOscillators;
}

void Voice::applyEffects(float &signal)
{
  if (config.hasOverdrive)
  {
    signal = overdrive.process(signal * config.overdriveGain);
  }

  // Noise-FX engine inserts: prime-tap diffusion smears the noise into
  // ambience, then the regenerative allpass swarm blooms. Both run pre-filter
  // so the envelope-scaled ladder shapes the resulting texture.
  if (cachedEngine_ == static_cast<uint8_t>(ENGINE_NOISEFX))
  {
    signal = rpdsp::fx_diffuse(signal, noiseDiffuseBuf_.data(), kNoiseFxBufferSize,
                               config.noiseDiffuseSize, config.noiseDiffuseMix,
                               noiseDiffuseState_);
    signal = rpdsp::fx_swarm(signal, config.noiseSwarmColor,
                             config.noiseSwarmRegen, noiseSwarmState_);
  }

  // Level adjustments removed from here; handled in finalizeOutput
}

// Provide a wrapper to maintain API compatibility
void Voice::processEffectsChain(float &signal)
{
  applyEffects(signal);
}

// -------- Alternate engines (waveguide / Hypersaw / noise-FX) --------

void Voice::applyEngineConfig_()
{
  // Waveguide tuning. These are control-rate setters with internal clamps;
  // setBrightness()/setPickHardness() derive coefficients from sampleRate_,
  // so waveguide_.prepare() must have run first (init() guarantees this).
  // NOTE: cachedEngine_ is NOT updated here — the engine switch belongs to
  // applyStructuralConfig_() so a live swap waits for the gate to fall.
  if (cachedEngine_ == ENGINE_WAVEGUIDE) {
    waveguide_.setDecayTimeSeconds(config.wgT60);
    waveguide_.setBrightness(config.wgBrightness);
    waveguide_.setPickPosition(config.wgPickPosition);
    waveguide_.setPickHardness(config.wgPickHardness);
    waveguide_.setStiffness(config.wgStiffness);
    waveguide_.setDetuneCents(config.wgDetune);
  } else if (cachedEngine_ == ENGINE_HYPERSAW) {
    hypersaw_.setDetune(config.hypersawDetune);
    hypersaw_.setMix(config.hypersawMix);
  } else if (cachedEngine_ == ENGINE_RECIPE) {
    recipeEngine_.configure(config);
  }

}

float Voice::processWaveguide_() noexcept
{
  if (wgPluckPending_)
  {
    wgPluckPending_ = false;
    // With an oscillator bank configured, honor its harmony/detune on the
    // first oscillator; otherwise use the plain base pitch.
    const float targetHz = (cachedOscCount_ > 0) ? pitchCache_.finalFreq[0]
                                                 : pitchCache_.baseFreq;
    if (targetHz > 0.0f)
    {
      // Velocity scales downstream at the filter input, so the string always
      // plucks at full level.
      waveguide_.pluck(targetHz, 1.0f);
    }
  }
  return waveguide_.process();
}

float Voice::processPitchedEngine_() noexcept
{
  // Native Hypersaw and recipe patches share one pitch input, including
  // harmony, pitch bend, octave and slide, with an empty oscillator bank.
  if (state.isGateHigh)
  {
    const uint32_t gen = pitchGen_;
    if (!state.hasSlide && gen != engineAppliedPitchGen_)
    {
      const float frequency = pitchCache_.finalFreq[0];
      if (frequency > 0.0f)
      {
        if (cachedEngine_ == ENGINE_HYPERSAW) hypersaw_.setFreq(frequency);
        freqSlew[0].currentFreq = frequency;
        freqSlew[0].targetFreq = frequency;
      }
      engineAppliedPitchGen_ = gen;
    }
    else if (state.hasSlide && gen != engineAppliedPitchGen_)
    {
      freqSlew[0].targetFreq = pitchCache_.finalFreq[0];
      engineAppliedPitchGen_ = gen;
    }

    if (state.hasSlide)
    {
      processFrequencySlew(0, freqSlew[0].targetFreq);
      if (cachedEngine_ == ENGINE_HYPERSAW) hypersaw_.setFreq(freqSlew[0].currentFreq);
    }
  }

  if (cachedEngine_ == ENGINE_RECIPE)
  {
    if (recipeTriggerPending_) { recipeTriggerPending_ = false; recipeEngine_.trigger(config); }
    return recipeEngine_.process(freqSlew[0].currentFreq, config);
  }

  if (hypersawTriggerPending_)
  {
    hypersawTriggerPending_ = false;
    hypersaw_.trigger();
  }
  return hypersaw_.process();
}

float Voice::processNoiseFxSource_() noexcept
{
  float source = noise_.process();
  if (config.noiseChaosLevel > 0.001f)
  {
    // Lorenz rate follows the base pitch so the growl tracks the sequence
    // instead of sitting at one fixed register (0.0001 slow CV .. 0.02 growl).
    const float baseHz = (pitchCache_.baseFreq > 0.0f) ? pitchCache_.baseFreq : 110.0f;
    const float rate = std::clamp(baseHz / sampleRate, 1.0e-4f, 0.02f);
    source += rpdsp::chaos_lorenz(rate, noiseChaosState_) * config.noiseChaosLevel * 0.5f;
  }
  return source;
}

void Voice::resetAlternateEngines_() noexcept
{
  recipeEngine_.reset();
  recipeTriggerPending_ = false;
  waveguide_.reset();
  wgPluckPending_ = false;
  hypersaw_.reset();
  hypersawTriggerPending_ = false;
  engineAppliedPitchGen_ = 0;
  noiseDiffuseState_[0] = 0.0f;
  for (float &s : noiseSwarmState_)
    s = 0.0f;
  for (float &s : noiseChaosState_)
    s = 0.0f;
  noiseDiffuseBuf_.fill(0.0f);
}

inline float Voice::finalizeOutput(float signal, float envelopeValue) noexcept
{
  float preEffects = signal * envelopeValue;
  // Apply effects (pre-filter)
  //    VCA envelope is applied pre effects so that the overdrive sounds more dynamic
  applyEffects(preEffects);

  // With the main filter bypassed, velocity scales the signal directly —
  // the filter input was its only entry point.
  // Hard-sync presets re-purpose Velocity as their slave-frequency lane, so
  // it must not also change the VCA level. Their fixed outputLevel remains
  // the gain control; all other voices keep the normal velocity response.
  const float amplitude = VoiceParameters::layout(config).velocityToAmplitude ? state.velocityLevel : 1.0f;
  const float filterInput = preEffects * amplitude;
  float shaped;
  if (!config.hasFilter)
  {
    shaped = filterInput;
  }
  else if (config.filterType == FILTER_SVF)
  {
    const rpdsp::StateVariableOutput svf = filterSvf_.process(filterInput);
    shaped = (svfOutputSel_ == 1)   ? svf.bandpass
             : (svfOutputSel_ == 2) ? svf.highpass
                                    : svf.lowpass;
  }
  else
  {
    shaped = filter.process(filterInput);
  }

  // Apply optional high-pass filter
  float postHpf = shaped;
  if (!hpfBypass_)
  {
    postHpf = highPassFilter.process(shaped).highpass;
  }

  float finalOutput = postHpf * config.outputLevel;

  return finalOutput;
}

void Voice::updateOscillatorFrequencies()
{
  // Deprecated path for direct control-thread commits; retained for backward compatibility.
  // Both cache refresh and oscillator commits remain on the audio thread.
  refreshPitch_();
}

inline void Voice::applyEnvelopeParameters() noexcept
{
  // Map normalized parameters to appropriate ranges
  float attack =
      dspmap::fmap(state.attackTimeSeconds, 0.002f, 0.75f, dspmap::Mapping::LINEAR);
  float decay =
      dspmap::fmap(state.decayTimeSeconds, 0.01f, 0.5f, dspmap::Mapping::LOG);
  // float release = decay; // Use decay for release in this implementation

  envelope.setAttack(attack);
  envelope.setDecay(0.075f + (decay * 0.32f));
  envelope.setRelease(decay);
}

inline void Voice::applyEnvelopeDefaults_() noexcept
{
  envelope.setAttack(config.defaultAttack);
  envelope.setDecay(config.defaultDecay);
  envelope.setSustain(config.defaultSustain);
  envelope.setRelease(config.defaultRelease);
}

size_t Voice::effectiveScaleIndex_() const noexcept
{
  if (scaleTable == nullptr || scaleTableCount == 0)
    return 0;
  const size_t idx = audioScaleIndex_;
  return (idx >= scaleTableCount) ? scaleTableCount - 1 : idx;
}

inline float Voice::calculateNoteFrequency(float note, int8_t octaveOffset,
                                           int harmony) noexcept
{
  // Keep note+harmony inside the 48-step scale row even with extreme values.
  int noteWithHarmony = static_cast<int>(note) + harmony;
  if (noteWithHarmony < 0)
    noteWithHarmony = 0;
  if (noteWithHarmony >= static_cast<int>(SCALE_STEPS))
    noteWithHarmony = static_cast<int>(SCALE_STEPS) - 1;

  // Single lookup path: the injected table when present, otherwise chromatic
  // mapping (each scale step is one semitone above C3).
  int scaleSemitone;
  if (scaleTable != nullptr && scaleTableCount > 0)
  {
    scaleSemitone = scaleTable[effectiveScaleIndex_()][noteWithHarmony];
  }
  else
  {
    scaleSemitone = noteWithHarmony;
  }

  // Map to MIDI centered at 48 (C3) and saturate so the octave offset can
  // never index past the 128-entry frequency lookup table.
  int midiNote = scaleSemitone + 48 + static_cast<int>(octaveOffset);
  if (midiNote < 0)
    midiNote = 0;
  if (midiNote > 127)
    midiNote = 127;

  return frequencyLookupTable[midiNote];
}

void Voice::checkScaleIndexChanged_() noexcept
{
  if (pitchSnapshot_.scaleIndex != effectiveScaleIndex_())
    baseFreqDirty_ = true;
}

// Recompute cached base frequency (static pitch only). Includes ONLY static contributors (note, octave/transpose,
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
  controls_.frequency = frequency;
  controls_.changes |= FrequencyChanged;
  flushControlUpdates();
}

void Voice::applyFrequency_(float frequency)
{
  // Explicit Hz requests use the same gate/slide commit path as sequenced pitch.
  pitchCache_.baseFreq = frequency;
  for (uint8_t i = 0; i < 3; ++i)
  {
    const float previous = pitchCache_.finalFreq[i];
    const float ratio = previous > 0.0f ? pitchCache_.slaveFreq[i] / previous : 1.0f;
    pitchCache_.finalFreq[i] = frequency * detuneMul[i];
    pitchCache_.slaveFreq[i] = pitchCache_.finalFreq[i] * ratio;
  }
  ++pitchGen_;
}

void Voice::setSlideTime(float slideTime)
{
  controls_.slideSeconds = std::clamp(slideTime, 0.0f, 10.0f);
  controls_.changes |= SlideChanged;
  flushControlUpdates();
}

void Voice::updateParameters(const VoiceState &newState)
{
  // Only unpublished updates can coalesce. Preserve a pending retrigger while
  // its gate stays high; a later gate-off always wins during overload.
  const bool pendingRetrigger = (controls_.changes & ParametersChanged) &&
                                controls_.state.shouldRetrigger;
  controls_.state = newState;
  controls_.state.shouldRetrigger = newState.isGateHigh &&
                                   (newState.shouldRetrigger || pendingRetrigger);
  controls_.changes |= ParametersChanged;
  flushControlUpdates();
}

// Voice Presets moved to src/voice/VoicePresets.cpp

// -------- Pitch optimization: change detection, cache, and API --------
// Fields watched: noteIndex, octaveOffset, harmony[0..oscCount-1], oscCount, detuneVersion_,
// hasSlide, pitch bend/mod in semitones.
// Audio-local versioning: recompute the cache and bump pitchGen_;
// audio thread commits in mixOscillators() when pitchGen_ != appliedPitchGen_.
// setFreq gating: ShouldApplyFreq_ uses kPitchRelEps and kPitchAbsEpsHz (≈0.017 cent minimum)
// to cut redundant oscillator.setFreq calls, including during slide slews.

// Compare current/new state & dependencies to snapshot to decide if recompute needed.
// Note: also watches detuneVersion_.
bool Voice::pitchParamsChanged_(const VoiceState &newState) const
{
  const uint8_t oscCount = cachedOscCount_;
  const bool usesPitchedEngine = cachedEngine_ == ENGINE_HYPERSAW || cachedEngine_ == ENGINE_RECIPE;
  bool usesHardSync = false;
  for (uint8_t i = 0; i < oscCount; ++i)
  {
    usesHardSync |= config.oscWaveforms[i] == WAVE_HARDSYNC_SAW;
  }
  if (pitchSnapshot_.oscCount != oscCount)
    return true;
  if (pitchSnapshot_.usesPitchedEngine != usesPitchedEngine)
    return true;
  if (pitchSnapshot_.usesHardSync != usesHardSync)
    return true;
  if (usesHardSync && pitchSnapshot_.hardSyncSlaveControl != newState.velocityLevel)
    return true;
  if (pitchSnapshot_.noteIndex != newState.noteIndex)
    return true;
  if (pitchSnapshot_.octaveOffset != newState.octaveOffset)
    return true;
  // A queued scale switch must refresh pitch
  // even when the note itself is unchanged.
  if (pitchSnapshot_.scaleIndex != effectiveScaleIndex_())
    return true;
  if (pitchSnapshot_.hasSlide != newState.hasSlide)
    return true;
  // Harmony
  for (uint8_t i = 0; i < oscCount; ++i)
  {
    if (pitchSnapshot_.harmony[i] != config.harmony[i])
      return true;
  }
  // Detune version
  if (pitchSnapshot_.detuneVersion != detuneVersion_)
    return true;
  // Pitch bend/mod snapshots
  if (pitchSnapshot_.bendSemis != pitchBendSemitones_)
    return true;
  if (pitchSnapshot_.modSemis != pitchModSemitones_)
    return true;

  return false;
}

// Recompute pitch cache using current state/config and pitch controls.
// Writes pitchCache_ then bumps generation.
void Voice::updatePitchCache_()
{
  const uint8_t oscCount = cachedOscCount_;
  const bool usesPitchedEngine = cachedEngine_ == ENGINE_HYPERSAW || cachedEngine_ == ENGINE_RECIPE;
  const float hardSyncSlaveOffsetSemitones =
      (std::clamp(state.velocityLevel, 0.0f, 1.0f) - 0.5f) *
      (2.0f * kHardSyncSlaveOffsetRangeSemitones);

  // A runtime scale switch invalidates the static base before it is consulted.
  checkScaleIndexChanged_();

  // Ensure static base is up to date; avoids redoing static work when only dynamics change.
  recomputeBaseFreqIfDirty_();
  const float baseFreq = cachedBaseFreqHz_;

  // Combined dynamic pitch (bend + mod) in semitones (dynamic; not cached into base)
  const float pitchSemis = pitchBendSemitones_ + pitchModSemitones_;
  // Use standard exp2f(2^x) for portability instead of fastpow2f
  const float pitchMul = (pitchSemis == 0.0f) ? 1.0f : exp2f(pitchSemis * (1.0f / 12.0f));

  // Fill cache: base + per-osc harmony (static) then dynamic multipliers and static detune
  pitchCache_.baseFreq = baseFreq;
  for (uint8_t i = 0; i < 3; ++i)
  {
    float hfreq = baseFreq;
    if (i < oscCount || (usesPitchedEngine && i == 0))
    {
      const int h = config.harmony[i];
      hfreq = (h == 0) ? baseFreq : calculateNoteFrequency(state.noteIndex, state.octaveOffset, h);
      const float f = hfreq * pitchMul * detuneMul[i];
      pitchCache_.harmonyFreq[i] = hfreq;
      pitchCache_.finalFreq[i] = f;
      pitchCache_.slaveFreq[i] = (config.oscWaveforms[i] == WAVE_HARDSYNC_SAW)
                                     ? f * exp2f(hardSyncSlaveOffsetSemitones * (1.0f / 12.0f))
                                     : f;
    }
    else
    {
      pitchCache_.harmonyFreq[i] = 0.0f;
      pitchCache_.finalFreq[i] = 0.0f;
      pitchCache_.slaveFreq[i] = 0.0f;
    }
  }

  // Update snapshot
  pitchSnapshot_.noteIndex = state.noteIndex;
  pitchSnapshot_.octaveOffset = state.octaveOffset;
  pitchSnapshot_.scaleIndex = effectiveScaleIndex_();
  pitchSnapshot_.oscCount = oscCount;
  pitchSnapshot_.usesPitchedEngine = usesPitchedEngine;
  pitchSnapshot_.usesHardSync = false;
  for (uint8_t i = 0; i < oscCount; ++i)
  {
    pitchSnapshot_.usesHardSync |= config.oscWaveforms[i] == WAVE_HARDSYNC_SAW;
  }
  pitchSnapshot_.hardSyncSlaveControl = state.velocityLevel;
  pitchSnapshot_.hasSlide = state.hasSlide;
  pitchSnapshot_.detuneVersion = detuneVersion_;
  pitchSnapshot_.bendSemis = pitchBendSemitones_;
  pitchSnapshot_.modSemis = pitchModSemitones_;
  for (uint8_t i = 0; i < 3; ++i)
  {
    pitchSnapshot_.harmony[i] = config.harmony[i];
  }

  // Advance the audio-local cache version after recomputing pitch
  ++pitchGen_;
}

// Public API
void Voice::setPitchBend(float semitones)
{
  controls_.bendSemitones = semitones;
  controls_.changes |= BendChanged;
  flushControlUpdates();
}

void Voice::setModulationDepth(float semitones)
{
  controls_.modulationSemitones = semitones;
  controls_.changes |= ModulationChanged;
  flushControlUpdates();
}

void Voice::markPitchDirty()
{
  controls_.changes |= PitchRefresh;
  flushControlUpdates();
}

void Voice::updateFrequencyIfNeeded()
{
  markPitchDirty();
}

void Voice::refreshPitch_()
{
  if (baseFreqDirty_ || pitchParamsChanged_(state))
    updatePitchCache_();
}

float Voice::getCachedFrequency(uint8_t oscIndex) const
{
  const uint8_t oscCount = cachedOscCount_;
  if (oscIndex >= oscCount)
    return 0.0f;
  return pitchCache_.finalFreq[oscIndex];
}

float Voice::getCachedSlaveFrequency(uint8_t oscIndex) const
{
  const uint8_t oscCount = cachedOscCount_;
  if (oscIndex >= oscCount)
    return 0.0f;
  return pitchCache_.slaveFreq[oscIndex];
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

// Audio thread only: compare with its own previous state, then update DSP.
void Voice::applyParameters_(const VoiceState &newState) noexcept
{
  if (newState.noteIndex != state.noteIndex || newState.octaveOffset != state.octaveOffset)
    baseFreqDirty_ = true;
  state = newState;
  gate = state.isGateHigh;

  VoiceParameters::apply(config, state);
  const auto &parameters = VoiceParameters::layout(config);
  const bool repurposedFilter = VoiceParameters::binding(config, ParamId::Filter).target != nullptr;
  filterFrequency = dspmap::fmap(repurposedFilter ? config.filterCutoffBase : state.filterCutoff,
                                parameters.cutoffMinimum, parameters.cutoffMaximum, dspmap::Mapping::EXP);
  if (parameters.envelopeFromTracks)
    applyEnvelopeParameters();
  applyEngineConfig_();

  // Stage pitch recompute; audio thread will commit oscillator freq via mixOscillators
  refreshPitch_();
}

// Apply staged structural config (oscillator bank + engine) on the audio
// thread. Only called while the gate is low so the change never interrupts a
// sounding note.
void Voice::applyStructuralConfig_() noexcept
{
  cachedOscCount_ = stagedOscCount_;
  for (size_t i = 0; i < 3; ++i)
  {
    oscillators[i].prepare(sampleRate); // phase reset deferred off the playing note
    lastAppliedOscFreq_[i] = -1.0f;
    if (i < cachedOscCount_)
    {
      oscillators[i].setWaveform(stagedWaveforms_[i]);
      // Ignored by non-pulse waveforms
      oscillators[i].setPulseWidth(stagedPulseWidth_[i]);
    }
  }

  // Alternate engines: clear stale tails when switching engines.
  if (stagedEngine_ != cachedEngine_)
  {
    resetAlternateEngines_();
  }
  cachedEngine_ = stagedEngine_;
  recipeEngine_.select(config.recipe);
  applyEngineConfig_();
  // Every re-created oscillator needs its current cached pitch committed on
  // the next gate, even if no musical parameter changed during the swap.
  appliedPitchGen_ = 0;
  // The selected source can change the pitch-cache shape (Hypersaw needs
  // finalFreq[0] even though its regular oscillator count is zero).
  refreshPitch_();
  structuralPending_ = false;
}

// Audio thread only: queue slots have already been released after a local copy.
void Voice::applyConfig_(const VoiceConfig &newConfig) noexcept
{
  config = newConfig;

  // Update filters (scalar; safe mid-note)
  if (config.hasFilter)
  {
    filter.setRes(config.filterRes);
    filter.setInputDrive(config.filterDrive);
    filter.setPassbandGain(config.filterPassbandGain);
    filter.setMode(ladderModeFromVoiceMode(config.filterMode));
    configureMainFilterFromConfig_();
  }

  highPassFilter.setCutoff(config.highPassFreq);
  highPassFilter.setResonance(config.highPassRes);
  hpfBypass_ = (config.highPassFreq <= 20.0f && config.highPassRes <= 0.01f);

  // Update effects
  overdrive.setDrive(1.0f + (config.overdriveDrive * 3.0f)); // map 0-1 drive to 1-4

  // Stage the structural part (oscillator bank rebuild + engine switch).
  // Applied immediately only when nothing is sounding, so a live preset
  // swap is click-free.
  stagedOscCount_ = static_cast<uint8_t>(std::min<size_t>(3, config.oscillatorCount));
  for (size_t i = 0; i < 3; ++i)
  {
    stagedWaveforms_[i] = config.oscWaveforms[i];
    stagedPulseWidth_[i] = config.oscPulseWidth[i];
  }
  stagedEngine_ = (config.engine <= static_cast<uint8_t>(ENGINE_RECIPE))
                      ? config.engine
                      : static_cast<uint8_t>(ENGINE_OSC);
  structuralPending_ = true;
  if (!gate)
  {
    applyStructuralConfig_();
  }

  // Engine tuning (waveguide/Hypersaw/noise scalars) is safe to apply mid-note
  applyEngineConfig_();

  // Envelope segment times: for standard voices the next staged state
  // re-maps them; for re-purposed slots the preset defaults define the shape.
  applyEnvelopeDefaults_();

  // Detune multipliers depend on config
  recomputeDetuneMultipliers();

  // Pitch depends on harmony, etc.
  refreshPitch_();
}
