#pragma once

#include "VoiceOscillator.h"
#include <cstdint>

// Sound-generation engine selected by VoiceConfig::engine. Engines share the
// source -> effects -> velocity -> output chain; drone build: no per-voice
// filter or amplitude envelope. Only the source stage (and, for the noise
// engine, the pre-output effect inserts) differs.
enum VoiceEngine : uint8_t
{
  ENGINE_OSC = 0,       // Up to 3 oscillators (or raw noise when oscillatorCount == 0)
  ENGINE_WAVEGUIDE = 1, // Karplus-Strong plucked string (rpdsp::PluckedStringVoice)
  ENGINE_NOISEFX = 2,   // Noise + chaos source through diffuser/swarm inserts
  ENGINE_HYPERSAW = 3,  // One rpdsp::Hypersaw (internally seven detuned saw voices)
  ENGINE_RECIPE = 4,    // Fixed-state rpdsp recipe selected by config.recipe
};

// How the sequencer's parameter slots are interpreted for this voice. STANDARD
// leaves Velocity as the only active shaping lane (drone voices carry no
// filter/envelope lanes); the alternates re-purpose selected slots for
// engine-specific macros (routed in Voice::applyParameters_(), named via
// VoicePresets::getSequencerParamName).
enum VoiceParamSet : uint8_t
{
  PARAMSET_STANDARD = 0,
  PARAMSET_WAVEGUIDE = 1,
  PARAMSET_HYPERSAW = 2,
  PARAMSET_NOISESTORM = 3,
  PARAMSET_HARDSYNC = 4,
};

// Legacy main-filter topology value persisted in saved patches. The drone
// build removed the main filter; the enum survives as the field's storage type.
enum VoiceFilterType : uint8_t
{
  FILTER_LADDER = 0,
  FILTER_SVF = 1,
};

// Filter modes retained as the legacy persisted field type. The drone build
// removed the main filter; only the enum values survive inside saved patches.
enum class VoiceFilterMode : uint8_t
{
  LP24 = 0,
  LP12,
  BP24,
  BP12,
  HP24,
  HP12,
};

/**
 * @brief Configuration structure for a voice
 *
 * Defines the characteristics and behavior of a synthesizer voice including
 * oscillator settings, filter parameters, effects chain, and envelope configuration.
 */
struct VoiceParameterLayout;
struct VoiceRecipe;

struct VoiceConfig
{
  // Patch bases live on the control core. Recorded lanes contain modifiers;
  // only the composed playback state is sent to the audio core.
  bool usePatchBases = false; // Enabled by firmware; legacy library clients opt in.
  float baseNote = 0.0f; // Additive transpose in scale steps
  float baseVelocity = 0.5f;
  float baseOctave = 0.0f; // semitones, quantized to octaves
  float baseGateLength = 0.5f; // Half a sixteenth note; 60 PPQN ticks
  bool baseGate = true;
  bool baseSlide = false;
  float slideSeconds = 0.06f;

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

  // Which sequencer parameter slots this voice re-purposes (VoiceParamSet).
  uint8_t paramSet = PARAMSET_STANDARD;

  // Optional immutable descriptors in flash. Must outlive this Voice and all
  // queued configs (normally inline constexpr definitions in preset headers).
  const VoiceParameterLayout *parameters = nullptr; // null selects legacy paramSet
  const VoiceRecipe *recipe = nullptr;              // ENGINE_RECIPE source
  float macro1 = 0.5f;
  float macro2 = 0.5f;
  float macro3 = 0.5f;
  float fmModFeedback = 0.0f;
  float phaseTriangleFold = 0.0f;
  float spectralSubRatio = 0.5f;
  float spectralSubShape = 0.0f;
  float prismDriftChaos = 0.65f;
  bool recipeRetrigger = true;
  float noiseSourceLevel = 1.0f;
  float noiseChaosRate = 1.0f;

  // Waveguide engine parameters (ENGINE_WAVEGUIDE only)
  float wgT60 = 2.5f;          // String tail T60 in seconds (0.05-10.0)
  float wgBrightness = 0.7f;   // Loop damping: 0 dark nylon .. 1 glassy (0.0-1.0)
  float wgPickPosition = 0.25f; // Pick point on string, 0.02 bridge .. 0.5 middle
  float wgPickHardness = 0.8f; // Excitation burst: 0 soft felt .. 1 hard pick
  float wgStiffness = 0.0f;    // Inharmonic dispersion: 0 harmonic .. 1 bell-like
  float wgDetune = 6.0f;       // Two-string course spread in cents (0.0-30.0)

  // Hypersaw engine parameters (ENGINE_HYPERSAW only)
  float hypersawDetune = 0.2f; // Seven-voice detune amount (0.0-1.0)
  float hypersawMix = 0.5f;    // Mix amount for the hypersaw layers (0.0-1.0)

  // Noise-FX engine parameters (ENGINE_NOISEFX only)
  float noiseDiffuseSize = 0.8f; // Prime-tap diffuser smear (0.0-1.0)
  float noiseDiffuseMix = 0.7f;  // Diffuser wet amount (0.0-1.0)
  float noiseSwarmColor = 0.5f;  // Allpass swarm tone (0.0-1.0)
  float noiseSwarmRegen = 0.9f;  // Allpass swarm regeneration (0.0-1.2)
  float noiseChaosLevel = 0.35f; // Pitch-tracked chaos_lorenz growl mix (0.0-1.0)

  // Legacy main-filter and envelope settings. The drone build removed the
  // main filter and the amplitude envelope from the audio path; these fields
  // survive only so PatchCodec keeps round-tripping saved patches unchanged
  // (the binary PatchSnapshot layout is locked). Nothing in the firmware
  // reads them for sound.
  uint8_t filterType = FILTER_LADDER; // Legacy: main filter topology (VoiceFilterType)
  float filterRes = 0.2f;            // Legacy: filter resonance (0.0-1.0)
  float filterDrive = 1.8f;          // Legacy: ladder drive amount
  float filterPassbandGain = 0.23f;  // Legacy: ladder passband gain compensation
  VoiceFilterMode filterMode = VoiceFilterMode::LP24; // Legacy: filter response
  float filterCutoffBase = 0.37f;    // Legacy: normalized static cutoff
  float filterEnvelopeAmount = 1.0f; // Legacy: envelope-to-cutoff depth
  float filterEnvelopeFloor = 0.1f;  // Legacy: envelope-to-cutoff floor

  // High-pass filter settings. Rendered by the waveguide engines only
  // (sub-shedding for the Karplus tails); inert for every other engine.
  float highPassFreq = 80.0f; // High-pass cutoff frequency in Hz (20.0-20000.0)
  float highPassRes = 0.1f;   // High-pass resonance (0.0-1.0)

  // Effects chain configuration
  bool hasOverdrive = false;     // Enable overdrive effect
  bool hasEnvelope = true;       // Legacy flag: drones ignore it (patch format compatibility)
  bool hasFilter = true;         // Legacy flag: drones ignore it (patch format compatibility)
  float overdriveGain = 0.34f;   // Overdrive output gain (0.0-2.0)
  float overdriveDrive = 0.25f;  // Overdrive drive amount (0.0-1.0)

  // Legacy envelope defaults. Inert since the drone build removed the ADSR;
  // retained for the locked PatchSnapshot layout.
  float defaultAttack = 0.04f; // Default attack time in seconds (0.001-10.0)
  float defaultDecay = 0.14f;  // Default decay time in seconds (0.001-10.0)
  float defaultSustain = 0.5f; // Default sustain level (0.0-1.0)
  float defaultRelease = 0.2f; // Default release time in seconds (0.001-10.0)

  // Voice mixing
  float outputLevel = 0.6f; // Voice output level (0.0-1.0)
  bool enabled = true;      // Voice enabled state
};
