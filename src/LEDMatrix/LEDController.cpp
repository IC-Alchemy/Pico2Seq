#include "LEDController.h"
#include "../VelocityEncoder/src/MagEncoder.h"

// External sensor instance
extern MagEncoder magEncoder;

void initLEDController() {
  // Currently no initialization required
  // Extension point for future LED controller setup
}

void updateControlLEDs(LEDMatrix& ledMatrix, const UIState& uiState) {
  const LEDThemeColors* activeThemeColors = getActiveThemeColors();
  const uint32_t currentTimeMs = millis();
  
  // Calculate pulsing brightness value for active parameter buttons
  const uint8_t pulseValue = LEDConstants::PULSE_BASE_BRIGHTNESS + 
    (sinf(currentTimeMs * LEDConstants::PULSE_FREQUENCY) * LEDConstants::PULSE_AMPLITUDE);

  // Check if magnetic encoder is controlling a parameter
  const bool isEncoderParameterSelected = (uiState.currentEncoderParameter != EncoderParameterMode::COUNT);

  // Lambda to check if a button index corresponds to current encoder parameter
  auto isCurrentEncoderButton = [&](int buttonIndex) -> bool {
    switch (uiState.currentEncoderParameter) {
      case EncoderParameterMode::Velocity: 
        return buttonIndex == ControlLEDIndices::VELOCITY_LED_INDEX;
      case EncoderParameterMode::Filter:   
        return buttonIndex == ControlLEDIndices::FILTER_LED_INDEX;
      case EncoderParameterMode::Attack:   
        return buttonIndex == ControlLEDIndices::ATTACK_LED_INDEX;
      case EncoderParameterMode::Decay:    
        return buttonIndex == ControlLEDIndices::DECAY_LED_INDEX;
      default: 
        return false;
    }
  };

  // Parameter LED configuration structure for consistent handling
  struct ParameterLEDConfiguration {
    int ledIndex;
    ParamId parameterID;
    CRGB LEDThemeColors::*activeColorPtr;
    CRGB LEDThemeColors::*inactiveColorPtr;
  };

  // Parameter button LED configurations
  const ParameterLEDConfiguration parameterLEDs[] = {
    {ControlLEDIndices::NOTE_LED_INDEX,     ParamId::Note,     &LEDThemeColors::modNoteActive,     &LEDThemeColors::modNoteInactive},
    {ControlLEDIndices::VELOCITY_LED_INDEX, ParamId::Velocity, &LEDThemeColors::modVelocityActive, &LEDThemeColors::modVelocityInactive},
    {ControlLEDIndices::FILTER_LED_INDEX,   ParamId::Filter,   &LEDThemeColors::modFilterActive,   &LEDThemeColors::modFilterInactive},
    {ControlLEDIndices::ATTACK_LED_INDEX,   ParamId::Attack,   &LEDThemeColors::modAttackActive,   &LEDThemeColors::modAttackInactive},
    {ControlLEDIndices::DECAY_LED_INDEX,    ParamId::Decay,    &LEDThemeColors::modDecayActive,    &LEDThemeColors::modDecayInactive},
    {ControlLEDIndices::OCTAVE_LED_INDEX,   ParamId::Octave,   &LEDThemeColors::modOctaveActive,   &LEDThemeColors::modOctaveInactive},
    {ControlLEDIndices::SLIDE_LED_INDEX,    ParamId::Slide,    &LEDThemeColors::modSlideActive,    &LEDThemeColors::modSlideInactive},
  };

  // Helper lambda to set LED by linear index
  auto setLEDByLinearIndex = [&ledMatrix](int linearIndex, const CRGB& color) {
    const int xCoord = linearIndex % LEDMatrix::WIDTH;
    const int yCoord = linearIndex / LEDMatrix::WIDTH;
    ledMatrix.setLED(xCoord, yCoord, color);
  };

  // Helper lambda to create pulsed color for active parameters
  auto createPulsedColor = [pulseValue](const CRGB& baseColor) {
    CRGB pulsedColor = baseColor;
    return pulsedColor.nscale8(pulseValue);
  };

  // Helper lambda to create faded color based on parameter value
  auto createParameterValueColor = [](const CRGB& baseColor, float parameterValue) {
    CRGB fadedColor = baseColor;
    const uint8_t scalingFactor = static_cast<uint8_t>(parameterValue * LEDConstants::FULL_BRIGHTNESS);
    return fadedColor.nscale8(scalingFactor);
  };

  // Update parameter button LEDs
  for (const auto& paramConfig : parameterLEDs) {
    CRGB ledColor;
    
    // Check if parameter button is currently held
    const bool isParameterHeld = (paramConfig.parameterID == ParamId::Slide) ? 
      uiState.slideMode : 
      uiState.parameterButtonHeld[static_cast<int>(paramConfig.parameterID)];

    if (isParameterHeld) {
      // Parameter is actively held - show pulsing active color
      ledColor = createPulsedColor(activeThemeColors->*(paramConfig.activeColorPtr));
    } else if (isEncoderParameterSelected && magEncoder.isConnected() && 
               isCurrentEncoderButton(paramConfig.ledIndex)) {
      // magnetic encoder is controlling this parameter - show value-based fading
      const float currentParameterValue = getEncoderParameterValue();
      ledColor = createParameterValueColor(activeThemeColors->*(paramConfig.activeColorPtr), 
                                          currentParameterValue);
    } else {
      // Parameter is inactive - show dim inactive color
      ledColor = activeThemeColors->*(paramConfig.inactiveColorPtr);
    }
    
    setLEDByLinearIndex(paramConfig.ledIndex, ledColor);
  }

  // Update delay parameter LEDs (magnetic encoder controlled)
  CRGB delayTimeLEDColor = LEDColors::DELAY_TIME_BASE;
  CRGB delayFeedbackLEDColor = LEDColors::DELAY_FEEDBACK_BASE;

  if (magEncoder.isConnected()) {
    const float currentParameterValue = getEncoderParameterValue();
    
    switch (uiState.currentEncoderParameter) {
      case EncoderParameterMode::DelayTime:
        delayTimeLEDColor = createParameterValueColor(LEDColors::DELAY_INDICATOR, currentParameterValue);
        break;
      case EncoderParameterMode::DelayFeedback:
        delayFeedbackLEDColor = createParameterValueColor(LEDColors::DELAY_INDICATOR, currentParameterValue);
        break;
      default:
        // No encoder parameter selected - use base colors
        break;
    }
  }

  setLEDByLinearIndex(ControlLEDIndices::DELAY_TIME_LED_INDEX, delayTimeLEDColor);
  setLEDByLinearIndex(ControlLEDIndices::DELAY_FEEDBACK_LED_INDEX, delayFeedbackLEDColor);

  // Voice selection indicators: the two panel LEDs serve all four voices by
  // following the selected voice pair (1+2 or 3+4). The selected voice's LED
  // is bright, its pair partner's dim. (If the bench panel turns out to have
  // dedicated V3/V4 LEDs, this is the place to split them out.)
  const bool selectedIsFirstInPair = (uiState.selectedVoiceIndex % 2) == 0;
  const CRGB selectedVoiceLEDColor = activeThemeColors->defaultActive;
  const CRGB partnerVoiceLEDColor = activeThemeColors->defaultInactive;

  setLEDByLinearIndex(ControlLEDIndices::VOICE1_LED_INDEX,
                      selectedIsFirstInPair ? selectedVoiceLEDColor : partnerVoiceLEDColor);
  setLEDByLinearIndex(ControlLEDIndices::VOICE2_LED_INDEX,
                      selectedIsFirstInPair ? partnerVoiceLEDColor : selectedVoiceLEDColor);

  // Update delay toggle LED with flash timing
  CRGB delayToggleLEDColor;
  if (uiState.flash23Until && currentTimeMs < uiState.flash23Until) {
    delayToggleLEDColor = activeThemeColors->randomizeFlash;
  } else {
    delayToggleLEDColor = uiState.delayOn ? 
      activeThemeColors->gateOnV1 : activeThemeColors->gateOffV1;
  }
  setLEDByLinearIndex(ControlLEDIndices::DELAY_TOGGLE_LED_INDEX, delayToggleLEDColor);

  // Update randomize LED with flash timing
  CRGB randomizeLEDColor;
  if (uiState.flash31Until && currentTimeMs < uiState.flash31Until) {
    randomizeLEDColor = activeThemeColors->randomizeFlash;
  } else {
    randomizeLEDColor = activeThemeColors->randomizeIdle;
  }
  setLEDByLinearIndex(ControlLEDIndices::RANDOMIZE_LED_INDEX, randomizeLEDColor);
}
