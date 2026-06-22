#ifndef LEDMATRIX_FEEDBACK_H
#define LEDMATRIX_FEEDBACK_H

#include "ledMatrix.h"
#include "LEDConstants.h"
#include "../voice/VoiceManager.h"

// Forward declarations to break circular dependencies
class Sequencer;
struct UIState;

/**
 * @brief LED Matrix Feedback System for PicoMudrasSequencer
 *
 * Provides comprehensive visual feedback for sequencer state, parameter editing,
 * and UI modes through the LED matrix. Handles multiple display modes including:
 * - Step gate visualization with playhead indication
 * - Parameter editing feedback with length and value display
 * - Settings menu navigation with preset selection
 * - Voice parameter configuration display
 * - Breathing animation for idle states
 * - Polyrhythmic overlay visualization
 */

/**
 * @brief LED Theme Enumeration
 *
 * Defines available color themes for the LED matrix system.
 * Each theme provides a complete set of colors for all UI elements.
 */
enum class LEDTheme
{
  DEFAULT = 0, // Standard blue/green theme
  OCEANIC,     // Blue/cyan ocean theme
  VOLCANIC,    // Red/orange fire theme
  FOREST,      // Green/brown nature theme
  NEON,        // Bright cyan/magenta theme
  MODERN,      // New modern muted theme with high legibility
  DARK_NOCTIS, // Dark theme with cool blue accents
  DARK_EMBER,  // Dark theme with warm amber accents
  BLUE,        // High-contrast blue theme
  GREEN,       // High-contrast green theme
  COUNT        // Keep last - used for theme count
};

/**
 * @brief LED Theme Color Structure
 *
 * Contains all colors used by the LED matrix system for a specific theme.
 * Provides consistent color mapping across all LED feedback functions.
 */
struct LEDThemeColors
{
  // Voice gate state colors
  CRGB gateOnV1;  // Voice 1 gate active color
  CRGB gateOffV1; // Voice 1 gate inactive color
  CRGB gateOnV2;  // Voice 2 gate active color
  CRGB gateOffV2; // Voice 2 gate inactive color

  // Playhead and accent colors
  CRGB playheadAccent;    // Current step playhead highlight
  CRGB idleBreathingBlue; // Breathing animation base color

  // Edit mode colors
  CRGB editModeDimBlueV1; // Voice 1 parameter edit mode
  CRGB editModeDimBlueV2; // Voice 2 parameter edit mode

  // Parameter button colors (active/inactive pairs)
  CRGB modNoteActive;       // Note parameter active
  CRGB modNoteInactive;     // Note parameter inactive
  CRGB modVelocityActive;   // Velocity parameter active
  CRGB modVelocityInactive; // Velocity parameter inactive
  CRGB modFilterActive;     // Filter parameter active
  CRGB modFilterInactive;   // Filter parameter inactive
  CRGB modDecayActive;      // Decay parameter active
  CRGB modDecayInactive;    // Decay parameter inactive
  CRGB modAttackActive;     // Attack parameter active
  CRGB modAttackInactive;   // Attack parameter inactive
  CRGB modOctaveActive;     // Octave parameter active
  CRGB modOctaveInactive;   // Octave parameter inactive
  CRGB modSlideActive;      // Slide parameter active
  CRGB modSlideInactive;    // Slide parameter inactive

  // Default and mode colors
  CRGB defaultActive;        // Default active state
  CRGB defaultInactive;      // Default inactive state
  CRGB modParamModeActive;   // Parameter mode active
  CRGB modParamModeInactive; // Parameter mode inactive
  CRGB modGateModeActive;    // Gate mode active
  CRGB modGateModeInactive;  // Gate mode inactive

  // Special effect colors
  CRGB randomizeFlash; // Randomize button flash
  CRGB randomizeIdle;  // Randomize button idle
};

/**
 * @brief Initialize LED matrix feedback system
 *
 * Sets up smoothed color buffers and initializes the LED feedback system.
 * Must be called before using other LED feedback functions.
 */
void setupLEDMatrixFeedback();

/**
 * @brief Update step LEDs based on sequencer and UI state
 *
 * Main LED update function that handles all display modes:
 * - Idle breathing animation when sequencers stopped
 * - Gate state visualization with playhead indication
 * - Parameter editing mode with length and value feedback
 * - Settings menu navigation display
 * - Voice parameter configuration display
 *
 * @param ledMatrix Reference to LED matrix for output
 * @param seq1 Voice 1 sequencer reference
 * @param seq2 Voice 2 sequencer reference
 * @param seq3 Voice 3 sequencer reference
 * @param seq4 Voice 4 sequencer reference
 * @param uiState Current UI state containing mode flags and selections
 * @param mm Unused parameter (legacy)
 */
void updateStepLEDs(
    LEDMatrix &ledMatrix,
    const Sequencer &seq1,
    const Sequencer &seq2,
    const Sequencer &seq3,
    const Sequencer &seq4,
    const UIState &uiState,
    int mm);

/**
 * @brief Update LED matrix for settings mode interface
 *
 * Displays settings menu navigation with visual feedback:
 * - Main menu: Shows voice selection options with pulsing
 * - Preset selection: Shows available presets with current selection highlight
 * - Voice indicators: Shows which voice is being configured
 *
 * @param ledMatrix Reference to LED matrix for output
 * @param uiState Current UI state containing settings menu state
 */
void updateSettingsModeLEDs(LEDMatrix &ledMatrix, const UIState &uiState);

/**
 * @brief Update LED matrix for voice parameter feedback
 *
 * Highlights voice parameter buttons (9-24) with visual indication
 * of parameter state and pulsing effect for recent changes.
 *
 * @param ledMatrix Reference to LED matrix for output
 * @param uiState Current UI state containing voice parameter information
 */
void updateVoiceParameterLEDs(LEDMatrix &ledMatrix, const UIState &uiState);

/**
 * @brief Set individual step LED color (legacy function)
 *
 * @param step Step index (0-15)
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 */
void setStepLedColor(uint8_t step, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set active LED color theme
 *
 * Changes the active color theme for all LED feedback functions.
 * Theme change takes effect immediately on next LED update.
 *
 * @param theme LEDTheme enumeration value to activate
 */
void setLEDTheme(LEDTheme theme);

/**
 * @brief Get pointer to currently active theme colors
 *
 * @return Const pointer to active LEDThemeColors structure
 */
const LEDThemeColors *getActiveThemeColors();

#endif // LEDMATRIX_FEEDBACK_H
