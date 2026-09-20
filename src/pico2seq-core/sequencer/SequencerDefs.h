#ifndef SEQUENCER_DEFS_H
#define SEQUENCER_DEFS_H

#include <stdint.h>
#include <variant> // Required for std::variant

/**
 * @brief Sequencer timing and configuration constants
 *
 * Centralized namespace for all sequencer-related timing constants and limits.
 * All timing values are specified with clear unit indicators for maintainability.
 */
namespace SequencerConstants
{
  // Timing constants with unit indicators
  static constexpr uint16_t PULSES_PER_QUARTER_NOTE_PPQN = 480;
  static constexpr uint8_t PULSES_PER_SEQUENCER_STEP_TICKS = PULSES_PER_QUARTER_NOTE_PPQN / 4;

  // Step count limits
  static constexpr uint8_t MAX_STEPS_COUNT = 64;
  static constexpr uint8_t MIN_STEPS_COUNT = 2;
  static constexpr uint8_t DEFAULT_STEPS_COUNT = 16;

  // Three-octave chromatic C-to-C span for the Note parameter track.
  static constexpr int NOTE_PARAMETER_MIN = 0;
  static constexpr int NOTE_PARAMETER_MAX = 36;

  // Gate timing constants
  static constexpr uint16_t DEFAULT_GATE_LENGTH_TICKS = PULSES_PER_SEQUENCER_STEP_TICKS / 2;
  static constexpr uint16_t MIN_GATE_LENGTH_TICKS = 1;
  static constexpr uint16_t MAX_GATE_LENGTH_TICKS = PULSES_PER_SEQUENCER_STEP_TICKS;

  // Distance thresholds (in millimeters) and normalized ranges for Octave parameter track
  static constexpr float SENSOR_MIN_DISTANCE_MM = 55.0f;
  static constexpr float SENSOR_MAX_DISTANCE_MM = 700.0f;
  static constexpr float SENSOR_SPAN_MM = SENSOR_MAX_DISTANCE_MM - SENSOR_MIN_DISTANCE_MM;

  static constexpr float OCTAVE_ZONE_MINUS_2_MAX_MM = 90.0f;
  static constexpr float OCTAVE_ZONE_MINUS_1_MAX_MM = 280.0f;
  static constexpr float OCTAVE_ZONE_ZERO_MAX_MM    = 425.0f;
  static constexpr float OCTAVE_ZONE_PLUS_1_MAX_MM  = 550.0f;

  static constexpr float OCTAVE_NORM_MINUS_2_MAX = (OCTAVE_ZONE_MINUS_2_MAX_MM - SENSOR_MIN_DISTANCE_MM) / SENSOR_SPAN_MM;
  static constexpr float OCTAVE_NORM_MINUS_1_MAX = (OCTAVE_ZONE_MINUS_1_MAX_MM - SENSOR_MIN_DISTANCE_MM) / SENSOR_SPAN_MM;
  static constexpr float OCTAVE_NORM_ZERO_MAX    = (OCTAVE_ZONE_ZERO_MAX_MM - SENSOR_MIN_DISTANCE_MM) / SENSOR_SPAN_MM;
  static constexpr float OCTAVE_NORM_PLUS_1_MAX  = (OCTAVE_ZONE_PLUS_1_MAX_MM - SENSOR_MIN_DISTANCE_MM) / SENSOR_SPAN_MM;

  // Stored track discrete values corresponding to -2, -1, 0, +1, +2 octaves
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

// Legacy constants for backward compatibility - will be phased out
constexpr uint16_t PULSES_PER_QUARTER_NOTE = SequencerConstants::PULSES_PER_QUARTER_NOTE_PPQN;
constexpr uint8_t PULSES_PER_SEQUENCER_STEP = SequencerConstants::PULSES_PER_SEQUENCER_STEP_TICKS;
constexpr uint8_t SEQUENCER_MAX_STEPS = SequencerConstants::MAX_STEPS_COUNT;
constexpr uint8_t MIN_STEPS = SequencerConstants::MIN_STEPS_COUNT;
constexpr uint8_t DEFAULT_STEPS = SequencerConstants::DEFAULT_STEPS_COUNT;

/**
 * @brief Parameter identifiers for sequencer step automation
 *
 * Defines all automatable parameters for each sequencer step. These IDs must
 * match the order of the CORE_PARAMETERS array for proper parameter mapping.
 * Each parameter has specific value ranges documented in CORE_PARAMETERS.
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

// Constant for array sizing based on ParamId::Count
constexpr uint8_t PARAM_ID_COUNT = static_cast<uint8_t>(ParamId::Count);

/**
 * @brief Magnetic encoder parameter cycling modes
 *
 * Defines which parameter the magnetic encoder controls in real-time.
 * Moved from UIEventHandler.h to break circular dependency.
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
 * @brief Magnetic encoder base parameter values for bidirectional control
 *
 * Stores base values for each parameter that can be controlled by the
 * magnetic encoder. Supports bidirectional control by maintaining center points.
 */
struct EncoderBaseValues
{
  // These bases are normalized bipolar offsets. They are combined with the
  // sequencer value by shiftAndScale() when a step is played.
  float note = 0.0f;          // Base note/pitch offset (normalized 0.0-1.0 domain)
  float velocity = 0.0f;      // Base velocity (0.0-1.0)
  float filter = 0.0f;        // Base filter cutoff (0.0-1.0)
  float attack = 0.0f;        // Base attack time (0.0-1.0 seconds)
  float decay = 0.0f;         // Base decay time (0.0-1.0 seconds)
  float octave = 0.0f;        // Base octave offset (normalized 0.0-1.0 domain)
  float slideTime = 0.0f;     // Slide time in seconds for voice glide
};

/**
 * @brief Voice-specific encoder base values
 *
 * Inherits from EncoderBaseValues with no additional members.
 */
struct EncoderBaseValuesVoice1 : public EncoderBaseValues
{
  // No additional members
};
/**
 * @brief Step parameter edit button state tracking
 *
 * Tracks which parameter edit buttons are currently pressed for step editing.
 * Used by the UI system to determine which parameter to modify when editing steps.
 */
struct StepEditButtons
{
  bool note;     // Note parameter edit button state
  bool velocity; // Velocity parameter edit button state
  bool filter;   // Filter parameter edit button state
  bool attack;   // Attack parameter edit button state
  bool decay;    // Decay parameter edit button state
  bool octave;   // Octave parameter edit button state
};

// Fixed-size parameter automation track, now sourced from rpdsp instead of
// being defined locally -- see src/rpdsp/src/rpdsp/parameter_track.h.
//
// Behavior is preserved with two deliberate exceptions vs. the original
// float-only ParameterTrack<MAX_SIZE> struct that used to live here:
//   1. init(defaultValue) alone no longer implies "start at
//      SequencerConstants::DEFAULT_STEPS_COUNT steps" -- the rpdsp version
//      defaults its second (stepCount) argument to MaxSteps. Call sites that
//      relied on the old hardcoded 16-step default (ParameterManager::init)
//      now pass CORE_PARAMETERS[i].defaultSteps explicitly instead.
//   2. resize() now clamps out-of-range requests into [1, MaxSteps] instead
//      of silently no-op'ing when newStepCount is outside
//      [MIN_STEPS_COUNT, MAX_SIZE]. This matches the always-clamp convention
//      rpdsp already uses in GatePattern/RhythmGateSequencer. In practice all
//      existing call sites already pass validated in-range values, so this
//      only changes behavior for out-of-range inputs that weren't hit before.
//
// Included via a relative path (not <rpdsp/parameter_track.h>) because the
// Arduino firmware build has no dedicated -I wiring for bundled modules --
// it resolves src/pico2seq-core's own includes the same way (see
// Pico2Seq.ino and src/voice/Voice.h), relying on quoted-include relative
// resolution instead of a configured include path.
#include "../../rpdsp/src/rpdsp/parameter_track.h"

template <uint8_t MAX_SIZE>
using ParameterTrack = rpdsp::ParameterTrack<float, MAX_SIZE>;

// Define the variant type for parameter values that can be int, float, or bool
using ParameterValueType = std::variant<int, float, bool>;

// Step editing behavior, independent of voice/recipe bindings. Stepped lanes
// use encoder detents; their storage remains governed by the value/range types.
// In particular Octave retains a normalized float lane for sensor recording.
enum class ParameterEditKind : uint8_t
{
  Continuous,
  Stepped,
  Toggle
};

/**
 * @brief Parameter definition with metadata and constraints
 *
 * Defines the characteristics and valid ranges for each sequencer parameter.
 * Used for validation, UI display, and parameter initialization.
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
 * @brief Core parameter definitions array
 *
 * Defines metadata for all sequencer parameters. Array order MUST match ParamId enum.
 * Includes live-record eligibility and the encoder base target. SlideTime is
 * a voice-only control, not the Slide toggle lane, so it has no entry here.
 * Uses SequencerConstants for consistent step count defaults.
 */
constexpr ParameterDefinition CORE_PARAMETERS[] = {
    // Name, default, min, max, edit kind, steps, recordable, encoder base target, patch default
    {"Note", 0, SequencerConstants::NOTE_PARAMETER_MIN, SequencerConstants::NOTE_PARAMETER_MAX,
     ParameterEditKind::Stepped, SequencerConstants::DEFAULT_STEPS_COUNT, true, EncoderParameterMode::Note, false},
    {"Velocity", 0.5f, 0.0f, 1.0f, ParameterEditKind::Continuous, SequencerConstants::DEFAULT_STEPS_COUNT, true, EncoderParameterMode::Velocity, true},
    {"Filter", 0.5f, 0.0f, 1.0f, ParameterEditKind::Continuous, SequencerConstants::DEFAULT_STEPS_COUNT, true, EncoderParameterMode::Filter, true},
    {"Attack", 0.01f, 0.0f, 1.0f, ParameterEditKind::Continuous, SequencerConstants::DEFAULT_STEPS_COUNT, true, EncoderParameterMode::Attack, true},
    // Decay has no record button: on 20 of the 29 presets the Decay lane is a
    // timbre control, not an envelope stage (VoiceParameterLayout::envelopeFromTracks),
    // so the button did nothing there. Reach it per step with the ENV-mode faders.
    {"Decay", 0.3f, 0.0f, 1.0f, ParameterEditKind::Continuous, SequencerConstants::DEFAULT_STEPS_COUNT, false, EncoderParameterMode::COUNT, true},
    {"Octave", 0.5f, 0.0f, 1.0f, ParameterEditKind::Stepped, SequencerConstants::DEFAULT_STEPS_COUNT, true, EncoderParameterMode::Octave, false},
    {"GateLength", 0.5f, 0.001f, 1.0f, ParameterEditKind::Continuous, SequencerConstants::DEFAULT_STEPS_COUNT, false, EncoderParameterMode::COUNT, false},
    {"Gate", false, false, true, ParameterEditKind::Toggle, SequencerConstants::DEFAULT_STEPS_COUNT, false, EncoderParameterMode::COUNT, false},
    {"Slide", false, false, true, ParameterEditKind::Toggle, SequencerConstants::DEFAULT_STEPS_COUNT, false, EncoderParameterMode::COUNT, false},
    // Sustain is edited per step by the ENV-mode faders only.
    {"Sustain", 0.5f, 0.0f, 1.0f, ParameterEditKind::Continuous, SequencerConstants::DEFAULT_STEPS_COUNT, false, EncoderParameterMode::COUNT, true},
    // Release owns the 5th record button and the encoder base that Decay had.
    // It reaches the envelope on every preset, so it is what shapes how long a
    // step rings - up to 10 s, enough for one downbeat note to cover 16 steps.
    {"Release", 0.3f, 0.0f, 1.0f, ParameterEditKind::Continuous, SequencerConstants::DEFAULT_STEPS_COUNT, true, EncoderParameterMode::Release, true}
};

static_assert(sizeof(CORE_PARAMETERS) / sizeof(CORE_PARAMETERS[0]) == PARAM_ID_COUNT,
              "Every ParamId must have a descriptor");

// Invalid IDs do not silently select a different parameter.
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
 * Offset-to-absolute mapping of the former modifier lanes: 0.5 plays the
 * base, 0 and 1 reach the ends of the lane. Used to convert old sessions and
 * to spread Randomize around a patch value.
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
 * @brief Voice synthesis parameters for audio output
 *
 * Contains all parameters needed by the audio synthesis engine for a single voice.
 * This struct is passed from the sequencer to the audio processing system.
 * Variable names include unit indicators and clear purpose descriptions.
 *
 * Member Ranges:
 * - noteIndex: 0-36 (integral scale step index for SCALE_STEPS array lookup)
 * - velocityLevel: 0.0-1.0 (voice amplitude multiplier)
 * - filterCutoff: 0.0-1.0 (filter cutoff frequency, 0=low, 1=high)
 * - attackTimeSeconds: 0.0-1.0 (envelope attack time in seconds)
 * - decayTimeSeconds: 0.0-1.0 (envelope decay time in seconds)
 * - octaveOffset: -12, 0, or +12 semitones from the octave parameter track
 * - gateLengthTicks: 1-PULSES_PER_SEQUENCER_STEP (gate duration in clock ticks)
 * - isGateHigh: boolean (voice on/off state)
 * - hasSlide: boolean (portamento enable flag)
 * - shouldRetrigger: boolean (envelope restart command flag)
 */
struct VoiceState
{
  float noteIndex = 0.0f;                                                   // Integral scale step index (0-36) for scale array lookup
  // Matches CORE_PARAMETERS' neutral Velocity default. Hard-sync presets use
  // this centered value as zero slave-frequency offset, so their slave follows
  // the master until a Slave value is recorded.
  float velocityLevel = 0.5f;
  float filterCutoff = 0.37f;                                               // Filter cutoff frequency (0.0-1.0)
  float attackTimeSeconds = 0.01f;                                          // Envelope attack time (0.0-1.0 seconds)
  float decayTimeSeconds = 0.1f;                                           // Envelope decay time (0.0-1.0 seconds)
  float sustainLevel = 0.5f;                                                // Envelope sustain level (0.0-1.0)
  float releaseTimeSeconds = 0.3f;                                          // Envelope release time (0.0-1.0 normalized)
  int8_t octaveOffset = 0;                                                  // Signed semitone transpose from the octave track
  uint16_t gateLengthTicks = SequencerConstants::DEFAULT_GATE_LENGTH_TICKS; // Gate duration in clock ticks

  // Default gate to LOW to ensure silence until sequencer explicitly gates HIGH
  bool isGateHigh = false;      // Voice on/off state
  bool hasSlide = false;        // Portamento enable flag
  bool shouldRetrigger = false; // Envelope restart command flag
};

/**
 * @brief Sequencer step parameter container
 *
 * Contains all automatable parameters for a single sequencer step.
 * This struct is used internally by the sequencer for step data storage.
 * Variable names include unit indicators and clear purpose descriptions.
 *
 * Member Ranges:
 * - noteIndex: 0-36 (integral scale step index)
 * - velocityLevel: 0.0-1.0 (voice amplitude)
 * - filterCutoff: 0.0-1.0 (filter cutoff frequency)
 * - attackTimeSeconds: 0.0-1.0 (envelope attack time in seconds)
 * - decayTimeSeconds: 0.0-1.0 (envelope decay time in seconds)
 * - octaveOffset: -12, 0, or +12 semitones from the octave parameter track
 * - gateLengthTicks: 1-PULSES_PER_SEQUENCER_STEP (gate duration in clock ticks)
 * - isGateActive: boolean (step active/inactive)
 * - hasSlide: boolean (portamento to this step)
 */
struct Step
{
  float noteIndex = 0.0f;                                                   // Integral scale step index (0-36)
  float velocityLevel = 0.5f;                                               // Voice amplitude (0.0-1.0)
  float filterCutoff = 0.5f;                                                 // Filter cutoff frequency (0.0-1.0)
  float attackTimeSeconds = 0.01f;                                          // Envelope attack time (0.0-1.0 seconds)
  float decayTimeSeconds = 0.2f;                                            // Envelope decay time (0.0-1.0 seconds)
  float sustainLevel = 0.5f;                                                // Envelope sustain level (0.0-1.0)
  float releaseTimeSeconds = 0.3f;                                          // Envelope release time (0.0-1.0 normalized)
  int8_t octaveOffset = 0;                                                  // Signed semitone transpose from the octave track
  uint16_t gateLengthTicks = SequencerConstants::DEFAULT_GATE_LENGTH_TICKS; // Gate duration in clock ticks
  bool isGateActive = false;                                                // Step active/inactive state
  bool hasSlide = false;                                                    // Portamento enable for this step
};

// --- Utility Functions ---
float mapNormalizedValueToParamRange(ParamId id, float normalizedValue);
// Fold a variant parameter value (int, float, or bool) into the float domain
// the parameter tracks store. Shared by the sequencer, parameter manager,
// and UI clear-step path.
float parameterValueAsFloat(const ParameterValueType &value);

#endif // SEQUENCER_DEFS_H
