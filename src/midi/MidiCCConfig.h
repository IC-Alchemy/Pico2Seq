#ifndef MIDI_CC_CONFIG_H
#define MIDI_CC_CONFIG_H

#include <cstdint>

/**
 * @file MidiCCConfig.h
 * @brief MIDI Continuous Controller Configuration for PicoMudrasSequencer
 *
 * This file contains all configuration constants and mappings for MIDI CC
 * functionality. Modify these values to customize CC behavior without
 * changing the core implementation.
 */

// =======================
//   CC NUMBER MAPPINGS
// =======================

/**
 * CC Number Assignment Strategy:
 *
 * Voice 1 Parameters:
 * - CC71: Octave offset
 * - CC72: Decay time
 * - CC73: Attack time
 * - CC74: Filter cutoff (standard MIDI CC for filter)
 *
 * Voice 2 Parameters:
 * - CC75: Octave offset
 * - CC76: Decay time
 * - CC77: Attack time
 * - CC78: Filter cutoff
 *
 * This mapping provides:
 * - Clear separation between voices (4 CC numbers each)
 * - Standard CC74 for filter cutoff (widely recognized)
 * - Logical grouping of envelope parameters
 * - Easy expansion for additional voices
 */

// Voice-specific CC base numbers
constexpr uint8_t CC_VOICE1_BASE = 71;  ///< Base CC number for Voice 1 (range: 71-74)
constexpr uint8_t CC_VOICE2_BASE = 75;  ///< Base CC number for Voice 2 (range: 75-78)

// Parameter offset mappings (added to base for final CC number)
constexpr uint8_t CC_OCTAVE_OFFSET = 0;  ///< Octave: CC71/75
constexpr uint8_t CC_DECAY_OFFSET = 1;   ///< Decay:  CC72/76
constexpr uint8_t CC_ATTACK_OFFSET = 2;  ///< Attack: CC73/77
constexpr uint8_t CC_FILTER_OFFSET = 3;  ///< Filter: CC74/78 (standard)

// =======================
//   CHANNEL CONFIGURATION
// =======================

constexpr uint8_t CC_MIDI_CHANNEL = 1;  ///< MIDI channel for all CC messages

// =======================
//   TRANSMISSION SETTINGS
// =======================

constexpr unsigned long CC_MIN_INTERVAL_MS = 10;  ///< Minimum time between CC transmissions (ms)
constexpr bool CC_RATE_LIMITING_ENABLED = true;   ///< Enable/disable transmission rate limiting

// =======================
//   VALUE SCALING
// =======================

constexpr float CC_PARAMETER_MIN = 0.0f;  ///< Minimum normalized parameter value
constexpr float CC_PARAMETER_MAX = 1.0f;  ///< Maximum normalized parameter value
constexpr uint8_t CC_MIDI_MIN = 0;        ///< Minimum MIDI CC value
constexpr uint8_t CC_MIDI_MAX = 127;      ///< Maximum MIDI CC value

// =======================
//   DEBUG CONFIGURATION
// =======================

constexpr bool CC_DEBUG_ENABLED = true;           ///< Enable/disable CC debug output
constexpr bool CC_VERBOSE_DEBUG = false;          ///< Enable verbose debugging (parameter names, etc.)
constexpr bool CC_PERFORMANCE_MONITORING = false; ///< Enable performance metrics tracking

// =======================
//   FEATURE FLAGS
// =======================

constexpr bool CC_CHANGE_DETECTION_ENABLED = true;  ///< Enable change detection to prevent spam
constexpr bool CC_HYSTERESIS_ENABLED = false;       ///< Enable hysteresis for change detection
constexpr uint8_t CC_HYSTERESIS_THRESHOLD = 1;      ///< MIDI value change threshold for hysteresis

// =======================
//   ARRAY SIZING
// =======================

constexpr uint8_t CC_MAX_VOICES = 2;      ///< Maximum number of voices supported
constexpr uint8_t CC_PARAMETERS_PER_VOICE = 4;  ///< Number of CC parameters per voice

#endif // MIDI_CC_CONFIG_H
