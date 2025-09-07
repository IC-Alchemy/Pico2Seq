#ifndef OLED_H
#define OLED_H

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "../ui/UIState.h"
#include "../ui/ButtonManager.h"
#include "../sequencer/Sequencer.h"
#include "../sequencer/SequencerDefs.h"
#include "../LEDMatrix/LEDConstants.h"

/**
 * @brief Voice Parameter Observer Interface
 *
 * Provides immediate callback system for real-time OLED display updates
 * when voice parameters change or voices are switched. Enables responsive
 * visual feedback for parameter editing operations.
 */
class VoiceParameterObserver
{
public:
  virtual ~VoiceParameterObserver() = default;

  /**
   * @brief Called when a voice parameter value changes
   * @param voiceId ID of the voice that changed
   * @param state Current voice state with updated parameter values
   */
  virtual void onVoiceParameterChanged(uint8_t voiceId, const VoiceState &state) = 0;

  /**
   * @brief Called when the active voice is switched
   * @param newVoiceId ID of the newly selected voice
   */
  virtual void onVoiceSwitched(uint8_t newVoiceId) = 0;
};

/**
 * @brief OLED Display Manager for PicoMudrasSequencer
 *
 * Comprehensive display system providing real-time visual feedback for:
 * - Parameter editing with value display and progress bars
 * - Settings menu navigation with preset selection
 * - Voice parameter configuration display
 * - Step sequencer status with animated indicators
 * - Scale and shuffle pattern information
 *
 * Implements VoiceParameterObserver for immediate parameter change feedback
 * and includes animated visual enhancements for professional appearance.
 */
class OLEDDisplay : public VoiceParameterObserver
{
public:
  /**
   * @brief Constructor - initializes display object
   */
  OLEDDisplay();

  /**
   * @brief Initialize OLED display hardware
   * @return true if initialization successful, false on failure
   */
  bool begin();

  /**
   * @brief Update display with current system state (basic version)
   * @param uiState Current UI state containing button states and modes
   * @param seq1 Voice 1 sequencer reference for parameter values
   * @param seq2 Voice 2 sequencer reference for parameter values
   * @param seq3 Voice 3 sequencer reference for parameter values
   * @param seq4 Voice 4 sequencer reference for parameter values
   */
  void update(const UIState &uiState, const Sequencer &seq1, const Sequencer &seq2,
              const Sequencer &seq3, const Sequencer &seq4);

  /**
   * @brief Update display with voice manager access (extended version)
   * @param uiState Current UI state containing button states and modes
   * @param seq1 Voice 1 sequencer reference for parameter values
   * @param seq2 Voice 2 sequencer reference for parameter values
   * @param seq3 Voice 3 sequencer reference for parameter values
   * @param seq4 Voice 4 sequencer reference for parameter values
   * @param voiceManager Pointer to voice manager for accessing voice configurations
   */
  void update(const UIState &uiState, const Sequencer &seq1, const Sequencer &seq2,
              const Sequencer &seq3, const Sequencer &seq4, class VoiceManager *voiceManager);

  /**
   * @brief Clear display and turn off all pixels
   */
  void clear();

  /**
   * @brief Check if display hardware is initialized and ready
   * @return true if display is ready for use
   */
  bool isInitialized() const { return isDisplayInitialized; }

  /**
   * @brief Set voice manager reference for parameter updates
   * @param voiceManager Pointer to the voice manager instance
   */
  void setVoiceManager(class VoiceManager *voiceManager);

  // VoiceParameterObserver interface implementation
  void onVoiceParameterChanged(uint8_t voiceId, const VoiceState &state) override;
  void onVoiceSwitched(uint8_t newVoiceId) override;

  /**
   * @brief Handle voice switching with immediate OLED update (extended version)
   * @param uiState Current UI state for context
   * @param voiceManager Pointer to voice manager for configuration access
   */
  void onVoiceSwitched(const UIState &uiState, class VoiceManager *voiceManager);

private:
  // Hardware display object
  Adafruit_SH1106G displayHardware;
  bool isDisplayInitialized = false;

  // Voice manager reference for immediate updates
  class VoiceManager *voiceManagerReference = nullptr;

  // Animation state variables
  uint32_t lastAnimationFrameMs = 0;
  uint8_t borderAnimationPhase = 0;

  /**
   * @brief Display parameter editing information with progress bar
   * @param parameterName Name of the parameter being edited
   * @param currentValue Current parameter value (0.0-1.0 for most parameters)
   * @param voiceIndex Voice index (0-3) for display
   * @param stepIndex Current step index for the parameter
   */
  void displayParameterInfo(const char *parameterName, float currentValue,
                            uint8_t voiceIndex, uint8_t stepIndex);

  /**
   * @brief Format parameter value for human-readable display
   * @param parameterID Parameter ID for type-specific formatting
   * @param rawValue Raw parameter value from sequencer
   * @return Formatted string with units and appropriate precision
   */
  String formatParameterValue(ParamId parameterID, float rawValue);

  /**
   * @brief Display settings menu with navigation and preset selection
   * @param uiState Current UI state containing settings menu state
   */
  void displaySettingsMenu(const UIState &uiState);

  /**
   * @brief Display voice parameter information (legacy function)
   * @param uiState Current UI state containing voice parameter information
   * @param voiceManager Pointer to voice manager for configuration access
   * @param leadVoiceId ID of the lead voice
   * @param bassVoiceId ID of the bass voice
   */
  void displayVoiceParameterInfo(const UIState &uiState, class VoiceManager *voiceManager,
                                 uint8_t leadVoiceId, uint8_t bassVoiceId);

  /**
   * @brief Display voice parameter toggles in settings mode
   * @param uiState Current UI state with voice selection
   * @param voiceManager Pointer to voice manager for configuration access
   */
  void displayVoiceParameterToggles(const UIState &uiState, class VoiceManager *voiceManager);

  /**
   * @brief Force immediate display update for voice parameter changes
   * @param uiState Current UI state for context
   * @param voiceManager Pointer to voice manager for configuration access
   */
  void forceUpdate(const UIState &uiState, class VoiceManager *voiceManager);

  // Visual enhancement helper functions

  /**
   * @brief Draw step indicator bars at bottom of display
   * @param sequencer Sequencer reference for step state
   * @param yPosition Y coordinate for indicator placement
   */
  void drawStepIndicators(const Sequencer &sequencer, int yPosition);

  /**
   * @brief Run startup animation sequence
   */
  void runStartupAnimation();
};

// External declaration for global OLED display instance
extern OLEDDisplay oledDisplay;

#endif // OLED_H