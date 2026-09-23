// Voice.cpp — Voice implementation. Signal chain per sample/span: sources →
// envelope gain → effects → velocity → main filter → HPF. Control thread only
// stages (setters/updateParameters); Core 1 renders and never allocates.
#include "Voice.h"
#include "../utils/AudioRam.h"
#include "../utils/DspMapping.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include "../pico2seq-core/scales/scales.h" // Use centralized SCALES_COUNT / SCALE_STEPS
#include "VoicePresets.h"
#include "MusicalValues.h"

// Default slide rate: gentle portamento between sequenced notes.
static constexpr float FREQ_SLEW_RATE = 0.00035f; // per-sample slide coefficient
static constexpr float BASE_FREQ =
    110.0f; // chromatic-fallback anchor (A1) when no scale table is injected

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

float Voice::frequencyLookupTable[128];
bool Voice::lookupTableInitialized = false;

// MIDI 0..127 pitch table (built once, thread-safe): per-sample mtof() would
// waste Core 1 on every note; a lookup keeps pitch commits cheap.
inline void Voice::initFrequencyLookupTable() noexcept
{
  std::call_once(g_freqTableOnce, []() noexcept
                 {
    // One mtof() per MIDI note, then lookups forever.
    for (int midi = 0; midi < 128; ++midi)
    {
      frequencyLookupTable[midi] = rpdsp::midiNoteToHz(static_cast<float>(midi));
    } });
}

// Cached detune multipliers (2^(semitones/12) per osc): chorus/thickness
// without powf on Core 1. Bumps detuneVersion_ so the pitch cache recomputes.
inline void Voice::recomputeDetuneMultipliers()
{
  // 1/12 octave-per-semitone factor for the exp2f detune above.
  constexpr float kInv12 = 1.0f / 12.0f;
  for (uint8_t i = 0; i < 3; ++i)
  {
    detuneMul[i] = exp2f(config.oscDetuning[i] * kInv12);
  }
  detuneVersion_++;
}

  // One-pole smoother coefficient: ~63% of a knob move lands in tau seconds,
  // so gain/cutoff glides hide steps (zipper) instead of clicking.
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
  // Pitch cache starts dirty so the first span computes the real note.
  baseFreqDirty_ = true;
  cachedBaseFreqHz_ = 220.0f;
  lastSentBaseFreqHz_ = -1.0f;
  // Initialize frequency lookup table once in a thread-safe manner
  initFrequencyLookupTable();
  // Initialize cached detune multipliers
  recomputeDetuneMultipliers();

  // Initialize runtime caches used by optimizations
  lastAppliedFilterCutoff = -1.0f;
  lastEnvelopeValue = 0.0f;

  // Oscillator slots are fixed-size members; nothing to allocate.

  // Slide state per osc: exponential glide toward each new note when set.
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
  state.gateLengthTicks = 90; // 3/4 step, matching VoiceConfig::baseGateLength
  state.isGateHigh = false;
  state.hasSlide = false;
  state.shouldRetrigger = false;
  controls_.config = cfg;
  controls_.state = state;
}

void Voice::init(float sr)
{
#if P2S_VOICE_IDLE_SKIP
  quietRun_ = 0;
#endif
  // Setup only: neither core may access this Voice concurrently with init().
  // Fold any pre-init setters into the initial state without running DSP early.
  ControlUpdate unused;
  while (controlQueue_.tryPop(unused)) {}
  controls_.scaleIndex = currentScalePtr_ ? *currentScalePtr_ : 0;
  controls_.changes = 0;
  config = controls_.config;
  velocityToAmplitude_ = VoiceParameters::velocityToAmplitude(config);
  state = controls_.state;
  gate = state.isGateHigh;
  scaleTable = controls_.scaleTable;
  scaleTableCount = controls_.scaleCount;
  audioScaleIndex_ = controls_.scaleIndex;
  slideTimeSeconds = controls_.slideSeconds;
  pitchBendSemitones_ = controls_.bendSemitones;
  pitchModSemitones_ = controls_.modulationSemitones;
  filterFrequency = controls_.filterHz;
  filterEnvTarget_ = filterFrequency;
  refreshFilterEnvDepth_();
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
  // HPF last in chain: sheds sub rumble (esp. Karplus tails) below the cutoff.
  highPassFilter.prepare(sampleRate);
  highPassFilter.setCutoff(config.highPassFreq);
  highPassFilter.setResonance(config.highPassRes);
  hpfBypass_ = (config.highPassFreq <= 20.0f && config.highPassRes <= 0.01f);
  // 4 ms cutoff glide: tracks envelope sweeps without zipper stepping.
  filterCutoffCurrent = filterFrequency;
  {
    const float tau = 0.004f; // glide time in seconds
    filterCutoffAlpha = makeSmoothingAlpha(tau, sampleRate);
  }

  // -1 sentinel forces the first span's SetFreq; 0 envelope reads as silent.
  lastAppliedFilterCutoff = -1.0f;
  lastEnvelopeValue = 0.0f;

  // ADSR: attack blooms the note, sustain holds it, release tails it.
  envelope.prepare(sampleRate);
  applyEnvelopeDefaults_();
  gateHighPrev_ = false;

  // Gentle grit stage: 0-1 drive maps to 1-4 waveshaper drive.
  overdrive.setDrive(1.0f + (config.overdriveDrive * 3.0f)); // map 0-1 drive to 1-4
  overdrive.setOutputGain(1.0f);

  // Alternate engines: prepare + seed state, then apply tuning from config.
  // init() is setup-time, so the engine cache is set directly here.
  cachedEngine_ = (config.engine <= static_cast<uint8_t>(ENGINE_SITAR))
                      ? config.engine
                      : static_cast<uint8_t>(ENGINE_OSC);
  recipeEngine_.prepare(sampleRate);
  recipeEngine_.select(config.recipe);
  waveguide_.prepare(sampleRate);
  sitar_.prepare(sampleRate);
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

void PICO2SEQ_AUDIO_FUNC(Voice::applyControlUpdate_)() noexcept
{
  if (!controlQueue_.tryPop(audioUpdate_))
    return;
  const ControlUpdate &update = audioUpdate_;

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
    // The sitar meend bend time follows the voice's slide parameter.
    pushSitarSlideTime_();
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
  {
    filterFrequency = update.filterHz;
    refreshFilterEnvDepth_();
  }
}

void PICO2SEQ_AUDIO_FUNC(Voice::processBlock)(float *out, uint32_t n) noexcept
{
  uint32_t i = 0;
  while (i < n)
  {
    if (!controlQueue_.consumerEmpty())
    {
      applyControlUpdate_();   // exactly one update per sample while any are queued
      renderSpan_(out + i, 1);
      ++i;
      continue;
    }
    const uint32_t span = std::min<uint32_t>(n - i, kMaxSpan);
    renderSpan_(out + i, span);
    i += span;
  }
}

float PICO2SEQ_AUDIO_FUNC(Voice::process)() noexcept
{
  float sample = 0.0f;
  processBlock(&sample, 1);
  return sample;
}

void PICO2SEQ_AUDIO_FUNC(Voice::renderSpan_)(float *out, uint32_t n) noexcept
{
  if (!config.enabled) { std::fill_n(out, n, 0.0f); return; }
#if P2S_VOICE_IDLE_SKIP
  if (canSkipSilentSpan_())
  {
    advanceSilentSpan_(n);
    std::fill_n(out, n, 0.0f);
    return;
  }
#endif
  const float *env = spanEnv_.data();
  float *sig = spanSignal_.data();

  // (1) gate and shouldRetrigger cannot change inside a span, so the per-sample
  //     edge logic only acts on the first sample.
  handleGateEdges_();

  // (2) Envelope on a local copy (state stays in registers).
  if (config.hasEnvelope)
  {
    rpdsp::ADSR adsr = envelope;
    for (uint32_t k = 0; k < n; ++k) spanEnv_[k] = adsr.process();
    envelope = adsr;
    // Held envelope edits land once their stage has ended.
    if (envelopeChangePending_())
      applyPendingEnvelopeTimes_(false);
  }
  else
  {
    std::fill_n(spanEnv_.data(), n, 1.0f);
  }
  lastEnvelopeValue = spanEnv_[n - 1];

  // (3) Today this runs after sample 0's envelope. applyStructuralConfig_() never
  //     touches the ADSR, so running it after the whole envelope loop is equivalent.
  if (structuralPending_ && !gate) applyStructuralConfig_();

  // (4) Cutoff smoother + setFreq throttle; coefficient updates recorded by index.
  const uint32_t events = config.hasFilter ? planFilterUpdates_(env, n) : 0;

  // (5) Sources retain the per-sample silence gate and pending pitch commits.
  renderSources_(sig, env, n);

  // (6) VCA, then pre-filter effects in sample order.
  for (uint32_t k = 0; k < n; ++k) sig[k] *= env[k];
  if (config.hasOverdrive || cachedEngine_ == static_cast<uint8_t>(ENGINE_NOISEFX))
    for (uint32_t k = 0; k < n; ++k) applyEffects(sig[k]);

  // (7) Velocity (state cannot change inside a span).
  const float amplitude = velocityToAmplitude_ ? state.velocityLevel : 1.0f;
  for (uint32_t k = 0; k < n; ++k) sig[k] *= amplitude;

  // (8) Main filter, applying (4)'s updates before their sample.
  if (config.hasFilter) runMainFilter_(sig, n, events);

  // (9) High-pass on a local copy.
  if (!hpfBypass_)
  {
    rpdsp::StateVariableFilter hpf = highPassFilter;
    for (uint32_t k = 0; k < n; ++k) sig[k] = hpf.process(sig[k]).highpass;
    highPassFilter = hpf;
  }

  // (10) Output level.
  const float level = config.outputLevel;
  for (uint32_t k = 0; k < n; ++k) out[k] = sig[k] * level;
#if P2S_VOICE_IDLE_SKIP
  trackQuietOutput_(out, n);
#endif
}

#if P2S_VOICE_IDLE_SKIP
bool PICO2SEQ_AUDIO_FUNC(Voice::canSkipSilentSpan_)() const noexcept
{
  return quietRun_ >= kQuietHold && config.hasEnvelope && !gate && !gateHighPrev_ &&
         !state.shouldRetrigger && !structuralPending_ && !envelope.isActive() &&
         cachedEngine_ != ENGINE_WAVEGUIDE && cachedEngine_ != ENGINE_NOISEFX &&
         // The sitar engine reports its own composite activity (string +
         // body + taraf): skip only once nothing in the model is ringing.
         (cachedEngine_ != ENGINE_SITAR || !sitar_.isActive());
}

void PICO2SEQ_AUDIO_FUNC(Voice::advanceSilentSpan_)(uint32_t n) noexcept
{
  // Keep cutoff smoothing and its throttle alive; freezing them changes
  // the attack of the next note even after the audible tail has finished.
  lastEnvelopeValue = 0.0f;
  if (!config.hasFilter) return;
  std::fill_n(spanEnv_.data(), n, 0.0f);
  const uint32_t events = planFilterUpdates_(spanEnv_.data(), n);
  if (events > 0)
  {
    // No filter samples run between these updates, so the last coefficients win.
    const float hz = spanFilterEvents_[events - 1].cutoffHz;
    if (config.filterType == FILTER_SVF) filterSvf_.setCutoff(hz);
    else filter.setFreq(hz);
  }
}

void PICO2SEQ_AUDIO_FUNC(Voice::trackQuietOutput_)(const float *out, uint32_t n) noexcept
{
  uint32_t trailing = 0;
  while (trailing < n && std::fabs(out[n - 1 - trailing]) < kQuietLevel) ++trailing;
  const uint32_t run = trailing == n ? quietRun_ + n : trailing;
  quietRun_ = static_cast<uint16_t>(std::min<uint32_t>(run, kQuietHold));
}
#endif

void Voice::handleGateEdges_() noexcept
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
      {
        applyPendingEnvelopeTimes_(true);
        envelope.noteOn();
      }
      // Retune the string before the re-pluck (no-op unless waveguide).
      if (cachedEngine_ == ENGINE_WAVEGUIDE) pushWaveguideParams_();
      wgPluckPending_ = true; // waveguide engine re-plucks on retriggers
      sitarPluckPending_ = true; // sitar engine re-plucks on retriggers
      hypersawTriggerPending_ = true;
      recipeTriggerPending_ = true;
    }
  }
  else if (rising)
  {
    if (config.hasEnvelope)
    {
      applyPendingEnvelopeTimes_(true);
      envelope.noteOn();
    }
    // Fresh string tuning lands with the pluck, never mid-note.
    if (cachedEngine_ == ENGINE_WAVEGUIDE) pushWaveguideParams_();
    wgPluckPending_ = true;
    sitarPluckPending_ = true;
    hypersawTriggerPending_ = true;
    recipeTriggerPending_ = true;
  }
  else if (falling)
  {
    if (config.hasEnvelope)
      envelope.noteOff();
  }
}

// Envelope depth follows the Filter lane, so a cutoff sequence modulates the
// contour as well as the frequency. Cheap enough for the control path; the
// audio path calls it only when a staged cutoff change lands.
void Voice::refreshFilterEnvDepth_() noexcept
{
  // The Filter lane IS the envelope amount: 0 leaves the cutoff parked on the
  // patch base, 1 gives the preset's full sweep. A preset that re-purposes the
  // lane for timbre has no depth to sequence, so it takes its static base.
  const float lane = std::clamp(
      VoiceParameters::binding(config, ParamId::Filter).target != nullptr
          ? config.filterCutoffBase
          : state.filterCutoff,
      0.0f, 1.0f);
  filterEnvOctaves_ = std::max(0.0f, config.filterEnvelopeOctaves) * lane;
  filterEnvRest_ = std::clamp(config.filterEnvelopeRest, 0.0f, 1.0f);
}

uint32_t PICO2SEQ_AUDIO_FUNC(Voice::planFilterUpdates_)(const float *env, uint32_t n) noexcept
{
  float current = filterCutoffCurrent;
  float lastApplied = lastAppliedFilterCutoff;
  uint8_t counter = filterUpdateCounter;
  const float alpha = filterCutoffAlpha;
  const float octaves = filterEnvOctaves_;
  const float rest = filterEnvRest_;
  float target = filterEnvTarget_;
  uint32_t events = 0;
  for (uint32_t k = 0; k < n; ++k)
  {
    // exp2f runs at the setFreq rate, not per sample; the smoother below fills
    // the gap, which is also what keeps cutoff modulation free of zipper noise.
    if (counter == 0)
      target = filterFrequency * exp2f(octaves * (env[k] - rest));
    current += alpha * (target - current);
    if (counter == 0 && ShouldApplyFilterFreq_(current, lastApplied))
    {
      spanFilterEvents_[events++] = {static_cast<uint8_t>(k), current};
      lastApplied = current;
    }
    counter = static_cast<uint8_t>((counter + 1) & (kFilterUpdateInterval - 1));
  }
  filterCutoffCurrent = current;
  lastAppliedFilterCutoff = lastApplied;
  filterUpdateCounter = counter;
  filterEnvTarget_ = target;
  return events;
}

void PICO2SEQ_AUDIO_FUNC(Voice::runMainFilter_)(float *sig, uint32_t n, uint32_t events) noexcept
{
  uint32_t pos = 0;
  for (uint32_t e = 0; e <= events; ++e)
  {
    const uint32_t end = (e < events) ? spanFilterEvents_[e].index : n;
    if (end > pos)
    {
      if (config.filterType == FILTER_SVF)
      {
        rpdsp::StateVariableFilter svf = filterSvf_;
        const uint8_t sel = svfOutputSel_;
        for (uint32_t k = pos; k < end; ++k)
        {
          const rpdsp::StateVariableOutput o = svf.process(sig[k]);
          sig[k] = (sel == 1) ? o.bandpass : (sel == 2) ? o.highpass : o.lowpass;
        }
        filterSvf_ = svf;
      }
      else
      {
        rpdsp::LadderFilter ladder = filter;
        for (uint32_t k = pos; k < end; ++k) sig[k] = ladder.process(sig[k]);
        filter = ladder;
      }
    }
    if (e < events)
    {
      const float hz = spanFilterEvents_[e].cutoffHz;
      if (config.filterType == FILTER_SVF) filterSvf_.setCutoff(hz);
      else filter.setFreq(hz);
    }
    pos = end;
  }
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

void PICO2SEQ_AUDIO_FUNC(Voice::commitOscillatorPitch_)() noexcept
{
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

}

void PICO2SEQ_AUDIO_FUNC(Voice::renderSources_)(float *sig, const float *env, uint32_t n) noexcept
{
  std::fill_n(sig, n, 0.0f);
  const bool gateBySilence = config.hasEnvelope;
  uint32_t first = 0;
  while (first < n && gateBySilence && env[first] <= 0.001f) ++first;
  if (first == n) return; // Pitch commits wait until a source can advance.

  if (cachedEngine_ == ENGINE_WAVEGUIDE)
  {
    for (uint32_t k = first; k < n; ++k)
      if (!gateBySilence || env[k] > 0.001f) sig[k] = processWaveguide_();
    return;
  }
  if (cachedEngine_ == ENGINE_SITAR)
  {
    for (uint32_t k = first; k < n; ++k)
      if (!gateBySilence || env[k] > 0.001f) sig[k] = processSitar_();
    return;
  }
  if (cachedEngine_ == ENGINE_HYPERSAW || cachedEngine_ == ENGINE_RECIPE)
  {
    for (uint32_t k = first; k < n; ++k)
      if (!gateBySilence || env[k] > 0.001f) sig[k] = processPitchedEngine_();
    return;
  }
  if (cachedEngine_ == ENGINE_NOISEFX)
  {
    for (uint32_t k = first; k < n; ++k)
      if (!gateBySilence || env[k] > 0.001f) sig[k] = processNoiseFxSource_();
    return;
  }
  if (cachedOscCount_ == 0)
  {
    for (uint32_t k = first; k < n; ++k)
      if (!gateBySilence || env[k] > 0.001f) sig[k] = noise_.process();
    return;
  }

  commitOscillatorPitch_();
  for (size_t i = 0; i < cachedOscCount_; ++i)
  {
    if (!state.hasSlide)
    {
      oscillators[i].renderAdd(sig, env, n, config.oscAmplitudes[i], gateBySilence);
      continue;
    }
    for (uint32_t k = first; k < n; ++k)
    {
      if (gateBySilence && env[k] <= 0.001f) continue;
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
      sig[k] += oscillators[i].process() * config.oscAmplitudes[i];
    }
  }
}

void PICO2SEQ_AUDIO_FUNC(Voice::applyEffects)(float &signal)
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

  // Level adjustments removed from here; handled in renderSpan_
}

// Provide a wrapper to maintain API compatibility
void Voice::processEffectsChain(float &signal)
{
  applyEffects(signal);
}

// -------- Alternate engines (waveguide / sitar / Hypersaw / noise-FX) --------

void Voice::pushSitarSlideTime_() noexcept
{
  if (cachedEngine_ != static_cast<uint8_t>(ENGINE_SITAR))
    return;
  if (sitarSettings_.valid && sitarSettings_.slideTime == slideTimeSeconds)
    return;
  sitar_.setSlideTimeSeconds(slideTimeSeconds);
  sitarSettings_.slideTime = slideTimeSeconds;
}

void Voice::applyEngineConfig_()
{
  // Engine tuning. These are control-rate setters with internal clamps;
  // setBrightness()/setPickHardness() derive coefficients from sampleRate_,
  // so waveguide_.prepare()/sitar_.prepare() must have run first (init()
  // guarantees this).
  // NOTE: cachedEngine_ is NOT updated here — the engine switch belongs to
  // applyStructuralConfig_() so a live swap waits for the gate to fall.
  if (cachedEngine_ == ENGINE_WAVEGUIDE) {
    // String tuning lands only on note starts (see pushWaveguideParams_()):
    // retuning a ringing Karplus loop mid-note clicks and fights the tail,
    // so edits made while gated wait for the next gate rise/retrigger.
    // Idle pushes carry the exact base (no RNG consumed — determinism for a
    // given gate history); the gate-on path re-rolls humanization anyway.
    if (gate)
      return;
    auto &last = waveguideSettings_;
    if (!last.valid || last.t60 != config.wgT60)
      waveguide_.setDecayTimeSeconds(config.wgT60);
    if (!last.valid || last.brightness != config.wgBrightness)
      waveguide_.setBrightness(config.wgBrightness);
    if (!last.valid || last.pickPosition != config.wgPickPosition)
      waveguide_.setPickPosition(config.wgPickPosition);
    if (!last.valid || last.pickHardness != config.wgPickHardness)
      waveguide_.setPickHardness(config.wgPickHardness);
    if (!last.valid || last.stiffness != config.wgStiffness)
      waveguide_.setStiffness(config.wgStiffness);
    if (!last.valid || last.detune != config.wgDetune)
      waveguide_.setDetuneCents(config.wgDetune);
    last = {config.wgT60, config.wgBrightness, config.wgPickPosition,
            config.wgPickHardness, config.wgStiffness, config.wgDetune, true};
    waveguideApplied_ = {config.wgT60, config.wgBrightness, config.wgPickPosition,
                         config.wgPickHardness, config.wgStiffness, config.wgDetune, true};
  } else if (cachedEngine_ == ENGINE_SITAR) {
    auto &last = sitarSettings_;
    if (!last.valid || last.decay != config.sitarDecay)
      sitar_.setDecayTimeSeconds(config.sitarDecay);
    if (!last.valid || last.brightness != config.sitarBrightness)
      sitar_.setBrightness(config.sitarBrightness);
    if (!last.valid || last.pickPosition != config.sitarPickPosition)
      sitar_.setPickPosition(config.sitarPickPosition);
    if (!last.valid || last.pickHardness != config.sitarPickHardness)
      sitar_.setPickHardness(config.sitarPickHardness);
    if (!last.valid || last.jawari != config.sitarJawari)
      sitar_.setJawari(config.sitarJawari);
    if (!last.valid || last.jawariThreshold != config.sitarJawariThreshold)
      sitar_.setJawariThreshold(config.sitarJawariThreshold);
    if (!last.valid || last.tarafAmount != config.sitarTarafAmount)
      sitar_.setTarafAmount(config.sitarTarafAmount);
    if (!last.valid || last.tarafDecay != config.sitarTarafDecay)
      sitar_.setTarafDecaySeconds(config.sitarTarafDecay);
    if (!last.valid || last.bodyAmount != config.sitarBodyAmount)
      sitar_.setBodyAmount(config.sitarBodyAmount);
    if (!last.valid || last.bodyFrequency != config.sitarBodyFrequency)
      sitar_.setBodyFrequency(config.sitarBodyFrequency);
    last = {config.sitarDecay, config.sitarBrightness, config.sitarPickPosition,
            config.sitarPickHardness, config.sitarJawari,
            config.sitarJawariThreshold, config.sitarTarafAmount,
            config.sitarTarafDecay, config.sitarBodyAmount,
            config.sitarBodyFrequency, last.slideTime, true};
    // Meend bend time follows the voice's slide parameter (slideSeconds).
    pushSitarSlideTime_();
>>>>>>> 1dd6e54 (feat: add ENGINE_SITAR voice engine wired to rpdsp::SitarStringVoice)
  } else if (cachedEngine_ == ENGINE_HYPERSAW) {
    hypersaw_.setDetune(config.hypersawDetune);
    hypersaw_.setMix(config.hypersawMix);
  } else if (cachedEngine_ == ENGINE_RECIPE) {
    recipeEngine_.configure(config);
  }

}

float Voice::wgHumanize_(float base) noexcept
{
  // ±kWaveguideHumanize multiplicative; exact zeros stay zero.
  return base * (1.0f + kWaveguideHumanize * wgHumanizeRng_.nextBipolar());
}

void Voice::pushWaveguideParams_() noexcept
{
  // Per-note humanization: each base rolls ±4% (under the 5% ceiling) so
  // repeated notes never machine-gun. The setters clamp into range; the
  // position/hardness/detune setters take effect on the next pluck(), which
  // this call always precedes (gate rise/retrigger arms wgPluckPending_).
  // Only waveguideApplied_ is recorded here — the base cache belongs to
  // applyEngineConfig_(), so an edit made while gated is still "unseen"
  // and lands (re-humanized) on the next gate-on.
  const float t60 = wgHumanize_(config.wgT60);
  const float brightness = wgHumanize_(config.wgBrightness);
  const float pickPosition = wgHumanize_(config.wgPickPosition);
  const float pickHardness = wgHumanize_(config.wgPickHardness);
  const float stiffness = wgHumanize_(config.wgStiffness);
  const float detune = wgHumanize_(config.wgDetune);
  waveguide_.setDecayTimeSeconds(t60);
  waveguide_.setBrightness(brightness);
  waveguide_.setPickPosition(pickPosition);
  waveguide_.setPickHardness(pickHardness);
  waveguide_.setStiffness(stiffness);
  waveguide_.setDetuneCents(detune);
  waveguideApplied_ = {t60, brightness, pickPosition, pickHardness,
                       stiffness, detune, true};
}

float PICO2SEQ_AUDIO_FUNC(Voice::processWaveguide_)() noexcept
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
      // Velocity drives the excitation itself — like a real pluck, a soft
      // pick injects less energy (and a softer attack transient) instead of
      // ringing at full level and being scaled after the fact.
      waveguide_.pluck(targetHz, state.velocityLevel);
    }
  }
  return waveguide_.process();
}

float PICO2SEQ_AUDIO_FUNC(Voice::processSitar_)() noexcept
{
  // Same pitch plumbing as the waveguide: honor oscillator-bank harmony on
  // the first slot, else the plain base pitch.
  const float targetHz = (cachedOscCount_ > 0) ? pitchCache_.finalFreq[0]
                                               : pitchCache_.baseFreq;
  if (sitarPluckPending_)
  {
    sitarPluckPending_ = false;
    if (targetHz > 0.0f)
    {
      // Velocity drives the excitation itself, clamped to [0,1]: the model's
      // pluck amplitude is deliberately unbounded above, and velocity is a
      // 0..1 control. A pluck cancels any active slide (a re-pluck wins).
      sitar_.pluck(targetHz, std::clamp(state.velocityLevel, 0.0f, 1.0f));
      engineAppliedPitchGen_ = pitchGen_;
    }
  }
  else if (state.isGateHigh && pitchGen_ != engineAppliedPitchGen_)
  {
    // A ringing string repitched while gated (slide lane, legato note,
    // bend/mod): meend — bend to the new pitch through the model's
    // fractional-delay slew instead of waiting for the next pluck.
    if (targetHz > 0.0f)
    {
      sitar_.slideTo(targetHz);
      engineAppliedPitchGen_ = pitchGen_;
    }
  }
  return sitar_.process();
}

float PICO2SEQ_AUDIO_FUNC(Voice::processPitchedEngine_)() noexcept
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

float PICO2SEQ_AUDIO_FUNC(Voice::processNoiseFxSource_)() noexcept
{
  float source = noise_.process() * config.noiseSourceLevel;
  if (config.noiseChaosLevel > 0.001f)
  {
    // Lorenz rate follows the base pitch so the growl tracks the sequence
    // instead of sitting at one fixed register (0.0001 slow CV .. 0.02 growl).
    const float baseHz = (pitchCache_.baseFreq > 0.0f) ? pitchCache_.baseFreq : 110.0f;
    const float rate = std::clamp(baseHz * config.noiseChaosRate / sampleRate, 1.0e-4f, 0.02f);
    source += rpdsp::chaos_lorenz(rate, noiseChaosState_) * config.noiseChaosLevel * 0.5f;
  }
  return source;
}

void Voice::resetAlternateEngines_() noexcept
{
  recipeEngine_.reset();
  recipeTriggerPending_ = false;
  waveguide_.reset();
  waveguideSettings_.valid = false;
  waveguideApplied_.valid = false;
  // Fresh string model → fresh humanization sequence (distinct per voice).
  // Keeps reset/re-init output bit-identical for a given gate history.
  wgHumanizeRng_ = rpdsp::XorShift32{kWaveguideHumanizeSeed +
                                     static_cast<uint32_t>(voiceId) * 0x9E3779B9u};
  wgPluckPending_ = false;
  sitar_.reset();
  sitarSettings_.valid = false;
  sitarPluckPending_ = false;
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

void Voice::updateOscillatorFrequencies()
{
  // Deprecated path for direct control-thread commits; retained for backward compatibility.
  // Both cache refresh and oscillator commits remain on the audio thread.
  refreshPitch_();
}

inline void Voice::applyEnvelopeParameters() noexcept
{
  if(config.usePatchBases) {
    // Each lane drives its envelope stage unless the layout re-purposes it
    // (then the patch value shapes that stage, set in applyConfig_()).
    if (VoiceParameters::layout(config).envelopeFromTracks)
      setEnvelopeTimes_(MusicalValues::attackSeconds(state.attackTimeSeconds),
                        MusicalValues::envelopeSeconds(state.decayTimeSeconds));
    const bool sustainLane = !VoiceParameters::binding(config, ParamId::Sustain).target;
    const bool releaseLane = !VoiceParameters::binding(config, ParamId::Release).target;
    setEnvelopeShape_(sustainLane ? state.sustainLevel : config.defaultSustain,
                      releaseLane ? MusicalValues::releaseSeconds(state.releaseTimeSeconds)
                                  : config.defaultRelease);
    return;
  }
  // Map normalized parameters to appropriate ranges
  float attack =
      dspmap::fmap(state.attackTimeSeconds, 0.002f, 0.5f, dspmap::Mapping::LINEAR);
  float decay =
      dspmap::fmap(state.decayTimeSeconds, 0.01f, 0.5f, dspmap::Mapping::LOG);
  // float release = decay; // Use decay for release in this implementation

  setEnvelopeTimes_(attack, 0.075f + (decay * 0.32f));
  envelope.setRelease(decay);
}

inline void Voice::applyEnvelopeDefaults_() noexcept
{
  setEnvelopeTimes_(config.defaultAttack, config.defaultDecay);
  setEnvelopeShape_(config.defaultSustain, config.defaultRelease);
}

void Voice::setEnvelopeTimes_(float attackSeconds, float decaySeconds) noexcept
{
  pendingAttackSeconds_ = attackSeconds;
  pendingDecaySeconds_ = decaySeconds;
  applyPendingEnvelopeTimes_(false);
}

void Voice::setEnvelopeShape_(float sustainLevel, float releaseSeconds) noexcept
{
  pendingSustain_ = std::clamp(sustainLevel, 0.0f, 1.0f);
  pendingReleaseSeconds_ = releaseSeconds;
  applyPendingEnvelopeTimes_(false);
}

void PICO2SEQ_AUDIO_FUNC(Voice::applyPendingEnvelopeTimes_)(bool noteOn) noexcept
{
  // A stage not running (or restarting at a note-on) can take a new length
  // without a level step; the running one keeps its length until it ends.
  const auto stage = envelope.stage();
  if (pendingAttackSeconds_ >= 0.0f && (noteOn || stage != rpdsp::ADSR::Stage::kAttack))
  {
    envelope.setAttack(pendingAttackSeconds_);
    pendingAttackSeconds_ = -1.0f;
  }
  if (pendingDecaySeconds_ >= 0.0f && (noteOn || stage != rpdsp::ADSR::Stage::kDecay))
  {
    envelope.setDecay(pendingDecaySeconds_);
    pendingDecaySeconds_ = -1.0f;
  }
  // Decay ramps toward the sustain level and sustain holds it, so a new
  // level in either stage steps the output.
  if (pendingSustain_ >= 0.0f &&
      (noteOn || (stage != rpdsp::ADSR::Stage::kDecay && stage != rpdsp::ADSR::Stage::kSustain)))
  {
    envelope.setSustain(pendingSustain_);
    pendingSustain_ = -1.0f;
  }
  if (pendingReleaseSeconds_ >= 0.0f && (noteOn || stage != rpdsp::ADSR::Stage::kRelease))
  {
    envelope.setRelease(pendingReleaseSeconds_);
    pendingReleaseSeconds_ = -1.0f;
  }
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
  const int *row = scaleTable && scaleTableCount ? scaleTable[effectiveScaleIndex_()] : nullptr;
  const int midiNote = MusicalValues::midiNote(note, octaveOffset, harmony, row);
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

void PICO2SEQ_AUDIO_FUNC(Voice::processFrequencySlew)(uint8_t oscIndex, float targetFreq)
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
// audio thread commits in renderSources_() when pitchGen_ != appliedPitchGen_.
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
  // The cutoff is the patch base alone - the encoder's Filter target moves it.
  // The Filter lane is envelope depth now (refreshFilterEnvDepth_), so a
  // sequenced sweep opens and closes the filter through the contour rather
  // than stepping the frequency underneath it.
  (void)repurposedFilter;
  filterFrequency = VoiceParameters::mapCutoff(parameters, config.filterCutoffBase);
  refreshFilterEnvDepth_();
  if (parameters.envelopeFromTracks || config.usePatchBases)
    applyEnvelopeParameters();
  applyEngineConfig_();

  // Stage pitch recompute; audio thread will commit oscillator freq via renderSources_
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
#if P2S_VOICE_IDLE_SKIP
  // A new configuration must prove silence before it may be skipped.
  quietRun_ = 0;
#endif
  bool structuralChange = stagedOscCount_ != newConfig.oscillatorCount ||
      stagedEngine_ != newConfig.engine || config.recipe != newConfig.recipe;
  for(size_t i=0;i<3;++i)
    structuralChange = structuralChange || stagedWaveforms_[i] != newConfig.oscWaveforms[i] ||
        stagedPulseWidth_[i] != newConfig.oscPulseWidth[i];
  config = newConfig;
  velocityToAmplitude_ = VoiceParameters::velocityToAmplitude(config);

  // Update filters (scalar; safe mid-note)
  if (config.hasFilter)
  {
    filter.setRes(config.filterRes);
    filter.setInputDrive(config.filterDrive);
    filter.setPassbandGain(config.filterPassbandGain);
    filter.setMode(ladderModeFromVoiceMode(config.filterMode));
    configureMainFilterFromConfig_();

    const auto &paramLayout = VoiceParameters::layout(config);
    filterFrequency = VoiceParameters::mapCutoff(paramLayout, config.filterCutoffBase);
    refreshFilterEnvDepth_();
    // The cutoff smoother glides to the new target. Snapping to it (without
    // the envelope) stepped the filter on every live base edit. Both
    // topologies take the current cutoff so a switch starts from it.
    filter.setFreq(filterCutoffCurrent);
    filterSvf_.setCutoff(filterCutoffCurrent);
    lastAppliedFilterCutoff = filterCutoffCurrent;
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
  stagedEngine_ = (config.engine <= static_cast<uint8_t>(ENGINE_SITAR))
                      ? config.engine
                      : static_cast<uint8_t>(ENGINE_OSC);
  structuralPending_ = structuralPending_ || structuralChange;
  if (structuralPending_ && !gate)
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
