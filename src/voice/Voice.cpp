/*
 * Voice.cpp
 *
 * Implements the Voice class, which encapsulates all per‑voice audio processing for the
 * Pico2Seq synthesizer. A Voice manages its own oscillators, filters, envelope,
 * and optional effects (overdrive, wavefolder). It provides real‑time parameter
 * updates (detune, slide, filter, etc.) while keeping the audio thread lock‑free.
 *
 * The design goals are:
 *   • Low latency: most calculations are pre‑computed or use fast approximations.
 *   • Thread safety: static initialization uses std::call_once, and mutable state
 *     is updated without reallocations that could stall the audio callback.
 *   • Flexibility: configuration can be changed on‑the‑fly (e.g., toggling
 *     overdrive or wavefolder) without audible glitches.
 */
#include "Voice.h"
#include "../dsp/dsp.h"
#include "Arduino.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include "../scales/scales.h" // Use centralized SCALES_COUNT / SCALE_STEPS
#include "../scales/chord_stepper.h"
#include "VoicePresets.h"

// Thread-safe one-time init guard for frequency table
namespace {
static std::once_flag g_freqTableOnce;
}

// Static member initialization
float Voice::frequencyLookupTable[128];

// Pre-compute MIDI note to frequency mapping to avoid runtime calculations
inline void Voice::initFrequencyLookupTable() noexcept
{
  std::call_once(g_freqTableOnce, []() noexcept {
    for (int midi = 0; midi < 128; ++midi) {
      frequencyLookupTable[midi] = daisysp::mtof(static_cast<float>(midi));
    }
  });
}

// Cache detune multipliers for efficient per-sample pitch adjustments
inline void Voice::recomputeDetuneMultipliers()
{
  constexpr float kInv12 = 1.0f / 12.0f;  // Semitone to octave conversion
  for (uint8_t i = 0; i < 3; ++i) {
    detuneMul[i] = exp2f(config.oscDetuning[i] * kInv12);
  }
}

// Initialize voice with configuration and default state values
Voice::Voice(uint8_t id, const VoiceConfig &cfg)
    : voiceId(id), config(cfg), sampleRate(48000.0f), filterFrequency(1000.0f),
      gate(false), sequencer(nullptr) {
  initFrequencyLookupTable();
  recomputeDetuneMultipliers();
  oscillators.resize(config.oscillatorCount);

  // Initialize frequency slewing for all oscillators
  for (int i = 0; i < 3; i++) {
    freqSlew[i].currentFreq = 440.0f;
    freqSlew[i].targetFreq = 440.0f;
  }

  // Set default voice state
  state.noteIndex = 0.0f;
  state.velocityLevel = 0.8f;
  state.filterCutoff = 0.37f;
  state.attackTimeSeconds = 0.01f;
  state.decayTimeSeconds = 0.1f;
  state.octaveOffset = 0;
  state.gateLengthTicks = 12;
  state.isGateHigh = false;
  state.hasSlide = false;
  state.shouldRetrigger = false;
}

// Initialize DSP modules and compute per-sample coefficients
void Voice::init(float sr) {
  sampleRate = sr;

  // Compute per-sample slide coefficient from time constant
  if (slideTimeSeconds <= 0.0f) {
    slideAlpha = 1.0f;  // instantaneous
  } else {
    const float invTauFs = 1.0f / (slideTimeSeconds * sampleRate);
    slideAlpha = 1.0f - std::exp(-invTauFs);
  }

  // Initialize wavefolder wet/dry smoothing (5ms time constant)
  {
    const float tau = 0.005f;
    const float invTauFs = 1.0f / (tau * sampleRate);
    wavefolderMixAlpha = 1.0f - std::exp(-invTauFs);
    wavefolderMixTarget = config.hasWavefolder ? 1.0f : 0.0f;
    wavefolderMix = wavefolderMixTarget;
  }

  // Initialize oscillators
  for (size_t i = 0; i < oscillators.size() && i < config.oscillatorCount; i++) {
    oscillators[i].Init(sampleRate);
    oscillators[i].SetWaveform(config.oscWaveforms[i]);
    oscillators[i].SetAmp(config.oscAmplitudes[i]);

    // Set pulse width for square/pulse waves
    if (config.oscWaveforms[i] == daisysp::Oscillator::WAVE_POLYBLEP_SAW) {
      oscillators[i].SetPw(config.oscPulseWidth[i]);
    }
  }

  // Initialize noise generator
  noise_.Init();
  noise_.SetSeed(1);
  noise_.SetAmp(1.0f);

  // Initialize filters
  filter.Init(sampleRate);
  filter.SetFreq(filterFrequency);
  filter.SetRes(config.filterRes);
  filter.SetInputDrive(config.filterDrive);
  filter.SetPassbandGain(config.filterPassbandGain);
  filter.SetFilterMode(config.filterMode);

  highPassFilter.Init(sampleRate);
  highPassFilter.SetFreq(config.highPassFreq);
  highPassFilter.SetRes(config.highPassRes);

  // Initialize envelope
  envelope.Init(sampleRate);
  envelope.SetAttackTime(config.defaultAttack);
  envelope.SetDecayTime(config.defaultDecay);
  envelope.SetSustainLevel(config.defaultSustain);
  envelope.SetReleaseTime(config.defaultRelease);

  // Initialize effects
  overdrive.Init();
  overdrive.SetDrive(config.overdriveDrive);

  wavefolder.Init();
  wavefolder.SetGain(config.wavefolderGain);
  wavefolder.SetOffset(config.wavefolderOffset);

  recomputeDetuneMultipliers();
}

// Update voice configuration without audio dropouts
void Voice::setConfig(const VoiceConfig &cfg) {
  const bool oscCountChanged = (cfg.oscillatorCount != config.oscillatorCount);
  config = cfg;

  // Update oscillator parameters if count hasn't changed
  if (!oscCountChanged) {
    const size_t oscCount = std::min(static_cast<size_t>(3), oscillators.size());
    for (size_t i = 0; i < oscCount; i++) {
      oscillators[i].SetWaveform(config.oscWaveforms[i]);
      oscillators[i].SetAmp(config.oscAmplitudes[i]);
      if (config.oscWaveforms[i] == daisysp::Oscillator::WAVE_POLYBLEP_SAW) {
        oscillators[i].SetPw(config.oscPulseWidth[i]);
      }
    }
  }

  // Update filter parameters
  filter.SetRes(config.filterRes);
  filter.SetInputDrive(config.filterDrive);
  filter.SetPassbandGain(config.filterPassbandGain);
  filter.SetFilterMode(config.filterMode);
  highPassFilter.SetFreq(config.highPassFreq);
  highPassFilter.SetRes(config.highPassRes);

  // Update effects
  if (config.hasOverdrive) {
    overdrive.SetDrive(config.overdriveDrive);
  }
  wavefolderMixTarget = config.hasWavefolder ? 1.0f : 0.0f;

  recomputeDetuneMultipliers();
}





float Voice::process() noexcept {
  if (!config.enabled) {
    return 0.0f;
  }

  float envelopeValue = computeEnvelope();
  updateFilter(envelopeValue);
  float mixed = mixOscillators();
  applyEffects(mixed);
  return finalizeOutput(mixed, envelopeValue);
}

float Voice::computeEnvelope() {
  if (state.shouldRetrigger) {
    envelope.Retrigger(false);
    state.shouldRetrigger = false;
  }
  return config.hasEnvelope ? envelope.Process(gate) : 1.0f;
}

void Voice::updateFilter(float envelopeValue) {
  filter.SetFreq((filterFrequency * envelopeValue) + (filterFrequency * 0.25f));
}

float Voice::mixOscillators() {
  float mixedOscillators = 0.0f;
  const size_t oscCount = std::min(static_cast<size_t>(3), oscillators.size());

  if (oscCount > 0) {
    for (size_t i = 0; i < oscCount; i++) {
      if (state.hasSlide) {
        processFrequencySlew(i, freqSlew[i].targetFreq);
        oscillators[i].SetFreq(freqSlew[i].currentFreq);
      }
      mixedOscillators += oscillators[i].Process();
    }
  } else {
    // Percussion voices use noise instead of oscillators
    mixedOscillators = noise_.Process();
  }

  return mixedOscillators;
}

void Voice::applyEffects(float &signal) {
  if (config.hasOverdrive) {
    signal = overdrive.Process(signal) * config.overdriveGain;
  }

  // Smoothly crossfade wavefolder to avoid clicks
  wavefolder.SetGain(config.wavefolderGain);
  wavefolder.SetOffset(config.wavefolderOffset);
  wavefolderMixTarget = config.hasWavefolder ? 1.0f : 0.0f;
  wavefolderMix += (wavefolderMixTarget - wavefolderMix) * wavefolderMixAlpha;

  const float wet = wavefolder.Process(signal);
  signal = (1.0f - wavefolderMix) * signal + wavefolderMix * wet;

  // Level adjustments removed from here; handled in finalizeOutput
}

// API compatibility wrapper
void Voice::processEffectsChain(float &signal) {
  applyEffects(signal);
}

inline float Voice::finalizeOutput(float signal, float envelopeValue) noexcept {
  float filteredSignal = filter.Process(signal);
  highPassFilter.Process(filteredSignal);
  float highPassedSignal = highPassFilter.High();

  float finalOutput = highPassedSignal * envelopeValue;
  finalOutput *= (config.outputLevel * state.velocityLevel);
  return finalOutput;
}


void Voice::updateOscillatorFrequencies() {
  // Only update during gate high to prevent re-pitching during release
  if (!state.isGateHigh) {
    return;
  }

  const float baseFreq = calculateNoteFrequency(state.noteIndex, state.octaveOffset, 0, 0, ChordStepper::getIndex());
  const size_t oscCount = std::min(static_cast<size_t>(3), oscillators.size());

  for (size_t i = 0; i < oscCount; i++) {
    const float freq = calculateNoteFrequency(state.noteIndex, state.octaveOffset, config.harmonyOffsets[i], config.detuneAmounts[i], ChordStepper::getIndex());
    freqSlew[i].targetFreq = freq;

    if (!state.hasSlide) {
      freqSlew[i].currentFreq = freq;
      oscillators[i].SetFreq(freq);
    }
    // Sliding frequencies updated in mixOscillators via processFrequencySlew
  }
  }


void Voice::applyEnvelopeParameters() {
  envelope.SetTime(daisysp::ADSR_SEG_ATTACK, config.attackTime);
  envelope.SetTime(daisysp::ADSR_SEG_DECAY, config.decayTime);
  envelope.SetTime(daisysp::ADSR_SEG_RELEASE, config.releaseTime);
  envelope.SetSustainLevel(config.sustainLevel);
}

float Voice::calculateNoteFrequency(int noteIndex, int octaveOffset, int harmonyOffset, float detuneAmount, int chordIndex) const {
  const int effectiveNoteIndex = noteIndex + harmonyOffset;
  const int octaveAdjustedNoteIndex = effectiveNoteIndex + (octaveOffset * 12);
  const int clampedNoteIndex = std::max(0, std::min(octaveAdjustedNoteIndex, static_cast<int>(frequencyLookupTable.size()) - 1));

  const float baseFreq = frequencyLookupTable[clampedNoteIndex];
  const float detuneMultiplier = detuneMultipliers[static_cast<size_t>(std::abs(detuneAmount)) % detuneMultipliers.size()];
  const float detunedFreq = (detuneAmount >= 0) ? baseFreq * detuneMultiplier : baseFreq / detuneMultiplier;

  return detunedFreq;
}

void Voice::processFrequencySlew(uint8_t oscIndex, float targetFreq) {
  if (oscIndex >= 3)
    return;

  const float delta = freqSlew[oscIndex].targetFreq - freqSlew[oscIndex].currentFreq;
  freqSlew[oscIndex].currentFreq = std::fmaf(delta, slideAlpha, freqSlew[oscIndex].currentFreq);
}

void Voice::setFrequency(float frequency) {
  for (uint8_t i = 0; i < config.oscillatorCount && i < oscillators.size() && i < 3; i++) {
    float targetFreq = frequency * detuneMul[i];

    if (state.hasSlide) {
      freqSlew[i].targetFreq = targetFreq;
    } else {
      oscillators[i].SetFreq(targetFreq);
      freqSlew[i].currentFreq = targetFreq;
      freqSlew[i].targetFreq = targetFreq;
    }
  }
}

void Voice::setSlideTime(float slideTime) {
  if (slideTime < 0.0f)
    slideTime = 0.0f;
  if (slideTime > 10.0f)
    slideTime = 10.0f;

  slideTimeSeconds = slideTime;

  if (slideTimeSeconds <= 0.0f || sampleRate <= 0.0f) {
    slideAlpha = 1.0f;
  } else {
    const float invTauFs = 1.0f / (slideTimeSeconds * sampleRate);
    slideAlpha = 1.0f - std::exp(-invTauFs);
  }
}

void Voice::updateParameters(const VoiceState &newState) {
  state = newState;
  setGate(state.isGateHigh);
  filterFrequency = daisysp::fmap(state.filterCutoff, 250.0f, 8000.0f, daisysp::Mapping::EXP);
  applyEnvelopeParameters();
  updateOscillatorFrequencies();
}

// Voice Presets moved to src/voice/VoicePresets.cpp

void Voice::setSequencer(std::unique_ptr<Sequencer> seq) {
  sequencerOwned = std::move(seq);
  sequencer = sequencerOwned.get();
}

void Voice::setSequencer(Sequencer *seq) {
  sequencerOwned.reset();
  sequencer = seq;
}
