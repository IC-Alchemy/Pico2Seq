#ifndef SEQUENCER_DEFS_H
#define SEQUENCER_DEFS_H

#include <stdint.h>
#include <variant> // Required for std::variant

// SequencerDefs: shared types for the polymetric step sequencer (notes to gates).
// Each ParamId owns an independent ParameterTrack<N> with its own loop length.
// Portable C++ — no Arduino/hardware includes here.
namespace SequencerConstants
{
  // Clock: 480 PPQN, one 16th step per 120 ticks.
  static constexpr uint16_t PULSES_PER_QUARTER_NOTE_PPQN = 480;
  static constexpr uint8_t PULSES_PER_SEQUENCER_STEP_TICKS = PULSES_PER_QUARTER_NOTE_PPQN / 4;

  // Loop lengths: 2..64 steps per lane, 16 on boot.
  static constexpr uint8_t MAX_STEPS_COUNT = 64;
  static constexpr uint8_t MIN_STEPS_COUNT = 2;
  static constexpr uint8_t DEFAULT_STEPS_COUNT = 16;

  // Note lane holds scale degrees 0..36 (three chromatic octaves of steps).
  static constexpr int NOTE_PARAMETER_MIN = 0;
  static constexpr int NOTE_PARAMETER_MAX = 36;

  // Gate length in ticks: how long a step holds its note (staccato..legato).
  static constexpr uint16_t DEFAULT_GATE_LENGTH_TICKS = PULSES_PER_SEQUENCER_STEP_TICKS / 2;
  static constexpr uint16_t MIN_GATE_LENGTH_TICKS = 1;
  static constexpr uint16_t MAX_GATE_LENGTH_TICKS = PULSES_PER_SEQUENCER_STEP_TICKS;

  // Hand-distance zones for the Octave lane: nearer = lower octave.
  static constexpr float SENSOR_MIN_DISTANCE_MM = 55.0f;
  static constexpr float SENSOR_MAX_DISTANCE_MM = 700.0f;
  static constexpr float SENSOR_SPAN_MM = SENSOR_MAX_DISTANCE_MM - SENSOR_MIN_DISTANCE_MM;

  // Normalized equivalents of the zones above, derived from the sensor span.
  static constexpr float OCTAVE_ZONE_MINUS_2_MAX_MM = 90.0f;
  static constexpr float OCTAVE_ZONE_MINUS_1_MAX_MM = 220.0f;
  static constexpr float OCTAVE_ZONE_ZERO_MAX_MM    = 355.0f;
  static constexpr float OCTAVE_ZONE_PLUS_1_MAX_MM  = 550.0f;

  static constexpr float OCTAVE_NORM_MINUS_2_MAX = (OCTAVE_ZONE_MINUS_2_MAX_MM - SENSOR_MIN_DISTANCE_MM) / SENSOR_SPAN_MM;
  static constexpr float OCTAVE_NORM_MINUS_1_MAX = (OCTAVE_ZONE_MINUS_1_MAX_MM - SENSOR_MIN_DISTANCE_MM) / SENSOR_SPAN_MM;
  static constexpr float OCTAVE_NORM_ZERO_MAX    = (OCTAVE_ZONE_ZERO_MAX_MM - SENSOR_MIN_DISTANCE_MM) / SENSOR_SPAN_MM;
  static constexpr float OCTAVE_NORM_PLUS_1_MAX  = (OCTAVE_ZONE_PLUS_1_MAX_MM - SENSOR_MIN_DISTANCE_MM) / SENSOR_SPAN_MM;

  // Discrete lane detents for -2..+2 octaves; 0.5 is concert pitch.
  static constexpr float OCTAVE_TRACK_MINUS_2 = 0.0f;
  static constexpr float OCTAVE_TRACK_MINUS_1 = 0.25f;
  static constexpr float OCTAVE_TRACK_ZERO    = 0.5f;
  static constexpr float OCTAVE_TRACK_PLUS_1  = 0.75f;
  static constexpr float OCTAVE_TRACK_PLUS_2  = 1.0f;

  // Stored in a patch-default lane (ParameterDefinition::patchDefault) when
  // the step has no value of its own: playback uses the voice's patch value.
  // Only an exact write of this constant stores it; other values clamp.
  static constexpr float LANE_FOLLOWS_PATCH = -1.0f;
}

// Old names kept for callers; prefer SequencerConstants::* in new code.
constexpr uint16_t PULSES_PER_QUARTER_NOTE = SequencerConstants::PULSES_PER_QUARTER_NOTE_PPQN;
constexpr uint8_t PULSES_PER_SEQUENCER_STEP = SequencerConstants::PULSES_PER_SEQUENCER_STEP_TICKS;
constexpr uint8_t SEQUENCER_MAX_STEPS = SequencerConstants::MAX_STEPS_COUNT;
constexpr uint8_t MIN_STEPS = SequencerConstants::MIN_STEPS_COUNT;
constexpr uint8_t DEFAULT_STEPS = SequencerConstants::DEFAULT_STEPS_COUNT;

/**
 * @brief One automatable lane per step (order must match CORE_PARAMETERS).
 *
 * Musically: Note/Velocity/Gate shape the phrase, Filter/ADSR shape the tone,
 * Octave/GateLength/Slide shape articulation across steps.
 */
enum class ParamId : uint8_t
{
  Note,       // 0 - Scale step index (0-36, maps to SCALE_STEPS array)
  Velocity,   // 1 - Voice amplitude (0.0-1.0)
  Filter,     // 2 - Filter cutoff frequency (0.0-1.0)
  Attack,     // 3 - Envelope attack time (0.0-1.0 seconds)
  Decay,      // 4 - Envelope decay time (0.0-1.0 seconds)
  Octave,     // 5 - Normalized octave control, mapped to -12/0/+12 semitones
  GateLength, // 6 - Gate duration (0.001-1.0 as fraction of step)
  Gate,       // 7 - Gate on/off state (boolean)
  Slide,      // 8 - Portamento enable (boolean)
  Sustain,    // 9 - Envelope sustain level (0.0-1.0)
  Release,    // 10 - Envelope release time (0.0-1.0 normalized)
  Count       // Total parameter count for array sizing
};

// Array sizing helper; ParamId::Count is the lane count, not a lane.
constexpr uint8_t PARAM_ID_COUNT = static_cast<uint8_t>(ParamId::Count);

/**
 * @brief Which lane the magnetic encoder plays live (moved here to avoid
 * a UI <-> sequencer include cycle).
 */
enum class EncoderParameterMode : uint8_t
{
  Velocity = 0,      // Voice amplitude control
  Filter = 1,        // Filter cutoff control
  Attack = 2,        // Envelope attack time control
  Release = 3,       // Envelope release time control (was Decay until the
                     // 5th record button was remapped; slot kept so a saved
                     // session's encoder target still resolves)
  Note = 4,          // Note/pitch control
  Octave = 5,        // Octave offset control
  SlideTime = 6,     // Portamento/slide time control
  COUNT = 7          // Total mode count
};

/**
 * @brief Live encoder offsets added around the sequencer value at play time.
 * Lets the player bend pitch/filter/etc. around the pattern without rewriting it.
 */
struct EncoderBaseValues
{
  // Normalized bipolar offsets combined with the step value at play time.
  float note = 0.0f;          // Base note/pitch offset (normalized 0.0-1.0 domain)
  float velocity = 0.0f;      // Base velocity (0.0-1.0)
  float filter = 0.0f;        // Base filter cutoff (0.0-1.0)
  float attack = 0.0f;        // Base attack time (0.0-1.0 seconds)
  float decay = 0.0f;         // Base decay time (0.0-1.0 seconds)
  float octave = 0.0f;        // Base octave offset (normalized 0.0-1.0 domain)
  float slideTime = 0.0f;     // Slide time in seconds for voice glide
};

// Single-voice encoder bases; subtype exists for UI clarity, adds no state.
struct EncoderBaseValuesVoice1 : public EncoderBaseValues
{
  // No additional members
};
// Which lanes the step-edit buttons currently target (UI-owned, read here).
struct StepEditButtons
{
  bool note;     // Note parameter edit button state
  bool velocity; // Velocity parameter edit button state
  bool filter;   // Filter parameter edit button state
  bool attack;   // Attack parameter edit button state
  bool decay;    // Decay parameter edit button state
  bool octave;   // Octave parameter edit button state
};

// Fixed-size lane storage, now from rpdsp (see src/rpdsp/src/rpdsp/parameter_track.h).
// Keep portable: include by relative path (no -I wiring in the Arduino build).
// Two deliberate differences from the float-only track that used to live here:
//   1. init() defaults to 64 steps — callers pass CORE_PARAMETERS[i].defaultSteps.
//   2. resize() clamps into [1, MaxSteps] instead of ignoring out-of-range input.
#include "../../rpdsp/src/rpdsp/parameter_track.h"

template <uint8_t MAX_SIZE>
using ParameterTrack = rpdsp::ParameterTrack<float, MAX_SIZE>;

// int/float/bool folded into the float domain the tracks store.
using ParameterValueType = std::variant<int, float, bool>;

// How a lane responds to the encoder: smooth sweep, detented step, or on/off.
// Octave stays a normalized float lane so the distance sensor can record it.
enum class ParameterEditKind : uint8_t
{
  Continuous,
  Stepped,
  Toggle
};

/**
 * @brief Per-lane metadata: musical range, edit feel, and patch behavior.
 * Array order must match ParamId. Future editors: keep defaultSteps at 16
 * unless a lane needs a different polymetric default.
 */
struct ParameterDefinition
{
  const char *name;                // Display name for UI
  ParameterValueType defaultValue; // Default parameter value
  ParameterValueType minValue;     // Minimum allowed value
  ParameterValueType maxValue;     // Maximum allowed value
  ParameterEditKind editKind;      // Continuous, detented, or binary toggle
  uint8_t defaultSteps;            // Default number of steps for this parameter
  bool recordable;                 // Has a parameter-button live-record control;
                                   // not a restriction on explicit step/fader edits
  EncoderParameterMode encoderMode; // COUNT when there is no encoder base target
  // Absolute lane: a step holds its own normalized value, or
  // LANE_FOLLOWS_PATCH to play the voice's patch value. Other lanes are
  // offsets (Note, Octave, GateLength) or toggles.
  bool patchDefault;
};

/**
 * @brief Lane metadata table. SlideTime is voice-only (no Slide-toggle entry).
 */
constexpr ParameterDefinition CORE_PARAMETERS[] = {
    // Name, default, min, max, edit kind, steps, recordable, encoder target, patch default
    {"Note", 0, SequencerConstants::NOTE_PARAMETER_MIN, SequencerConstants::NOTE_PARAMETER_MAX,
     ParameterEditKind::Stepped, SequencerConstants::DEFAULT_STEPS_COUNT, true, EncoderParameterMode::Note, false},
    {"Velocity", 0.5f, 0.0f, 1.0f, ParameterEditKind::Continuous, SequencerConstants::DEFAULT_STEPS_COUNT, true, EncoderParameterMode::Velocity, true},
    {"Filter", 0.5f, 0.0f, 1.0f, ParameterEditKind::Continuous, SequencerConstants::DEFAULT_STEPS_COUNT, true, EncoderParameterMode::Filter, true},
    {"Attack", 0.01f, 0.0f, 1.0f, ParameterEditKind::Continuous, SequencerConstants::DEFAULT_STEPS_COUNT, true, EncoderParameterMode::Attack, true},
    // Decay has no record button: on many presets it is a timbre control, not an
    // envelope stage — reach it per step with the ENV-mode faders.
    {"Decay", 0.3f, 0.0f, 1.0f, ParameterEditKind::Continuous, SequencerConstants::DEFAULT_STEPS_COUNT, false, EncoderParameterMode::COUNT, true},
    {"Octave", 0.5f, 0.0f, 1.0f, ParameterEditKind::Stepped, SequencerConstants::DEFAULT_STEPS_COUNT, true, EncoderParameterMode::Octave, false},
    {"GateLength", 0.8f, 0.1f, 1.0f, ParameterEditKind::Continuous, SequencerConstants::DEFAULT_STEPS_COUNT, false, EncoderParameterMode::COUNT, false},
    {"Gate", false, false, true, ParameterEditKind::Toggle, SequencerConstants::DEFAULT_STEPS_COUNT, false, EncoderParameterMode::COUNT, false},
    {"Slide", false, false, true, ParameterEditKind::Toggle, SequencerConstants::DEFAULT_STEPS_COUNT, false, EncoderParameterMode::COUNT, false},
    // Sustain is edited per step by the ENV-mode faders only.
    {"Sustain", 0.5f, 0.0f, 1.0f, ParameterEditKind::Continuous, SequencerConstants::DEFAULT_STEPS_COUNT, false, EncoderParameterMode::COUNT, true},
    // Release owns the 5th record button and the encoder base that Decay had.
    // It reaches the envelope on every preset, so it is what shapes how long a
    // step rings - up to 10 s, enough for one downbeat note to cover 16 steps.
    {"Release", 0.4f, 0.0f, 1.0f, ParameterEditKind::Continuous, SequencerConstants::DEFAULT_STEPS_COUNT, true, EncoderParameterMode::Release, true}
};

static_assert(sizeof(CORE_PARAMETERS) / sizeof(CORE_PARAMETERS[0]) == PARAM_ID_COUNT,
              "Every ParamId must have a descriptor");

// nullptr on invalid id — callers must not fall through to another lane.
constexpr const ParameterDefinition *parameterDefinition(ParamId id) noexcept
{
  const auto index = static_cast<uint8_t>(id);
  return index < PARAM_ID_COUNT ? &CORE_PARAMETERS[index] : nullptr;
}

constexpr bool isPatchDefaultLane(ParamId id) noexcept
{
  const auto *definition = parameterDefinition(id);
  return definition && definition->patchDefault;
}

constexpr bool followsPatch(float stored) noexcept
{
  return stored < 0.0f;
}

/**
 * Map an offset lane (0..1) around a patch base: 0.5 plays the base, 0/1 reach
 * the lane ends. Used for old-session conversion and humanize spread.
 */
constexpr float offsetAroundBase(float base, float offset) noexcept
{
  const float n = offset < 0.0f ? 0.0f : (offset > 1.0f ? 1.0f : offset);
  const float b = base < 0.0f ? 0.0f : (base > 1.0f ? 1.0f : base);
  const float value = n >= 0.5f ? b + (n - 0.5f) * 2.0f * (1.0f - b)
                                : b + (n - 0.5f) * 2.0f * b;
  return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

constexpr ParamId parameterForEncoderMode(EncoderParameterMode mode) noexcept
{
  if (mode == EncoderParameterMode::COUNT)
    return ParamId::Count;
  for (uint8_t i = 0; i < PARAM_ID_COUNT; ++i)
    if (CORE_PARAMETERS[i].encoderMode == mode)
      return static_cast<ParamId>(i);
  return ParamId::Count;
}

/**
 * @brief Per-step voice output: what the audio engine plays for one step.
 * Gate off keeps the previous pitch so the tail rings instead of jumping.
 * Ranges: noteIndex 0-36, levels 0.0-1.0, octaveOffset in semitones,
 * gateLengthTicks 1..PULSES_PER_SEQUENCER_STEP.
 */
struct VoiceState
{
  float noteIndex = 0.0f; // Scale degree 0-36 for the scale-table lookup
  // Centered default doubles as zero slave-frequency offset on hard-sync presets.
  float velocityLevel = 0.5f;
  float filterCutoff = 0.37f; // Brightness 0.0-1.0 (dark..open)
  float attackTimeSeconds = 0.01f; // Pluck-like default; higher softens the front
  float decayTimeSeconds = 0.1f; // Time to fall toward sustain
  float sustainLevel = 0.5f; // Held level while the gate stays high
  float releaseTimeSeconds = 0.3f; // Ring-out after gate off (normalized)
  int8_t octaveOffset = 0; // Transpose in semitones from the Octave lane
  uint16_t gateLengthTicks = SequencerConstants::DEFAULT_GATE_LENGTH_TICKS; // Hold time; short = staccato

  // Silence until a gated step drives it high.
  bool isGateHigh = false;      // Voice on/off state
  bool hasSlide = false;        // Portamento enable flag
  bool shouldRetrigger = false; // Envelope restart command flag
};

/**
 * @brief Stored step: same fields as VoiceState, decoded from lane values.
 * Used for previews and UI reads — playback mapping, never transport state.
 */
struct Step
{
  float noteIndex = 0.0f; // Scale degree 0-36
  float velocityLevel = 0.5f; // Loudness 0.0-1.0
  float filterCutoff = 0.5f; // Brightness 0.0-1.0
  float attackTimeSeconds = 0.01f; // Envelope attack
  float decayTimeSeconds = 0.2f; // Envelope decay
  float sustainLevel = 0.5f; // Held level
  float releaseTimeSeconds = 0.3f; // Ring-out (normalized)
  int8_t octaveOffset = 0; // Transpose in semitones
  uint16_t gateLengthTicks = SequencerConstants::DEFAULT_GATE_LENGTH_TICKS; // Hold time
  bool isGateActive = false; // Sounds (true) or rests (false)
  bool hasSlide = false; // Glide into this step
};

// --- Utilities (defined in Sequencer.cpp) ---
float mapNormalizedValueToParamRange(ParamId id, float normalizedValue);
// Fold an int/float/bool lane default into the float domain the tracks store.
float parameterValueAsFloat(const ParameterValueType &value);

#endif // SEQUENCER_DEFS_H
