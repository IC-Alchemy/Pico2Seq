#include "Voice.h"
#include "../dsp/dsp.h"
#include "Arduino.h"
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

Voice::Voice(uint8_t id, const VoiceConfig &cfg)
    : voiceId(id), config(cfg), sampleRate(48000.0f), filterFrequency(1000.0f),
      gate(false),
      sequencer(nullptr)
{
  // Initialize frequency lookup table once in a thread-safe manner
  initFrequencyLookupTable();
  // Initialize cached detune multipliers
  recomputeDetuneMultipliers();

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
  if (slideTimeSeconds <= 0.0f)
  {
    slideAlpha = 1.0f; // instantaneous
  }
  else
  {
    const float invTauFs = 1.0f / (slideTimeSeconds * sampleRate);
    slideAlpha = 1.0f - std::exp(-invTauFs);
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

  // Initialize envelope
  envelope.Init(sampleRate);
  envelope.SetAttackTime(config.defaultAttack);
  envelope.SetDecayTime(config.defaultDecay);
  envelope.SetSustainLevel(config.defaultSustain);
  envelope.SetReleaseTime(config.defaultRelease);

  // Initialize effects
  if (config.hasOverdrive)
  {
    overdrive.Init();
    overdrive.SetDrive(config.overdriveDrive);
  }

  if (config.hasWavefolder)
  {
    wavefolder.Init();
    wavefolder.SetGain(config.wavefolderGain);
    wavefolder.SetOffset(config.wavefolderOffset);
  }

  // Update detune multipliers in case config changed before init
  recomputeDetuneMultipliers();
}

void Voice::setConfig(const VoiceConfig &cfg)
{
  config = cfg;

  // Resize oscillators if needed
  if (oscillators.size() != config.oscillatorCount)
  {
    oscillators.resize(config.oscillatorCount);
    // Initialize new oscillators
    for (size_t i = 0; i < oscillators.size(); i++)
    {
      oscillators[i].Init(sampleRate);
    }
  }

  // Update all components with new configuration
  init(sampleRate);
}

// Injected scale-data setters (defined out-of-line)
void Voice::setScaleTable(const int (*table)[48], size_t scaleCount)
{
  scaleTable = table;
  scaleTableCount = scaleCount;
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

  // 2) Filter cutoff update (uses envelope)
  updateFilter(envelopeValue);

  // 3) Oscillator/engine mixing (+ slide updates)
  float mixed = mixOscillators();

  // 4) Effects and gain shaping pre-filter
  applyEffects(mixed);

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
  // Update filter frequency with envelope modulation
  filter.SetFreq((filterFrequency * envelopeValue) +
                 (filterFrequency * .25f));
}

float Voice::mixOscillators()
{

  float mixedOscillators = 0.0f;

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
  // Apply effects chain
 if (config.hasOverdrive)
  {
    signal = overdrive.Process(signal) * config.overdriveGain;
  }

  if (config.hasWavefolder)
  {
    signal *= config.wavefolderGain;
    signal = wavefolder.Process(signal);
  }
  // Level adjustments removed from here; handled in finalizeOutput
}

inline float Voice::finalizeOutput(float signal, float envelopeValue) noexcept
{
  // Apply ladder filter
  float filteredSignal = filter.Process(signal);

  // Apply high-pass filter
  highPassFilter.Process(filteredSignal);

  // Get high-passed signal (no envelope or level applied here yet)
  float highPassedSignal = highPassFilter.High();

  // Apply envelope multiplication and final output scaling (including velocity-based level)
  float finalOutput = highPassedSignal * envelopeValue;
  finalOutput *= (config.outputLevel * ( state.velocityLevel));

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
  envelope.SetDecayTime(0.05f + (decay * 0.32f));
  envelope.SetReleaseTime(decay);
}

inline float Voice::calculateNoteFrequency(float note, int8_t octaveOffset,
                                    int harmony) noexcept
{
  // Clamp note to valid range with ARM-friendly integer operations
  const int noteIndex = std::max(0, std::min(static_cast<int>(note), static_cast<int>(SCALE_STEPS - 1)));

  // Resolve target index by advancing across UNIQUE scale degrees (not raw indices)
  // This fixes pentatonic and other scales with repeated entries in the SCALE_STEPS-step table.
  auto advanceDegrees = [](const int *row, int startIdx, int degreeOffset) -> int
  {
    int idx = startIdx;
    if (degreeOffset == 0)
      return idx;

    if (degreeOffset > 0)
    {
      int remaining = degreeOffset;
      int prev = row[idx];
      while (remaining > 0 && idx < static_cast<int>(SCALE_STEPS - 1))
      {
        ++idx;
        if (row[idx] != prev)
        {
          --remaining;
          prev = row[idx];
        }
      }
      return idx; // clamped at SCALE_STEPS-1 if we ran out
    }
    else // degreeOffset < 0
    {
      int remaining = -degreeOffset;
      int prev = row[idx];
      while (remaining > 0 && idx > 0)
      {
        --idx;
        if (row[idx] != prev)
        {
          --remaining;
          prev = row[idx];
        }
      }
      return idx; // clamped at 0 if we ran out
    }
  };

  int harmonyNoteIndex = noteIndex;

  // Resolve scale step to semitone offset using injected scale table if available
  int scaleSemitone; // chromatic fallback will set this below
  if (scaleTable && scaleTableCount > 0 && currentScalePtr)
  {
    const uint8_t idx = *currentScalePtr;
    if (idx < scaleTableCount)
    {
      const int *row = scaleTable[idx];
      // Advance by harmony degrees across UNIQUE semitone changes in the row
      harmonyNoteIndex = advanceDegrees(row, noteIndex, harmony);
      // Clamp defensively (advanceDegrees already clamps but keep safe)
      harmonyNoteIndex = std::max(0, std::min(harmonyNoteIndex, static_cast<int>(SCALE_STEPS - 1)));
      scaleSemitone = row[harmonyNoteIndex];
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
    harmonyNoteIndex = std::max(0, std::min(noteIndex + harmony, static_cast<int>(SCALE_STEPS - 1)));
    scaleSemitone = harmonyNoteIndex;
  }

  // Base MIDI mapping: center around 48 as before (C3-ish) then add octave offset
  int midiNote = scaleSemitone + 48 + static_cast<int>(octaveOffset);

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