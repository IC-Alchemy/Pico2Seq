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
namespace SequencerConstants {
  // Timing constants with unit indicators
  static constexpr uint16_t PULSES_PER_QUARTER_NOTE_PPQN = 480;
  static constexpr uint8_t PULSES_PER_SEQUENCER_STEP_TICKS = PULSES_PER_QUARTER_NOTE_PPQN / 4;
  
  // Step count limits
  static constexpr uint8_t MAX_STEPS_COUNT = 64;
  static constexpr uint8_t MIN_STEPS_COUNT = 2;
  static constexpr uint8_t DEFAULT_STEPS_COUNT = 16;
  
  // Gate timing constants
  static constexpr uint16_t DEFAULT_GATE_LENGTH_TICKS = PULSES_PER_SEQUENCER_STEP_TICKS / 2;
  static constexpr uint16_t MIN_GATE_LENGTH_TICKS = 1;
  static constexpr uint16_t MAX_GATE_LENGTH_TICKS = PULSES_PER_SEQUENCER_STEP_TICKS;
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
enum class ParamId : uint8_t {
  Note,       // 0 - Scale step index (0-21, maps to SCALE_STEPS array)
  Velocity,   // 1 - Voice amplitude (0.0-1.0)
  Filter,     // 2 - Filter cutoff frequency (0.0-1.0)
  Attack,     // 3 - Envelope attack time (0.0-1.0 seconds)
  Decay,      // 4 - Envelope decay time (0.0-1.0 seconds)
  Octave,     // 5 - Octave offset (0.0=C2, 0.5=C3, 1.0=C4)
  GateLength, // 6 - Gate duration (0.001-1.0 as fraction of step)
  Gate,       // 7 - Gate on/off state (boolean)
  Slide,      // 8 - Portamento enable (boolean)
  Count       // Total parameter count for array sizing
};

// Constant for array sizing based on ParamId::Count
constexpr uint8_t PARAM_ID_COUNT = static_cast<uint8_t>(ParamId::Count);

/**
 * @brief AS5600 magnetic encoder parameter cycling modes
 * 
 * Defines which parameter the AS5600 encoder controls in real-time.
 * Moved from UIEventHandler.h to break circular dependency.
 */
enum class AS5600ParameterMode : uint8_t {
  Velocity = 0,     // Voice amplitude control
  Filter = 1,       // Filter cutoff control
  Attack = 2,       // Envelope attack time control
  Decay = 3,        // Envelope decay time control
  Note = 4,         // Note/pitch control
  DelayTime = 5,    // Global delay time control
  DelayFeedback = 6, // Global delay feedback control
  SlideTime = 7,    // Portamento/slide time control
  COUNT = 8         // Total mode count
};

/**
 * @brief AS5600 encoder base parameter values for bidirectional control
 * 
 * Stores base values for each parameter that can be controlled by the AS5600
 * magnetic encoder. Supports bidirectional control by maintaining center points.
 */
struct AS5600BaseValues {
  float velocity = 0.0f;        // Base velocity (0.0-1.0)
  float filter = 0.0f;          // Base filter cutoff (0.0-1.0)
  float attack = 0.0f;          // Base attack time (0.0-1.0 seconds)
  float decay = 0.0f;           // Base decay time (0.0-1.0 seconds)
  float delayTime = 0.0f;       // Delay time offset for global delay
  float delayFeedback = 0.0f;   // Delay feedback offset for global delay
  float slideTime = 0.0f;       // Slide time in seconds for voice glide
};

/**
 * @brief Voice-specific AS5600 base values
 * 
 * Inherits from AS5600BaseValues with no additional members.
 * Delay parameters are inherited from base class.
 */
struct AS5600BaseValuesVoice1 : public AS5600BaseValues {
  // No additional members - delay parameters are inherited from base class
};
/**
 * @brief Step parameter edit button state tracking
 * 
 * Tracks which parameter edit buttons are currently pressed for step editing.
 * Used by the UI system to determine which parameter to modify when editing steps.
 */
struct StepEditButtons {
  bool note;      // Note parameter edit button state
  bool velocity;  // Velocity parameter edit button state
  bool filter;    // Filter parameter edit button state
  bool attack;    // Attack parameter edit button state
  bool decay;     // Decay parameter edit button state
  bool octave;    // Octave parameter edit button state
};

/**
 * @brief Fixed-size parameter automation track
 * 
 * Template class for storing parameter values across sequencer steps.
 * Handles step wrapping and dynamic sequence length changes.
 * Variable names include clear purpose descriptions.
 * 
 * @tparam MAX_SIZE Maximum number of steps (typically SequencerConstants::MAX_STEPS_COUNT)
 */
template <uint8_t MAX_SIZE>
struct ParameterTrack {
  float parameterValues[MAX_SIZE];          // Parameter values for each step
  uint8_t currentStepCount;                 // Current sequence length (2-64 steps)
  float defaultParameterValue;              // Default value for new steps

  /**
   * @brief Initialize track with default values
   * @param defaultValue Default parameter value for all steps
   */
  void init(float defaultValue) {
    defaultParameterValue = defaultValue;
    currentStepCount = SequencerConstants::DEFAULT_STEPS_COUNT;
    for (uint8_t i = 0; i < MAX_SIZE; ++i) {
      parameterValues[i] = defaultValue;
    }
  }

  /**
   * @brief Get parameter value for any step index
   * @param stepIndex Step index (handles wrapping automatically)
   * @return Parameter value at the specified step
   */
  float getValue(uint8_t stepIndex) const {
    if (currentStepCount == 0) {
      return defaultParameterValue; // Prevent division by zero
    }
    return parameterValues[stepIndex % currentStepCount];
  }

  /**
   * @brief Set parameter value for a specific step
   * @param stepIndex Step index (handles wrapping automatically)
   * @param newValue New parameter value
   */
  void setValue(uint8_t stepIndex, float newValue) {
    if (currentStepCount == 0) {
      return; // Prevent division by zero
    }
    parameterValues[stepIndex % currentStepCount] = newValue;
  }

  /**
   * @brief Resize track to new step count
   * @param newStepCount New sequence length (MIN_STEPS_COUNT to MAX_SIZE)
   */
  void resize(uint8_t newStepCount) {
    if (newStepCount >= SequencerConstants::MIN_STEPS_COUNT && newStepCount <= MAX_SIZE) {
      // Preserve existing values
      if (newStepCount > currentStepCount) {
        for (uint8_t i = currentStepCount; i < newStepCount; ++i) {
          parameterValues[i] = defaultParameterValue;
        }
      }
      currentStepCount = newStepCount;
    }
  }
};

// Define the variant type for parameter values that can be int, float, or bool
using ParameterValueType = std::variant<int, float, bool>;

/**
 * @brief Parameter definition with metadata and constraints
 * 
 * Defines the characteristics and valid ranges for each sequencer parameter.
 * Used for validation, UI display, and parameter initialization.
 */
struct ParameterDefinition {
  const char *name;                // Display name for UI
  ParameterValueType defaultValue; // Default parameter value
  ParameterValueType minValue;     // Minimum allowed value
  ParameterValueType maxValue;     // Maximum allowed value
  bool isBinary;                   // True for gate/slide-like parameters
  uint8_t defaultSteps;            // Default number of steps for this parameter
};

/**
 * @brief Core parameter definitions array
 * 
 * Defines metadata for all sequencer parameters. Array order MUST match ParamId enum.
 * Each entry specifies: name, defaultValue, minValue, maxValue, isBinary, defaultSteps
 * Uses SequencerConstants for consistent step count defaults.
 */
constexpr ParameterDefinition CORE_PARAMETERS[] = {
  // Parameter Name    Default    Min       Max       Binary  Steps
  {"Note",             0.0f,      0.0f,     21.0f,    false,  SequencerConstants::DEFAULT_STEPS_COUNT}, // Scale step index (0-21)
  {"Velocity",         0.5f,      0.0f,     1.0f,     false,  SequencerConstants::DEFAULT_STEPS_COUNT}, // Voice amplitude (0.0-1.0)
  {"Filter",           0.5f,      0.0f,     1.0f,     false,  SequencerConstants::DEFAULT_STEPS_COUNT}, // Filter cutoff (0.0-1.0)
  {"Attack",           0.01f,     0.0f,     1.0f,     false,  SequencerConstants::DEFAULT_STEPS_COUNT}, // Attack time (0.0-1.0 seconds)
  {"Decay",            0.11f,     0.0f,     1.0f,     false,  SequencerConstants::DEFAULT_STEPS_COUNT}, // Decay time (0.0-1.0 seconds)
  {"Octave",           0.0f,      0.0f,     1.0f,     false,  SequencerConstants::DEFAULT_STEPS_COUNT}, // Octave offset (0.0=C2, 0.5=C3, 1.0=C4)
  {"GateLength",       0.1f,      0.001f,   1.0f,     false,  SequencerConstants::DEFAULT_STEPS_COUNT}, // Gate duration (fraction of step)
  {"Gate",             false,     false,    true,     true,   SequencerConstants::DEFAULT_STEPS_COUNT}, // Gate on/off state
  {"Slide",            false,     false,    true,     true,   SequencerConstants::DEFAULT_STEPS_COUNT}  // Portamento enable
};

/**
 * @brief Voice synthesis parameters for audio output
 * 
 * Contains all parameters needed by the audio synthesis engine for a single voice.
 * This struct is passed from the sequencer to the audio processing system.
 * Variable names include unit indicators and clear purpose descriptions.
 * 
 * Member Ranges:
 * - noteIndex: 0.0-21.0 (scale step index for SCALE_STEPS array lookup)
 * - velocityLevel: 0.0-1.0 (voice amplitude multiplier)
 * - filterCutoff: 0.0-1.0 (filter cutoff frequency, 0=low, 1=high)
 * - attackTimeSeconds: 0.0-1.0 (envelope attack time in seconds)
 * - decayTimeSeconds: 0.0-1.0 (envelope decay time in seconds)
 * - octaveOffset: 0.0-1.0 (octave offset: 0.0=C2, 0.5=C3, 1.0=C4)
 * - gateLengthTicks: 1-PULSES_PER_SEQUENCER_STEP (gate duration in clock ticks)
 * - isGateHigh: boolean (voice on/off state)
 * - hasSlide: boolean (portamento enable flag)
 * - shouldRetrigger: boolean (envelope restart command flag)
 */
struct VoiceState {
  float noteIndex = 0.0f;                    // Scale step index (0-21) for scale array lookup
  float velocityLevel = 0.8f;                // Voice amplitude (0.0-1.0)
  float filterCutoff = 0.37f;                // Filter cutoff frequency (0.0-1.0)
  float attackTimeSeconds = 0.01f;           // Envelope attack time (0.0-1.0 seconds)
  float decayTimeSeconds = 0.01f;            // Envelope decay time (0.0-1.0 seconds)
  float octaveOffset = 0.0f;                 // Octave offset (0.0=C2, 0.5=C3, 1.0=C4)
  uint16_t gateLengthTicks = SequencerConstants::DEFAULT_GATE_LENGTH_TICKS; // Gate duration in clock ticks

  // Default gate to LOW to ensure silence until sequencer explicitly gates HIGH
  bool isGateHigh = false;                   // Voice on/off state
  bool hasSlide = false;                     // Portamento enable flag
  bool shouldRetrigger = false;              // Envelope restart command flag
};

/**
 * @brief Sequencer step parameter container
 * 
 * Contains all automatable parameters for a single sequencer step.
 * This struct is used internally by the sequencer for step data storage.
 * Variable names include unit indicators and clear purpose descriptions.
 * 
 * Member Ranges:
 * - noteIndex: 0.0-21.0 (scale step index)
 * - velocityLevel: 0.0-1.0 (voice amplitude)
 * - filterCutoff: 0.0-1.0 (filter cutoff frequency)
 * - attackTimeSeconds: 0.0-1.0 (envelope attack time in seconds)
 * - decayTimeSeconds: 0.0-1.0 (envelope decay time in seconds)
 * - octaveOffset: 0.0-1.0 (octave offset: 0.0=C2, 0.5=C3, 1.0=C4)
 * - gateLengthTicks: 1-PULSES_PER_SEQUENCER_STEP (gate duration in clock ticks)
 * - isGateActive: boolean (step active/inactive)
 * - hasSlide: boolean (portamento to this step)
 */
struct Step {
  float noteIndex = 0.0f;                    // Scale step index (0-21)
  float velocityLevel = 0.5f;                // Voice amplitude (0.0-1.0)
  float filterCutoff = 0.5f;                 // Filter cutoff frequency (0.0-1.0)
  float attackTimeSeconds = 0.04f;           // Envelope attack time (0.0-1.0 seconds)
  float decayTimeSeconds = 0.1f;             // Envelope decay time (0.0-1.0 seconds)
  float octaveOffset = 0.0f;                 // Octave offset (0.0=C2, 0.5=C3, 1.0=C4)
  uint16_t gateLengthTicks = SequencerConstants::DEFAULT_GATE_LENGTH_TICKS; // Gate duration in clock ticks
  bool isGateActive = false;                 // Step active/inactive state
  bool hasSlide = false;                     // Portamento enable for this step
};

/**
 * @brief Gate timing system for automatic gate turn-off
 * 
 * Manages gate duration timing with tick-based countdown. Used to automatically
 * turn off voice gates after the specified duration to prevent stuck notes.
 * All members are volatile for safe access from interrupt contexts.
 * Variable names include unit indicators for clarity.
 */
struct GateTimer {
  volatile bool isActive = false;                     // Timer active state
  volatile uint16_t ticksRemaining = 0;               // Clock ticks remaining until gate off
  volatile uint32_t totalTicksProcessed = 0;          // Debug counter for diagnostics

  /**
   * @brief Start gate timer with specified duration
   * @param durationTicks Gate duration in clock ticks
   */
  void start(uint16_t durationTicks) volatile {
    isActive = true;
    ticksRemaining = durationTicks;
    totalTicksProcessed = 0; // Reset debug counter
  }

  /**
   * @brief Process one clock tick
   * Decrements remaining ticks and deactivates timer when expired
   */
  void tick() volatile {
    totalTicksProcessed++; // Always increment for debugging
    if (isActive && ticksRemaining > 0) {
      ticksRemaining--;
      if (ticksRemaining == 0) {
        isActive = false;
      }
    }
  }

  /**
   * @brief Stop timer immediately
   */
  void stop() volatile {
    isActive = false;
    ticksRemaining = 0;
  }

  /**
   * @brief Check if timer has expired
   * @return true if timer is inactive and no ticks remain
   */
  bool isExpired() const volatile {
    return !isActive && ticksRemaining == 0;
  }
};

// --- Utility Functions ---
float mapNormalizedValueToParamRange(ParamId id, float normalizedValue);

#endif // SEQUENCER_DEFS_H
