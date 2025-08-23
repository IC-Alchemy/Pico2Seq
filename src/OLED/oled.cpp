#include "oled.h"
#include "../voice/Voice.h"
#include "../voice/VoicePresets.h"
#include "../voice/VoiceSystem.h" // Added for complete VoiceSystem type
#include "../../includes.h"
#include "../sequencer/SequencerDefs.h"
#include "../sequencer/ShuffleTemplates.h"
#include "../scales/scales.h"
#include "../ui/ButtonManager.h"
#include <cstring> // For strcmp, strlen
#include <Arduino.h>

// ========================= OLED Display Module =========================
// Overview:
// - Purpose: Centralized UI rendering for PicoMudrasSequencer on an SH110X OLED.
// - Responsibilities: Initialize display hardware, render hierarchical UI (settings,
//   parameter edit, default status), and provide immediate visual feedback for voice
//   parameter changes using an observer-style interface.
// - Design principles:
//   1) Deterministic, low-overhead drawing: minimize dynamic heap use and heavy
//      drawing inside real-time pathways; batch draw calls and refresh once.
//   2) Clear priority model: setting/parameter edit screens pre-empt default UI to
//      avoid flicker and conflicting renders.
//   3) Embedded-friendly timing: all temporary views use millis()-based timeouts
//      rather than blocking delays (except startup animation which is one-shot).
//   4) Portability: only rely on Arduino-compatible primitives and SH110X API.
//   5) Maintainability: readable layout constants and small helper functions.
// - Performance notes:
//   - We clear the display once per update and render everything for that frame,
//     then call display() exactly once to avoid partial refresh flicker.
//   - Geometry is computed with simple integer math to keep CPU usage low.
//   - Where possible we reuse UIState/Sequencer data to avoid recomputation.
// =======================================================================
OLEDDisplay::OLEDDisplay() : displayHardware(OLEDConstants::SCREEN_WIDTH, OLEDConstants::SCREEN_HEIGHT, &Wire, OLEDConstants::RESET_PIN),
                             isDisplayInitialized(false)
{
}

// begin():
// - Brings up the OLED hardware at the configured I2C address, returns false on
//   failure so caller can degrade gracefully.
// - Sets default text properties so subsequent draw calls have predictable state.
// - Runs a one-shot startup animation (it uses delays by design since it's only
//   executed during boot and never in the real-time audio loop).
bool OLEDDisplay::begin()
{
  // Initialize display hardware with I2C address
  if (!displayHardware.begin(OLEDConstants::I2C_ADDRESS, true))
  {
    Serial.println("[ERROR] OLED display initialization failed!");
    return false;
  }

  isDisplayInitialized = true;

  // Clear display and set default text properties
  displayHardware.clearDisplay();
  displayHardware.setTextSize(1);
  displayHardware.setTextColor(SH110X_WHITE);
  displayHardware.setCursor(0, 0);

  // Show startup animation
  runStartupAnimation();

  Serial.println("OLED display initialized successfully");
  return true;
}


void OLEDDisplay::clear()
{
  if (!isDisplayInitialized)
  {
    return;
  }

  displayHardware.clearDisplay();
  displayHardware.display();
}


void OLEDDisplay::setVoiceManager(VoiceManager *voiceManager)
{
  voiceManagerReference = voiceManager;
  Serial.println("OLED: Voice manager reference set");
}


void OLEDDisplay::displayVoiceParameterToggles(const UIState &uiState, VoiceManager *voiceManager)
{
  if (!isDisplayInitialized || !voiceManager)
  {
    return;
  }

  displayHardware.clearDisplay();
  displayHardware.setTextSize(1);
  displayHardware.setTextColor(SH110X_WHITE);

// Draw professional border
 displayHardware.drawRect(0, 0, OLEDConstants::SCREEN_WIDTH, OLEDConstants::SCREEN_HEIGHT, SH110X_WHITE);

  // Header with current voice indicator
  displayHardware.setCursor(OLEDConstants::TEXT_MARGIN - 3, 1);
  displayHardware.print("VOICE ");
  displayHardware.print(uiState.selectedVoiceIndex + 1);
  displayHardware.print(" PARAMETERS");

  // Draw separator line under header
  displayHardware.drawFastHLine(OLEDConstants::TEXT_MARGIN - 3, OLEDConstants::LINE_SPACING,
                                OLEDConstants::SCREEN_WIDTH - (2 * OLEDConstants::TEXT_MARGIN) + 6, SH110X_WHITE);

  // Map selected voice index to actual voice ID
  const uint8_t currentVoiceID = voiceSystem.getVoiceId(uiState.selectedVoiceIndex);
  VoiceConfig *voiceConfiguration = voiceManager->getVoiceConfig(currentVoiceID);

  if (!voiceConfiguration)
  {
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN - 3, 25);
    displayHardware.print("Voice config error");
    displayHardware.display();
    return;
  }

  // Voice parameter configuration data
  struct VoiceParameterDisplayInfo
  {
    const char *parameterName;
    int buttonNumber;
  };

  const VoiceParameterDisplayInfo parameterInfo[] = {
      {"Envelope", 8},
      {"Overdrive", 9},
      {"Wavefolder", 10},
      {"Filter Mode", 11},
      {"Filter Res", 12}};

  constexpr int parameterCount = sizeof(parameterInfo) / sizeof(parameterInfo[0]);

  // Display parameters in organized vertical layout
  const int startYPosition = 15;
  for (int paramIndex = 0; paramIndex < parameterCount; paramIndex++)
  {
    const int currentYPosition = startYPosition + (paramIndex * OLEDConstants::LINE_SPACING);

    // Display parameter name with colon
    displayHardware.setCursor(4, currentYPosition);
    displayHardware.print(parameterInfo[paramIndex].parameterName);
    displayHardware.print(":");

    // Display parameter value/state
    displayHardware.setCursor(70, currentYPosition);
    switch (parameterInfo[paramIndex].buttonNumber)
    {
    case 8: // Envelope
      displayHardware.print(voiceConfiguration->hasEnvelope ? "ON" : "OFF");
      break;
    case 9: // Overdrive
      displayHardware.print(voiceConfiguration->hasOverdrive ? "ON" : "OFF");
      break;
    case 10: // Wavefolder
      displayHardware.print(voiceConfiguration->hasWavefolder ? "ON" : "OFF");
      break;
    case 11: // Filter Mode
    {
      const char *filterModeNames[] = {"LP12", "LP24", "LP36", "BP12", "BP24"};
      const int filterModeIndex = static_cast<int>(voiceConfiguration->filterMode);
      if (filterModeIndex >= 0 && filterModeIndex < 5)
      {
        displayHardware.print(filterModeNames[filterModeIndex]);
      }
      else
      {
        displayHardware.print("UNK");
      }
    }
    break;
    case 12: // Filter Resonance
      displayHardware.print(static_cast<int>(voiceConfiguration->filterRes * 100));
      displayHardware.print("%");
      break;
    default:
      break;
    }

    // Display button number indicator
    displayHardware.setCursor(110, currentYPosition);
    displayHardware.print("[");
    displayHardware.print(parameterInfo[paramIndex].buttonNumber);
    displayHardware.print("]");
  }

  displayHardware.display();
}

// update() (thin wrapper):
// - For convenience, delegates to the extended overload by passing a null manager.
//   Keeps call sites simple when voice config is not needed for that frame.
void OLEDDisplay::update(const UIState &uiState, const Sequencer &seq1, const Sequencer &seq2,
                         const Sequencer &seq3, const Sequencer &seq4)
{
  // Call extended version with null voice manager
  update(uiState, seq1, seq2, seq3, seq4, nullptr);
}

// update() (main):
// - The heart of the display state machine. It enforces a strict priority order so
//   that mutually exclusive views do not fight for the screen in a given frame.
//   Priority from highest to lowest:
//     1) In settings + voice parameter edit active (recent interaction window)
//     2) In settings main/preset menu
//     3) Transient voice parameter info (outside settings, brief after-change)
//     4) Default status (scale, shuffle, selected voice, step indicators)
// - Timing: uses millis()-based timeouts from UIState to show transient UIs without
//   blocking the main loop.
// - Efficiency: clears once, sets text props once, and renders one view per frame.
void OLEDDisplay::update(const UIState &uiState, const Sequencer &seq1, const Sequencer &seq2,
                         const Sequencer &seq3, const Sequencer &seq4, VoiceManager *voiceManager)
{
  if (!isDisplayInitialized)
  {
    return;
  }

  // Store voice manager reference for immediate updates
  voiceManagerReference = voiceManager;

  displayHardware.clearDisplay();
  displayHardware.setTextSize(1);
  displayHardware.setTextColor(SH110X_WHITE);


  // Priority-based display logic to prevent menu conflicts and ensure proper display hierarchy

  // HIGHEST PRIORITY: Voice parameter editing mode in settings
  if (uiState.settingsMode && voiceManager && uiState.inVoiceParameterMode &&
      (millis() - uiState.voiceParameterChangeTime < LEDConstants::VOICE_PARAM_SETTINGS_TIMEOUT_MS))
  {
    displayVoiceParameterToggles(uiState, voiceManager);
    displayHardware.display();
    return;
  }

  // MEDIUM PRIORITY: Settings mode (main menu or preset selection)
  if (uiState.settingsMode)
  {
    displaySettingsMenu(uiState);
    displayHardware.display();
    return;
  }

  // LOW PRIORITY: Voice parameter info display (outside settings mode)
  if (uiState.inVoiceParameterMode &&
      (millis() - uiState.voiceParameterChangeTime < LEDConstants::VOICE_PARAM_TIMEOUT_MS))
  {
    // Show voice parameter info for limited time after change
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, OLEDConstants::TEXT_MARGIN);
    displayHardware.setTextSize(1);
    displayHardware.print("VOICE PARAM MODE");

    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 20);
    displayHardware.print("Button: ");
    displayHardware.print(uiState.lastVoiceParameterButton);

    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 35);
    displayHardware.print("Voice: ");
    displayHardware.print(uiState.selectedVoiceIndex + 1);

    displayHardware.display();
    return;
  }
  else if (uiState.inVoiceParameterMode)
  {
    // Clear voice parameter mode after timeout
    const_cast<UIState &>(uiState).inVoiceParameterMode = false;
  }

  const ParamButtonMapping *heldParam = getHeldParameterButton(uiState);

  if (heldParam != nullptr)
  {
    // Display parameter editing information
    uint8_t voice = uiState.selectedVoiceIndex + 1;
    const Sequencer &currentSeq = (uiState.selectedVoiceIndex == 0) ? seq1 : (uiState.selectedVoiceIndex == 1) ? seq2
                                                                         : (uiState.selectedVoiceIndex == 2)   ? seq3
                                                                                                               : seq4;
    uint8_t currentStep = currentSeq.getCurrentStepForParameter(heldParam->paramId);
    float currentValue = currentSeq.getStepParameterValue(heldParam->paramId, currentStep);
    displayParameterInfo(heldParam->name, currentValue, voice, currentStep);
  }
  else if (uiState.selectedStepForEdit != -1)
  {
    // Step editing mode - show step parameter values
    if (uiState.currentEditParameter != ParamId::Count)
    {
      // Display the currently editing parameter for the selected step
      uint8_t voice = uiState.selectedVoiceIndex + 1;
      const Sequencer &currentSeq = (uiState.selectedVoiceIndex == 0) ? seq1 : (uiState.selectedVoiceIndex == 1) ? seq2
                                                                           : (uiState.selectedVoiceIndex == 2)   ? seq3
                                                                                                                 : seq4;
      float currentValue = currentSeq.getStepParameterValue(uiState.currentEditParameter, uiState.selectedStepForEdit);

      // Find parameter name
      const char *paramName = "Unknown";
      for (size_t i = 0; i < PARAM_BUTTON_MAPPINGS_SIZE; ++i)
      {
        if (PARAM_BUTTON_MAPPINGS[i].paramId == uiState.currentEditParameter)
        {
          paramName = PARAM_BUTTON_MAPPINGS[i].name;
          break;
        }
      }

      displayParameterInfo(paramName, currentValue, voice, uiState.selectedStepForEdit);
    }
    else
    {
      // No parameter selected - show step selection prompt
      displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 20);
      displayHardware.setTextSize(2);
      displayHardware.print("Step ");
      displayHardware.print(uiState.selectedStepForEdit + 1);

      displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 40);
      displayHardware.setTextSize(1);
      displayHardware.print("Press param button");
      displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 50);
      displayHardware.print("to edit");
    }
  }
  else
  {
    // Default screen: Show current scale and shuffle pattern with enhanced formatting

    // Default Screen
    displayHardware.setTextSize(1);

    // Scale section
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, OLEDConstants::TEXT_MARGIN);
    displayHardware.setTextSize(1);
    displayHardware.print(scaleNames[currentScale]);

    // Shuffle section
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 20);
    displayHardware.print(getShuffleTemplateName(uiState.currentShufflePatternIndex));

    // Voice status display
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 35);
    displayHardware.setTextSize(2);
    displayHardware.print("Voice: ");
    displayHardware.setTextSize(3);
    // Raise numeric voice index by 6px while keeping horizontal alignment after label
    displayHardware.setCursor(displayHardware.getCursorX(), 25); // 35 - 6

    displayHardware.print(uiState.selectedVoiceIndex + 1);

    // Beat-synced step indicators at the bottom
    const Sequencer &currentSequencerDefault = (uiState.selectedVoiceIndex == 0) ? seq1 : (uiState.selectedVoiceIndex == 1) ? seq2
                                                                                      : (uiState.selectedVoiceIndex == 2)   ? seq3
                                                                                                                            : seq4;
    drawStepIndicators(currentSequencerDefault, 63);
  }

  displayHardware.display();
}

void OLEDDisplay::displayParameterInfo(const char *parameterName, float currentValue,
                                       uint8_t voiceNumber, uint8_t stepIndex)
{
  // Parameter name display
  displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, OLEDConstants::TEXT_MARGIN);
  displayHardware.setTextSize(2);
  displayHardware.print(parameterName);

  // Voice indicator
  displayHardware.setTextSize(1);
  displayHardware.setCursor(100, OLEDConstants::TEXT_MARGIN);
  displayHardware.print("V");
  displayHardware.print(voiceNumber);

  // Current step indicator
  displayHardware.setCursor(100, 15);
  displayHardware.print("S");
  displayHardware.print(stepIndex + 1);

  // Separator line
  displayHardware.drawFastHLine(2, 24, OLEDConstants::SCREEN_WIDTH - 4, SH110X_WHITE);

  // Parameter value display
  displayHardware.setTextSize(2);
  displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 32);

  // Find the ParamId based on the parameter name for proper formatting
  ParamId parameterID = ParamId::Note; // Default fallback
  for (size_t mappingIndex = 0; mappingIndex < PARAM_BUTTON_MAPPINGS_SIZE; ++mappingIndex)
  {
    if (strcmp(PARAM_BUTTON_MAPPINGS[mappingIndex].name, parameterName) == 0)
    {
      parameterID = PARAM_BUTTON_MAPPINGS[mappingIndex].paramId;
      break;
    }
  }

  String formattedParameterValue = formatParameterValue(parameterID, currentValue);
  displayHardware.print(formattedParameterValue);

  // Progress bar for normalized parameters (exclude discrete parameters)
  if (parameterID != ParamId::Note && parameterID != ParamId::Octave &&
      parameterID != ParamId::Gate && parameterID != ParamId::Slide)
  {

    // Calculate progress bar dimensions
    const int progressBarWidth = OLEDConstants::SCREEN_WIDTH - 10;
    const int progressBarHeight = OLEDConstants::PROGRESS_BAR_HEIGHT;
    const int progressBarX = OLEDConstants::TEXT_MARGIN;
    const int progressBarY = 52;

    // Draw progress bar background
    displayHardware.drawRect(progressBarX, progressBarY, progressBarWidth, progressBarHeight, SH110X_WHITE);

    // Fill progress bar based on parameter value (0.0 to 1.0)
    const int fillWidth = static_cast<int>(currentValue * (progressBarWidth - 4));
    if (fillWidth > 0)
    {
      displayHardware.fillRect(progressBarX + 2, progressBarY + 2, fillWidth,
                               progressBarHeight - 4, SH110X_WHITE);
    }
  }
}

String OLEDDisplay::formatParameterValue(ParamId paramId, float value)
{
  switch (paramId)
  {
  case ParamId::Note:
    return String((int)value);

  case ParamId::Velocity:
    return String((int)(value * 100)) + "%";

  case ParamId::Filter:
  {
    int filterFreq = daisysp::fmap(value, 100.0f, 6710.0f, daisysp::Mapping::EXP);
    return String((int)(filterFreq)) + "Hz";
  }

  case ParamId::Attack:
    return String(value, 3) + "s";

  case ParamId::Decay:
    return String(value, 3) + "s";

  case ParamId::Octave:
    if (value < 0.15f)
      return "-1";
    else if (value > 0.4f)
      return "+1";
    else
      return "0";

  case ParamId::GateLength:
    return String((int)(value * 100)) + "%";

  case ParamId::Gate:
    return value > 0.5f ? "ON" : "OFF";

  case ParamId::Slide:
    return value > 0.5f ? "ON" : "OFF";

  default:
    return String(value, 2);
  }
}

void OLEDDisplay::displaySettingsMenu(const UIState &uiState)
{
  displayHardware.setTextSize(1);

  if (uiState.inPresetSelection)
  {
    // Enhanced preset selection with cycling interface
    int currentPresetIndex = (uiState.settingsMenuIndex < UIState::MAX_VOICES) ? 
                            uiState.voicePresetIndices[uiState.settingsMenuIndex] : 0;

    // Header with voice info
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, OLEDConstants::TEXT_MARGIN);
    displayHardware.print("VOICE ");
    displayHardware.print(uiState.settingsMenuIndex + 1);
    displayHardware.print(" PRESET");

    // Draw separator line
    displayHardware.drawFastHLine(OLEDConstants::TEXT_MARGIN, OLEDConstants::HEADER_HEIGHT,
                                  OLEDConstants::SCREEN_WIDTH - 10, SH110X_WHITE);

    // Current preset - large and centered
    displayHardware.setTextSize(2);
    const char *currentPresetName = VoicePresets::getPresetName(currentPresetIndex);
    int textWidth = strlen(currentPresetName) * 12; // Approximate width for size 2
    int centerX = (OLEDConstants::SCREEN_WIDTH - textWidth) / 2;
    displayHardware.setCursor(centerX, 20);
    displayHardware.print(currentPresetName);

    // Subtle underline animation
    uint8_t phase = (millis() / 120) % (OLEDConstants::SCREEN_WIDTH - 10);
    displayHardware.drawFastHLine(OLEDConstants::TEXT_MARGIN, 38, OLEDConstants::SCREEN_WIDTH - 10, SH110X_WHITE);
    displayHardware.drawFastHLine(OLEDConstants::TEXT_MARGIN, 39, phase, SH110X_WHITE);

    // Navigation indicators
    displayHardware.setTextSize(1);

    // Previous preset (if available)
    if (currentPresetIndex > 0)
    {
      displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 45);
      displayHardware.print("< ");
      displayHardware.print(VoicePresets::getPresetName(currentPresetIndex - 1));
    }

    // Next preset (if available)
    if (currentPresetIndex < VoicePresets::getPresetCount() - 1)
    {
      const char *nextPresetName = VoicePresets::getPresetName(currentPresetIndex + 1);
      int nextTextWidth = strlen(nextPresetName) * 6 + 12; // 6 pixels per char + "> " width
      displayHardware.setCursor(OLEDConstants::SCREEN_WIDTH - nextTextWidth, 45);
      displayHardware.print(nextPresetName);
      displayHardware.print(" >");
    }

    // Preset counter at bottom
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 56);
    displayHardware.print(currentPresetIndex + 1);
    displayHardware.print("/");
    displayHardware.print(VoicePresets::getPresetCount());
  }
  else
  {
    // Enhanced main settings menu
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, OLEDConstants::TEXT_MARGIN);
    displayHardware.setTextSize(1);
    displayHardware.print("Sound Buffet");

    // Draw separator line
    displayHardware.drawFastHLine(OLEDConstants::TEXT_MARGIN, OLEDConstants::HEADER_HEIGHT,
                                  OLEDConstants::SCREEN_WIDTH - 10, SH110X_WHITE);

    // Voice configurations with better visual hierarchy (4 voices)
    for (int voiceIndex = 0; voiceIndex < 4; voiceIndex++)
    {
      int yPosition = 20 + (voiceIndex * OLEDConstants::LINE_SPACING);

      // Animated bullet indicator
      uint8_t blinkPhase = ((millis() / 250) + voiceIndex) % 2;
      if (blinkPhase)
      {
        displayHardware.fillCircle(4, yPosition + 2, 2, SH110X_WHITE);
      }
      else
      {
        displayHardware.drawCircle(4, yPosition + 2, 2, SH110X_WHITE);
      }

      // Current preset name for each voice
      displayHardware.setCursor(12, yPosition);
      const char *presetName = (voiceIndex < UIState::MAX_VOICES) ? 
                            VoicePresets::getPresetName(uiState.voicePresetIndices[voiceIndex]) : 
                            "Unknown";
      displayHardware.print(presetName);
    }
  }
}

void OLEDDisplay::displayVoiceParameterInfo(const UIState &uiState, VoiceManager *voiceManager,
                                            uint8_t leadVoiceId, uint8_t bassVoiceId)
{
  if (!isDisplayInitialized || !voiceManager)
    return;

  displayHardware.clearDisplay();
  displayHardware.setTextSize(1);
  displayHardware.setTextColor(SH110X_WHITE);

  // Get current voice configuration
  uint8_t selected = uiState.selectedVoiceIndex;
  uint8_t currentVoiceId = (selected == 0) ? leadVoiceId : (selected == 1) ? bassVoiceId
                                                       : voiceSystem.getVoiceId(selected);
  VoiceConfig *config = voiceManager->getVoiceConfig(currentVoiceId);

  if (!config)
  {
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 20);
    displayHardware.print("Voice config error");
    displayHardware.display();
    return;
  }

  // Header
  displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, OLEDConstants::TEXT_MARGIN);
  displayHardware.setTextSize(1);
  displayHardware.print("VOICE ");
  displayHardware.print(selected + 1);
  displayHardware.print(" PARAMETERS");

  // Draw separator line
  displayHardware.drawFastHLine(OLEDConstants::TEXT_MARGIN, OLEDConstants::HEADER_HEIGHT,
                                OLEDConstants::SCREEN_WIDTH - 10, SH110X_WHITE);

  // Parameter information based on button pressed
  const char *paramName = "";
  String paramValue = "";

  switch (uiState.lastVoiceParameterButton)
  {
  case 9:
    paramName = "Envelope";
    paramValue = config->hasEnvelope ? "ON" : "OFF";
    break;
  case 10:
    paramName = "Overdrive";
    paramValue = config->hasOverdrive ? "ON" : "OFF";
    break;
  case 11:
    paramName = "Wavefolder";
    paramValue = config->hasWavefolder ? "ON" : "OFF";
    break;
  case 12:
  {
    paramName = "Filter Mode";
    const char *filterNames[] = {"LP12", "LP24", "LP36", "BP12", "BP24"};
    int mode = static_cast<int>(config->filterMode);
    if (mode >= 0 && mode < 5)
    {
      paramValue = filterNames[mode];
    }
    else
    {
      paramValue = "Unknown";
    }
  }
  break;
  case 13:
    paramName = "Filter Res";
    paramValue = String(config->filterRes, 2);
    break;
  default:
    paramName = "Parameter";
    paramValue = String(uiState.lastVoiceParameterButton);
    break;
  }

  // Display parameter name
  displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 20);
  displayHardware.setTextSize(1);
  displayHardware.print(paramName);
  displayHardware.print(":");

  // Display parameter value
  displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 35);
  displayHardware.setTextSize(2);
  displayHardware.print(paramValue);

  // Show button number
  displayHardware.setTextSize(1);
  displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 55);
  displayHardware.print("Button ");
  displayHardware.print(uiState.lastVoiceParameterButton);

  displayHardware.display();
}

void OLEDDisplay::forceUpdate(const UIState &uiState, VoiceManager *voiceManager)
{
  if (!isDisplayInitialized)
  {
    Serial.println("OLED: Force update failed - display not initialized");
    return;
  }

  if (!voiceManager)
  {
    Serial.println("OLED: Force update failed - voiceManager is null");
    return;
  }

  // Store voice manager reference
  voiceManagerReference = voiceManager;

  // Force immediate update if in settings mode to show voice parameter toggles
  if (uiState.settingsMode)
  {
    displayVoiceParameterToggles(uiState, voiceManager);
    displayHardware.display();
  }


}

void OLEDDisplay::onVoiceParameterChanged(uint8_t voiceId, const VoiceState &state)
{
  // This method is called immediately when a voice parameter changes
  // Provides immediate visual feedback with proper voice ID mapping

  if (!isDisplayInitialized)
  {
    Serial.println("OLED: Parameter change ignored - display not initialized");
    return;
  }

  if (!voiceManagerReference)
  {
    Serial.println("OLED: Parameter change ignored - no voice manager reference");
    return;
  }

  // Determine which voice this corresponds to
  uint8_t displayVoiceNumber = 0;
  bool voiceFound = false;
  
  for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++)
  {
    if (voiceId == voiceSystem.getVoiceId(i))
    {
      displayVoiceNumber = i + 1; // Convert to 1-based
      voiceFound = true;
      break;
    }
  }
  
  if (!voiceFound)
  {
    Serial.print("OLED: Warning - Unknown voice ID: ");
    Serial.println(voiceId);
    return;
  }
  /*
      // Comprehensive debug output for troubleshooting
      Serial.println("=== OLED Voice Parameter Change ===");
      Serial.print("Voice ID: ");
      Serial.print(voiceId);
      Serial.print(" -> Display Voice: ");
      Serial.println(displayVoiceNumber);
      Serial.print("Note: ");
      Serial.print(state.noteIndex);
      Serial.print(" Velocity: ");
      Serial.print(state.velocityLevel);
      Serial.print(" Filter: ");
      Serial.print(state.filterCutoff);
      Serial.print(" Attack: ");
      Serial.print(state.attackTimeSeconds);
      Serial.print(" Decay: ");
      Serial.println(state.decayTimeSeconds);
      Serial.print("Lead Voice ID: ");
      Serial.println(voice1Id);
      Serial.print("Bass Voice ID: ");
      Serial.println(voice2Id);
      Serial.println("=================================");


      */
  // Force immediate display update if we have the voice manager
  // This will trigger the display to show the current parameter states
  if (voiceManagerReference)
  {
    Serial.println("OLED: Triggering immediate display refresh");
    // Note: The actual display update will be handled by the main loop
    // calling forceUpdate() with the current UIState
  }
}

void OLEDDisplay::onVoiceSwitched(const UIState &uiState, VoiceManager *voiceManager)
{
  if (!isDisplayInitialized)
  {
    Serial.println("OLED: Voice switch ignored - display not initialized");
    return;
  }

  if (!voiceManager)
  {
    Serial.println("OLED: Voice switch ignored - no voice manager");
    return;
  }

  // Store voice manager reference
  voiceManagerReference = voiceManager;

  // Force immediate update if in settings mode to show voice parameter toggles
  if (uiState.settingsMode)
  {
    displayVoiceParameterToggles(uiState, voiceManager);
    displayHardware.display();
  }


}

void OLEDDisplay::onVoiceSwitched(uint8_t newVoiceId)
{
  // Interface-compliant method for VoiceParameterObserver
  // This is called when the voice system switches to a new voice

  if (!isDisplayInitialized)
  {
    Serial.println("OLED: Voice switch ignored - display not initialized");
    return;
  }

  // Serial.print("OLED: Voice switched to ID ");
  // Serial.println(newVoiceId);

  // This method provides the minimal interface compliance
  // The extended version with UIState and VoiceManager parameters
  // should be used for full functionality
}

void OLEDDisplay::drawStepIndicators(const Sequencer &sequencer, int yPosition)
{
  // Draw step indicator bars showing gate states and current playhead position
  uint8_t stepCount = sequencer.getParameterStepCount(ParamId::Gate);
  if (stepCount == 0)
  {
    stepCount = LEDConstants::MAX_STEP_BUTTONS; // Default to 16 steps
  }
  stepCount = min(stepCount, static_cast<uint8_t>(32)); // Limit to display width

  const uint8_t currentStepIndex = sequencer.getCurrentStep();
  const int leftMargin = 4;
  const int rightMargin = OLEDConstants::SCREEN_WIDTH - 4;
  const int totalWidth = rightMargin - leftMargin;

  // Draw individual step indicators
  for (uint8_t stepIndex = 0; stepIndex < stepCount; ++stepIndex)
  {
    // Calculate step indicator position and width
    const int stepXPosition = leftMargin + (stepIndex * totalWidth) / stepCount;
    const int nextStepXPosition = leftMargin + ((stepIndex + 1) * totalWidth) / stepCount;
    const int stepWidth = max(2, nextStepXPosition - stepXPosition - 1);

    // Get step gate state and determine if this is the current step
    const float gateValue = sequencer.getStepParameterValue(ParamId::Gate, stepIndex);
    const bool isGateActive = gateValue > 0.5f;
    const bool isCurrentStep = (stepIndex == currentStepIndex);

    // Calculate indicator height based on state
    int indicatorHeight;
    if (isCurrentStep)
    {
      indicatorHeight = OLEDConstants::STEP_INDICATOR_HEIGHT; // Tallest for current step
    }
    else if (isGateActive)
    {
      indicatorHeight = 6; // Medium height for active gates
    }
    else
    {
      indicatorHeight = 4; // Shortest for inactive gates
    }

    const int indicatorTopY = yPosition - indicatorHeight;

    // Draw step indicator (filled for active gates, outline for inactive)
    if (isGateActive)
    {
      displayHardware.fillRect(stepXPosition, indicatorTopY, stepWidth, indicatorHeight, SH110X_WHITE);
    }
    else
    {
      displayHardware.drawRect(stepXPosition, indicatorTopY, stepWidth, indicatorHeight, SH110X_WHITE);
    }
  }
}

void OLEDDisplay::runStartupAnimation()
{
  // Professional startup animation with wipe effect and title bounce
  displayHardware.clearDisplay();

  // Horizontal wipe effect across screen
  for (int wipeWidth = 0; wipeWidth <= OLEDConstants::SCREEN_WIDTH; wipeWidth += 10)
  {
    displayHardware.fillRect(0, 0, wipeWidth, OLEDConstants::SCREEN_HEIGHT, SH110X_WHITE);
    displayHardware.display();
    delay(OLEDConstants::STARTUP_WIPE_DELAY_MS);

    // Clear and redraw border for scanning effect
    displayHardware.clearDisplay();
    displayHardware.drawRect(0, 0, OLEDConstants::SCREEN_WIDTH, OLEDConstants::SCREEN_HEIGHT, SH110X_WHITE);
  }

  // Title bounce animation
  const char *applicationTitle = "ic alchemy";
  const int titleCharacterWidth = 12; // Approximate width per character at size 2
  const int titlePixelWidth = strlen(applicationTitle) * titleCharacterWidth;
  const int titleCenterX = (OLEDConstants::SCREEN_WIDTH - titlePixelWidth) / 2;

  // Animate title dropping down and bouncing
  for (int titleY = -16; titleY <= 18; titleY += 3)
  {
    displayHardware.clearDisplay();

    displayHardware.setTextSize(2);
    displayHardware.setCursor(titleCenterX, titleY);
    displayHardware.print(applicationTitle);
    displayHardware.display();

    delay(OLEDConstants::STARTUP_BOUNCE_DELAY_MS);
  }

  // Final settle with subtitle
  displayHardware.setTextSize(1);
  displayHardware.setCursor(18, 44);
  displayHardware.print("Let's play");
  displayHardware.display();
  delay(OLEDConstants::STARTUP_SETTLE_DELAY_MS);
}