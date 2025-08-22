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

class OLEDDisplay {
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
  void update(const UIState& uiState, const Sequencer& seq1, const Sequencer& seq2, 
              const Sequencer& seq3, const Sequencer& seq4);
  
  /**
   * @brief Clear display and turn off all pixels
   */
  void clear();
  
  /**
   * @brief Check if display hardware is initialized and ready
   * @return true if display is ready for use
   */
  bool isInitialized() const { return isDisplayInitialized; }

private:
  // Hardware display object
  Adafruit_SH1106G displayHardware;
  bool isDisplayInitialized = false;
  
  // Animation state variables
  uint32_t lastAnimationFrameMs = 0;
  uint8_t borderAnimationPhase = 0;

  /**
   * @brief Display parameter editing information with progress bar
   * @param parameterName Name of the parameter being edited
   * @param currentValue Current parameter value (0.0-1.0 for most parameters)
   * @param voiceNumber Voice number (1-4) for display
   * @param stepIndex Current step index for the parameter
   */
  void displayParameterInfo(const char* parameterName, float currentValue, 
                           uint8_t voiceNumber, uint8_t stepIndex);
  
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
  void displaySettingsMenu(const UIState& uiState);
  
  /**
   * @brief Draw step indicator bars at bottom of display
   * @param sequencer Sequencer reference for step state
   * @param yPosition Y coordinate for indicator placement
   */
  void drawStepIndicators(const Sequencer& sequencer, int yPosition);
  
  /**
   * @brief Run startup animation sequence
   */
  void runStartupAnimation();
};

// External declaration for global OLED display instance
extern OLEDDisplay oledDisplay;

#endif // OLED_H